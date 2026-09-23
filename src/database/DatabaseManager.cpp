#include "database/DatabaseManager.h"

#include "database/DataDirectory.h"
#include "utils/TimeUtils.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QUuid>

namespace {
constexpr int kPasswordIterations = 100000;
constexpr int kPasswordKeyLength = 32;
constexpr int kCurrentSchemaVersion = 1;
constexpr int kBusyTimeoutMs = 5000;
const char *kConnectionName = "mu_monitor_sqlite";

QString quoteSqlString(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'") + value + QStringLiteral("'");
}

QString uniqueConnectionName(const QString &prefix)
{
    return prefix + QStringLiteral("-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

DatabaseManager &DatabaseManager::instance()
{
    static DatabaseManager manager;
    return manager;
}

DatabaseManager::DatabaseManager()
    : m_connectionName(QString::fromLatin1(kConnectionName))
{
}

DatabaseManager::~DatabaseManager()
{
    if (QCoreApplication::instance()) {
        shutdown();
    }
}

bool DatabaseManager::initialize(QString *errorMessage)
{
    if (m_initialized) {
        return true;
    }

    if (m_database.isValid() || QSqlDatabase::contains(m_connectionName)) {
        shutdown();
    }

    m_lastError.clear();
    if (errorMessage) {
        errorMessage->clear();
    }

    auto fail = [this, errorMessage](const QString &message) {
        m_lastError = message;
        if (errorMessage) {
            *errorMessage = message;
        }
        shutdown();
        return false;
    };

    QString resolveError;
    const DataDirectory::Paths paths = DataDirectory::resolve(
        QCoreApplication::arguments(), QCoreApplication::applicationDirPath(), &resolveError);
    if (paths.root.isEmpty()) {
        return fail(resolveError.isEmpty() ? QStringLiteral("无法解析数据目录")
                                           : resolveError);
    }

    QString layoutError;
    if (!DataDirectory::ensureLayout(paths, &layoutError)) {
        return fail(layoutError);
    }

    m_databasePath = QDir(paths.database).filePath(QStringLiteral("mu-monitor.db"));
    m_backupsDirectory = paths.backups;

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        return fail(QStringLiteral("未找到 Qt SQLite 驱动 QSQLITE"));
    }

    QLockFile migrationLock(
        QDir(paths.runtime).filePath(QStringLiteral("database-migration.lock")));
    migrationLock.setStaleLockTime(30000);
    if (!migrationLock.tryLock(10000)) {
        return fail(QStringLiteral("无法获取数据库迁移锁，可能有另一个实例正在迁移：%1")
                        .arg(static_cast<int>(migrationLock.error())));
    }

    const bool databaseExistsBeforeOpen = QFileInfo::exists(m_databasePath);
    const bool legacyExists = !paths.legacyDatabase.isEmpty()
        && QFileInfo::exists(paths.legacyDatabase);

    if (databaseExistsBeforeOpen && legacyExists) {
        return fail(QStringLiteral(
            "新旧数据库同时存在，拒绝静默选择或自动合并。新数据库：%1；旧数据库：%2")
                        .arg(m_databasePath, paths.legacyDatabase));
    }

    if (!databaseExistsBeforeOpen && legacyExists) {
        if (!validateSqliteDatabase(paths.legacyDatabase, QStringLiteral("旧数据库"),
                                    errorMessage)) {
            return false;
        }
        if (!importLegacyDatabase(paths.legacyDatabase, m_databasePath, errorMessage)) {
            return false;
        }
    }

    const bool databaseExists = QFileInfo::exists(m_databasePath);
    if (!checkDatabaseWritable(databaseExists, errorMessage)) {
        return false;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databasePath);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(kBusyTimeoutMs));

    if (!m_database.open()) {
        return fail(QStringLiteral("无法打开数据库 %1：%2")
                        .arg(m_databasePath, m_database.lastError().text()));
    }

    if (!quickCheck(m_database, QStringLiteral("初始化数据库"), errorMessage)) {
        return fail(m_lastError);
    }
    if (!configureDatabase(errorMessage)) {
        return fail(m_lastError);
    }
    if (!migrateSchema(databaseExists, errorMessage)) {
        return fail(m_lastError);
    }
    if (!ensureDefaultUser(errorMessage)) {
        return fail(m_lastError);
    }

    m_initialized = true;
    return true;
}

void DatabaseManager::shutdown()
{
    if (m_database.isValid()) {
        m_database.close();
    }
    m_database = QSqlDatabase();

    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_initialized = false;
}

bool DatabaseManager::checkDatabaseWritable(bool databaseExists, QString *errorMessage)
{
    const QString directory = QFileInfo(m_databasePath).absolutePath();
    const QFileInfo directoryInfo(directory);
    if (!directoryInfo.isDir() || !directoryInfo.isWritable()) {
        m_lastError = QStringLiteral("数据库目录不可写：%1").arg(directory);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    QFile probe(QDir(directory).filePath(
        QStringLiteral(".write-probe-%1.tmp")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))));
    if (!probe.open(QIODevice::WriteOnly)) {
        m_lastError = QStringLiteral("数据库目录不可写：%1").arg(directory);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const bool written = probe.write("1") == 1 && probe.flush();
    probe.close();
    if (!written || !QFile::remove(probe.fileName())) {
        m_lastError = QStringLiteral("验证数据库目录可写性失败：%1").arg(directory);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    if (!databaseExists) {
        return true;
    }

    const QFileInfo info(m_databasePath);
    if (!info.isFile()) {
        m_lastError = QStringLiteral("数据库路径不是普通文件：%1").arg(m_databasePath);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    if (!info.isWritable()) {
        m_lastError = QStringLiteral("数据库文件不可写：%1").arg(m_databasePath);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    QFile databaseFile(m_databasePath);
    if (!databaseFile.open(QIODevice::ReadWrite | QIODevice::ExistingOnly)) {
        m_lastError = QStringLiteral("无法以读写方式打开数据库：%1")
                          .arg(databaseFile.errorString());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    databaseFile.close();
    return true;
}

bool DatabaseManager::quickCheck(QSqlDatabase &database, const QString &context,
                                 QString *errorMessage)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
        m_lastError = QStringLiteral("%1 quick_check 执行失败：%2")
                          .arg(context, query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    bool foundResult = false;
    while (query.next()) {
        foundResult = true;
        const QString result = query.value(0).toString();
        if (result.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
            m_lastError = QStringLiteral("%1 quick_check 失败：%2").arg(context, result);
            if (errorMessage) *errorMessage = m_lastError;
            return false;
        }
    }

    if (!foundResult) {
        m_lastError = QStringLiteral("%1 quick_check 未返回结果").arg(context);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::configureDatabase(QString *errorMessage)
{
    auto fail = [this, errorMessage](const QString &message) {
        m_lastError = message;
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    };

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        return fail(QStringLiteral("启用外键约束失败：%1").arg(query.lastError().text()));
    }
    if (!query.exec(QStringLiteral("PRAGMA journal_mode = WAL"))) {
        return fail(QStringLiteral("启用 WAL 失败：%1").arg(query.lastError().text()));
    }
    if (!query.exec(QStringLiteral("PRAGMA busy_timeout = %1").arg(kBusyTimeoutMs))) {
        return fail(QStringLiteral("设置 busy_timeout 失败：%1").arg(query.lastError().text()));
    }

    if (!query.exec(QStringLiteral("PRAGMA foreign_keys"))
        || !query.next() || query.value(0).toInt() != 1) {
        return fail(QStringLiteral("外键约束未启用：%1").arg(query.lastError().text()));
    }
    if (!query.exec(QStringLiteral("PRAGMA busy_timeout"))
        || !query.next() || query.value(0).toInt() != kBusyTimeoutMs) {
        return fail(QStringLiteral("busy_timeout 未生效：%1").arg(query.lastError().text()));
    }
    if (!query.exec(QStringLiteral("PRAGMA journal_mode"))
        || !query.next()
        || query.value(0).toString().compare(QStringLiteral("wal"), Qt::CaseInsensitive) != 0) {
        return fail(QStringLiteral("SQLite 未运行在 WAL 模式：%1").arg(query.lastError().text()));
    }
    return true;
}

bool DatabaseManager::readSchemaVersion(bool *hasVersionTable, int *version,
                                        QString *errorMessage)
{
    if (!hasVersionTable || !version) {
        m_lastError = QStringLiteral("读取 schema_version 的参数无效");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    QSqlQuery tableQuery(m_database);
    tableQuery.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'schema_version'"));
    if (!tableQuery.exec()) {
        m_lastError = QStringLiteral("检查 schema_version 表失败：%1")
                          .arg(tableQuery.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    *hasVersionTable = tableQuery.next();
    *version = 0;
    if (!*hasVersionTable) {
        return true;
    }

    QSqlQuery versionQuery(m_database);
    if (!versionQuery.exec(QStringLiteral("SELECT MAX(version) FROM schema_version"))) {
        m_lastError = QStringLiteral("读取数据库版本失败：%1")
                          .arg(versionQuery.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    if (!versionQuery.next()) {
        m_lastError = QStringLiteral("读取数据库版本未返回结果");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    if (versionQuery.value(0).isNull()) {
        return true;
    }

    bool ok = false;
    const int parsedVersion = versionQuery.value(0).toInt(&ok);
    if (!ok || parsedVersion < 0) {
        m_lastError = QStringLiteral("schema_version 内容无效");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    *version = parsedVersion;
    return true;
}

bool DatabaseManager::migrateSchema(bool databaseExisted, QString *errorMessage)
{
    bool hasVersionTable = false;
    int currentVersion = 0;
    if (!readSchemaVersion(&hasVersionTable, &currentVersion, errorMessage)) {
        return false;
    }

    if (currentVersion > kCurrentSchemaVersion) {
        m_lastError = QStringLiteral("数据库版本 %1 高于程序支持版本 %2，拒绝降级")
                          .arg(currentVersion)
                          .arg(kCurrentSchemaVersion);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    if (currentVersion == kCurrentSchemaVersion) {
        return true;
    }

    if (databaseExisted) {
        if (!backupDatabase(currentVersion, errorMessage)) {
            return false;
        }
    }

    if (!m_database.transaction()) {
        m_lastError = QStringLiteral("无法开始数据库迁移事务：%1")
                          .arg(m_database.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    auto rollbackFail = [this, errorMessage](const QString &message) {
        m_database.rollback();
        m_lastError = message;
        if (errorMessage) *errorMessage = message;
        return false;
    };

    QSqlQuery versionTableQuery(m_database);
    if (!versionTableQuery.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "version INTEGER PRIMARY KEY,"
            "description TEXT NOT NULL,"
            "applied_at TEXT NOT NULL)"))) {
        return rollbackFail(QStringLiteral("创建 schema_version 表失败：%1")
                                .arg(versionTableQuery.lastError().text()));
    }

    for (int targetVersion = currentVersion + 1;
         targetVersion <= kCurrentSchemaVersion; ++targetVersion) {
        QString migrationError;
        if (targetVersion == 1) {
            if (!createTables(&migrationError)
                || !normalizeTimestampStorage(&migrationError)
                || !normalizeTelemetryStatusStorage(&migrationError)) {
                return rollbackFail(migrationError);
            }
        } else {
            return rollbackFail(QStringLiteral("缺少数据库迁移 %1").arg(targetVersion));
        }

        QSqlQuery recordVersion(m_database);
        recordVersion.prepare(QStringLiteral(
            "INSERT INTO schema_version (version, description, applied_at) "
            "VALUES (?, ?, ?)"));
        recordVersion.addBindValue(targetVersion);
        recordVersion.addBindValue(QStringLiteral("基线数据库结构"));
        recordVersion.addBindValue(TimeUtils::toUtcIso8601());
        if (!recordVersion.exec()) {
            return rollbackFail(QStringLiteral("记录数据库版本 %1 失败：%2")
                                    .arg(targetVersion)
                                    .arg(recordVersion.lastError().text()));
        }
    }

    if (!m_database.commit()) {
        m_database.rollback();
        m_lastError = QStringLiteral("提交数据库迁移失败：%1")
                          .arg(m_database.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::backupDatabase(int fromVersion, QString *errorMessage)
{
    if (m_backupsDirectory.isEmpty()) {
        m_lastError = QStringLiteral("备份目录未配置");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    if (!QDir().mkpath(m_backupsDirectory)) {
        m_lastError = QStringLiteral("无法创建备份目录：%1").arg(m_backupsDirectory);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QString timestamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"));
    const QString backupName = QStringLiteral("mu-monitor-before-v%1-%2-%3.db")
                                   .arg(fromVersion)
                                   .arg(timestamp,
                                        QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    const QString backupPath = QDir(m_backupsDirectory).filePath(backupName);

    QSqlQuery backup(m_database);
    if (!backup.exec(QStringLiteral("VACUUM INTO %1").arg(quoteSqlString(backupPath)))) {
        QFile::remove(backupPath);
        m_lastError = QStringLiteral("迁移前备份数据库失败：%1")
                          .arg(backup.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::validateSqliteDatabase(const QString &databasePath,
                                             const QString &context,
                                             QString *errorMessage)
{
    const QFileInfo info(databasePath);
    if (!info.isFile()) {
        m_lastError = QStringLiteral("%1不存在或不是普通文件：%2").arg(context, databasePath);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QString connectionName = uniqueConnectionName(QStringLiteral("mu_monitor_validate"));
    bool valid = false;
    QString validationError;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                          connectionName);
        database.setDatabaseName(databasePath);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!database.open()) {
            validationError = QStringLiteral("无法只读打开%1：%2")
                                  .arg(context, database.lastError().text());
        } else {
            valid = quickCheck(database, context, &validationError);
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (!valid) {
        m_lastError = validationError;
        if (errorMessage) *errorMessage = validationError;
    }
    return valid;
}

bool DatabaseManager::importLegacyDatabase(const QString &sourcePath,
                                           const QString &destinationPath,
                                           QString *errorMessage)
{
    if (QFileInfo::exists(destinationPath)) {
        m_lastError = QStringLiteral("导入旧数据库时目标数据库已存在：%1")
                          .arg(destinationPath);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QString temporaryPath = destinationPath + QStringLiteral(".import-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".tmp");
    const QString connectionName = uniqueConnectionName(QStringLiteral("mu_monitor_legacy"));
    bool copied = false;
    QString copyError;
    {
        QSqlDatabase source = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                        connectionName);
        source.setDatabaseName(sourcePath);
        source.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!source.open()) {
            copyError = QStringLiteral("无法只读打开旧数据库 %1：%2")
                            .arg(sourcePath, source.lastError().text());
        } else {
            QSqlQuery copy(source);
            if (!copy.exec(QStringLiteral("VACUUM INTO %1")
                               .arg(quoteSqlString(temporaryPath)))) {
                copyError = QStringLiteral("复制旧数据库失败：%1")
                                .arg(copy.lastError().text());
            } else {
                copied = true;
            }
            source.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (!copied) {
        QFile::remove(temporaryPath);
        m_lastError = copyError;
        if (errorMessage) *errorMessage = copyError;
        return false;
    }

    if (!QFile::rename(temporaryPath, destinationPath)) {
        QFile::remove(temporaryPath);
        m_lastError = QStringLiteral("无法将旧数据库副本移动到新位置：%1")
                          .arg(destinationPath);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::createTables(QString *errorMessage)
{
    QSqlQuery query(m_database);
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL UNIQUE,"
        "password_hash TEXT NOT NULL,"
        "salt TEXT NOT NULL,"
        "role TEXT NOT NULL DEFAULT 'user',"
        "created_at TEXT NOT NULL"
        ")");

    if (!query.exec(sql)) {
        m_lastError = QStringLiteral("创建 users 表失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    if (!ensureColumn(
            QStringLiteral("users"),
            QStringLiteral("role"),
            QStringLiteral("TEXT NOT NULL DEFAULT 'user'"),
            errorMessage)) {
        return false;
    }

    const QString sessionSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sessions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL,"
        "token_hash TEXT NOT NULL UNIQUE,"
        "expires_at TEXT NOT NULL,"
        "created_at TEXT NOT NULL,"
        "last_used_at TEXT NOT NULL"
        ")");

    if (!query.exec(sessionSql)) {
        m_lastError = QStringLiteral("创建 sessions 表失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QString telemetrySql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS telemetry ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "device_id TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "status TEXT NOT NULL,"
        "temperature REAL NOT NULL,"
        "pressure REAL NOT NULL,"
        "speed REAL NOT NULL,"
        "voltage REAL NOT NULL,"
        "recorded_at TEXT NOT NULL"
        ")");

    if (!query.exec(telemetrySql)) {
        m_lastError = QStringLiteral("创建 telemetry 表失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QString statusSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS device_status ("
        "device_id TEXT PRIMARY KEY,"
        "online INTEGER NOT NULL,"
        "collecting INTEGER NOT NULL DEFAULT 1,"
        "heartbeat_at TEXT NOT NULL,"
        "latency_ms INTEGER NOT NULL"
        ")");
    if (!query.exec(statusSql)) {
        m_lastError = QStringLiteral("创建 device_status 表失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    if (!ensureColumn(QStringLiteral("device_status"), QStringLiteral("collecting"),
                      QStringLiteral("INTEGER NOT NULL DEFAULT 1"), errorMessage)) {
        return false;
    }

    const QString logSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS system_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "level TEXT NOT NULL,"
        "source TEXT NOT NULL,"
        "message TEXT NOT NULL,"
        "created_at TEXT NOT NULL"
        ")");
    if (!query.exec(logSql)) {
        m_lastError = QStringLiteral("创建 system_logs 表失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QString deviceSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS devices ("
        "device_id TEXT PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "model TEXT NOT NULL DEFAULT '',"
        "location TEXT NOT NULL DEFAULT '',"
        "ip_address TEXT NOT NULL DEFAULT '',"
        "protocol TEXT NOT NULL DEFAULT '',"
        "notes TEXT NOT NULL DEFAULT '',"
        "updated_at TEXT NOT NULL"
        ")");
    if (!query.exec(deviceSql)) {
        m_lastError = QStringLiteral("创建 devices 表失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QString alarmSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS alarms ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "device_id TEXT NOT NULL,"
        "level TEXT NOT NULL,"
        "message TEXT NOT NULL,"
        "occurred_at TEXT NOT NULL"
        ")");
    if (!query.exec(alarmSql)) {
        m_lastError = QStringLiteral("创建 alarms 表失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    const QString metaSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS app_meta ("
        "key TEXT PRIMARY KEY,"
        "value TEXT NOT NULL"
        ")");
    if (!query.exec(metaSql)) {
        m_lastError = QStringLiteral("创建 app_meta 表失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QStringList indexes = {
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_telemetry_recorded_at ON telemetry(recorded_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_telemetry_device_time ON telemetry(device_id, recorded_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_system_logs_created_at ON system_logs(created_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alarms_device_time ON alarms(device_id, occurred_at)"),
    };
    for (const QString &indexSql : indexes) {
        if (!query.exec(indexSql)) {
            m_lastError = QStringLiteral("创建 telemetry 索引失败：%1").arg(query.lastError().text());
            if (errorMessage) *errorMessage = m_lastError;
            return false;
        }
    }

    return true;
}

bool DatabaseManager::normalizeTimestampStorage(QString *errorMessage)
{
    const QString migrationKey = QStringLiteral("timestamp_iso8601_ms_utc_v2");

    QSqlQuery check(m_database);
    check.prepare(QStringLiteral("SELECT 1 FROM app_meta WHERE key = ?"));
    check.addBindValue(migrationKey);
    if (!check.exec()) {
        m_lastError = QStringLiteral("检查时间戳迁移状态失败：%1").arg(check.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    if (check.next()) {
        return true;
    }

    const QList<QPair<QString, QString>> timestampColumns = {
        {QStringLiteral("users"), QStringLiteral("created_at")},
        {QStringLiteral("sessions"), QStringLiteral("expires_at")},
        {QStringLiteral("sessions"), QStringLiteral("created_at")},
        {QStringLiteral("sessions"), QStringLiteral("last_used_at")},
        {QStringLiteral("telemetry"), QStringLiteral("recorded_at")},
        {QStringLiteral("device_status"), QStringLiteral("heartbeat_at")},
        {QStringLiteral("system_logs"), QStringLiteral("created_at")},
        {QStringLiteral("devices"), QStringLiteral("updated_at")},
        {QStringLiteral("alarms"), QStringLiteral("occurred_at")},
    };

    const QString standardTimestampPattern = QStringLiteral("____-__-__T__:__:__.___Z");

    for (const auto &timestampColumn : timestampColumns) {
        QSqlQuery select(m_database);
        const QString selectSql = QStringLiteral(
            "SELECT rowid, %1 FROM %2 WHERE %1 NOT LIKE ?")
            .arg(timestampColumn.second, timestampColumn.first);
        select.prepare(selectSql);
        select.addBindValue(standardTimestampPattern);
        if (!select.exec()) {
            m_lastError = QStringLiteral("读取 %1.%2 历史时间戳失败：%3")
                              .arg(timestampColumn.first, timestampColumn.second,
                                   select.lastError().text());
            if (errorMessage) *errorMessage = m_lastError;
            return false;
        }

        QList<QPair<QVariant, QString>> legacyRows;
        while (select.next()) {
            legacyRows.append({select.value(0), select.value(1).toString()});
        }

        for (const auto &legacyRow : legacyRows) {
            const QDateTime timestamp = TimeUtils::fromIso8601(legacyRow.second);
            if (!timestamp.isValid()) {
                continue;
            }

            QSqlQuery update(m_database);
            update.prepare(QStringLiteral("UPDATE %1 SET %2 = ? WHERE rowid = ?")
                               .arg(timestampColumn.first, timestampColumn.second));
            update.addBindValue(TimeUtils::toUtcIso8601(timestamp));
            update.addBindValue(legacyRow.first);
            if (!update.exec()) {
                m_lastError = QStringLiteral("迁移 %1.%2 时间戳失败：%3")
                                  .arg(timestampColumn.first, timestampColumn.second,
                                       update.lastError().text());
                if (errorMessage) *errorMessage = m_lastError;
                return false;
            }
        }
    }

    QSqlQuery marker(m_database);
    marker.prepare(QStringLiteral("INSERT INTO app_meta (key, value) VALUES (?, ?)"));
    marker.addBindValue(migrationKey);
    marker.addBindValue(TimeUtils::toUtcIso8601());
    if (!marker.exec()) {
        m_lastError = QStringLiteral("记录时间戳迁移状态失败：%1").arg(marker.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    return true;
}

bool DatabaseManager::normalizeTelemetryStatusStorage(QString *errorMessage)
{
    const QString migrationKey = QStringLiteral("telemetry_status_codes_v1");

    QSqlQuery check(m_database);
    check.prepare(QStringLiteral("SELECT 1 FROM app_meta WHERE key = ?"));
    check.addBindValue(migrationKey);
    if (!check.exec()) {
        m_lastError = QStringLiteral("检查遥测状态迁移状态失败：%1").arg(check.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    if (check.next()) {
        return true;
    }

    const QList<QPair<QString, QString>> mappings = {
        {QStringLiteral("在线"), QStringLiteral("online")},
        {QStringLiteral("离线"), QStringLiteral("offline")},
        {QStringLiteral("报警"), QStringLiteral("alarm")},
        {QStringLiteral("已停止"), QStringLiteral("stopped")},
        {QStringLiteral("采集中"), QStringLiteral("online")},
        {QStringLiteral("未连接"), QStringLiteral("offline")},
    };

    for (const auto &mapping : mappings) {
        QSqlQuery update(m_database);
        update.prepare(QStringLiteral("UPDATE telemetry SET status = ? WHERE status = ?"));
        update.addBindValue(mapping.second);
        update.addBindValue(mapping.first);
        if (!update.exec()) {
            m_lastError = QStringLiteral("迁移遥测状态 %1 失败：%2")
                              .arg(mapping.first, update.lastError().text());
            if (errorMessage) *errorMessage = m_lastError;
            return false;
        }
    }

    QSqlQuery marker(m_database);
    marker.prepare(QStringLiteral("INSERT INTO app_meta (key, value) VALUES (?, ?)"));
    marker.addBindValue(migrationKey);
    marker.addBindValue(TimeUtils::toUtcIso8601());
    if (!marker.exec()) {
        m_lastError = QStringLiteral("记录遥测状态迁移状态失败：%1").arg(marker.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    return true;
}
bool DatabaseManager::ensureColumn(const QString &table, const QString &column,
                                   const QString &definition, QString *errorMessage)
{
    QSqlQuery info(m_database);
    if (!info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        m_lastError = QStringLiteral("读取表结构失败：%1").arg(info.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    while (info.next()) {
        if (info.value(1).toString() == column) {
            return true;
        }
    }

    QSqlQuery alter(m_database);
    const QString sql = QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                            .arg(table, column, definition);
    if (!alter.exec(sql)) {
        m_lastError = QStringLiteral("新增字段 %1 失败：%2").arg(column, alter.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::ensureDefaultUser(QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE username = ?"));
    query.addBindValue(QStringLiteral("admin"));
    if (!query.exec() || !query.next()) {
        m_lastError = QStringLiteral("检查默认账号失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    if (query.value(0).toInt() > 0) {
        QSqlQuery update(m_database);
        update.prepare(QStringLiteral(
            "UPDATE users SET role = 'admin' WHERE username = 'admin' AND role != 'admin'"));
        if (!update.exec()) {
            m_lastError = QStringLiteral("更新默认管理员角色失败：%1").arg(update.lastError().text());
            if (errorMessage) *errorMessage = m_lastError;
            return false;
        }
        return true;
    }

    const QString saltHex = generateSaltHex();
    const QByteArray salt = QByteArray::fromHex(saltHex.toLatin1());
    const QString hashHex = QString::fromLatin1(passwordHash(QStringLiteral("123456"), salt).toHex());

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO users (username, password_hash, salt, role, created_at) "
        "VALUES (?, ?, ?, ?, ?)"));
    insert.addBindValue(QStringLiteral("admin"));
    insert.addBindValue(hashHex);
    insert.addBindValue(saltHex);
    insert.addBindValue(QStringLiteral("admin"));
    insert.addBindValue(TimeUtils::toUtcIso8601());

    if (!insert.exec()) {
        m_lastError = QStringLiteral("创建默认账号失败：%1").arg(insert.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::validateUser(const QString &username, const QString &password)
{
    if (!m_initialized && !initialize()) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT password_hash, salt FROM users WHERE username = ?"));
    query.addBindValue(username.trimmed());
    if (!query.exec() || !query.next()) {
        m_lastError = QStringLiteral("用户名或密码错误");
        return false;
    }

    const QString storedHash = query.value(0).toString().toLower();
    const QByteArray salt = QByteArray::fromHex(query.value(1).toString().toLatin1());
    const QString computedHash = QString::fromLatin1(passwordHash(password, salt).toHex()).toLower();

    if (storedHash.size() != computedHash.size()) {
        return false;
    }

    int diff = 0;
    for (int i = 0; i < storedHash.size(); ++i) {
        diff |= storedHash.at(i).unicode() ^ computedHash.at(i).unicode();
    }
    return diff == 0;
}

bool DatabaseManager::createRememberSession(const QString &username, QString *rawToken,
                                            QString *errorMessage)
{
    cleanupExpiredSessions();

    QSqlQuery remove(m_database);
    remove.prepare(QStringLiteral("DELETE FROM sessions WHERE username = ?"));
    remove.addBindValue(username.trimmed());
    if (!remove.exec()) {
        m_lastError = QStringLiteral("清理旧登录会话失败：%1").arg(remove.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QString token = generateTokenHex();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime expiresAt = now.addDays(30);

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO sessions (username, token_hash, expires_at, created_at, last_used_at) "
        "VALUES (?, ?, ?, ?, ?)"));
    insert.addBindValue(username.trimmed());
    insert.addBindValue(tokenHash(token));
    insert.addBindValue(TimeUtils::toUtcIso8601(expiresAt));
    insert.addBindValue(TimeUtils::toUtcIso8601(now));
    insert.addBindValue(TimeUtils::toUtcIso8601(now));

    if (!insert.exec()) {
        m_lastError = QStringLiteral("创建免登录会话失败：%1").arg(insert.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    if (rawToken) *rawToken = token;
    return true;
}

bool DatabaseManager::validateRememberSession(const QString &rawToken, QString *username,
                                              QString *errorMessage)
{
    if (!m_initialized && !initialize()) {
        return false;
    }

    if (rawToken.trimmed().isEmpty()) {
        return false;
    }

    cleanupExpiredSessions();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT username, expires_at FROM sessions WHERE token_hash = ?"));
    query.addBindValue(tokenHash(rawToken.trimmed()));

    if (!query.exec() || !query.next()) {
        m_lastError = QStringLiteral("未找到有效的免登录会话");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    const QString storedUser = query.value(0).toString();
    const QDateTime expiresAt = TimeUtils::fromIso8601(query.value(1).toString());
    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (!expiresAt.isValid() || expiresAt <= now) {
        revokeRememberSession(rawToken);
        m_lastError = QStringLiteral("免登录会话已过期");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    QSqlQuery update(m_database);
    update.prepare(QStringLiteral(
        "UPDATE sessions SET last_used_at = ?, expires_at = ? WHERE token_hash = ?"));
    update.addBindValue(TimeUtils::toUtcIso8601(now));
    update.addBindValue(TimeUtils::toUtcIso8601(now.addDays(30)));
    update.addBindValue(tokenHash(rawToken.trimmed()));
    update.exec();

    if (username) *username = storedUser;
    return true;
}

void DatabaseManager::revokeRememberSession(const QString &rawToken)
{
    if (!m_initialized || rawToken.trimmed().isEmpty()) {
        return;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM sessions WHERE token_hash = ?"));
    query.addBindValue(tokenHash(rawToken.trimmed()));
    query.exec();
}

void DatabaseManager::cleanupExpiredSessions()
{
    if (!m_initialized && !initialize()) {
        return;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM sessions WHERE expires_at <= ?"));
    query.addBindValue(TimeUtils::toUtcIso8601());
    query.exec();
}

bool DatabaseManager::insertTelemetryRecords(const QList<TelemetryRecord> &records,
                                             QString *errorMessage)
{
    if (records.isEmpty()) {
        return true;
    }

    if (!m_database.transaction()) {
        m_lastError = QStringLiteral("无法开始遥测数据事务：%1").arg(m_database.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO telemetry ("
        "device_id, name, status, temperature, pressure, speed, voltage, recorded_at"
        ") VALUES (:device_id, :name, :status, :temperature, :pressure, :speed, :voltage, :recorded_at)"));

    for (const TelemetryRecord &record : records) {
        const QDateTime timestamp = record.updatedAt.isValid()
            ? record.updatedAt.toUTC()
            : QDateTime::currentDateTimeUtc();

        query.bindValue(QStringLiteral(":device_id"), record.deviceId);
        query.bindValue(QStringLiteral(":name"), record.name);
        query.bindValue(QStringLiteral(":status"), telemetryStatusCode(record.status));
        query.bindValue(QStringLiteral(":temperature"), record.temperature);
        query.bindValue(QStringLiteral(":pressure"), record.pressure);
        query.bindValue(QStringLiteral(":speed"), record.speed);
        query.bindValue(QStringLiteral(":voltage"), record.voltage);
        query.bindValue(QStringLiteral(":recorded_at"), TimeUtils::toUtcIso8601(timestamp));

        if (!query.exec()) {
            m_database.rollback();
            m_lastError = QStringLiteral("写入遥测数据失败：%1").arg(query.lastError().text());
            if (errorMessage) *errorMessage = m_lastError;
            return false;
        }
    }

    if (!m_database.commit()) {
        m_database.rollback();
        m_lastError = QStringLiteral("提交遥测数据失败：%1").arg(m_database.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    return true;
}

QList<TelemetryRecord> DatabaseManager::latestDeviceRecords(QString *errorMessage)
{
    QList<TelemetryRecord> records;

    QSqlQuery query(m_database);
    const QString sql = QStringLiteral(
        "SELECT t.device_id, t.name, t.status, t.temperature, t.pressure, "
        "t.speed, t.voltage, t.recorded_at "
        "FROM telemetry t "
        "WHERE t.rowid = ("
        "  SELECT t2.rowid FROM telemetry t2 "
        "  WHERE t2.device_id = t.device_id "
        "  ORDER BY t2.recorded_at DESC, t2.rowid DESC LIMIT 1"
        ") "
        "ORDER BY t.device_id");

    if (!query.exec(sql)) {
        m_lastError = QStringLiteral("查询全部设备失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return records;
    }

    while (query.next()) {
        TelemetryRecord record;
        record.deviceId = query.value(0).toString();
        record.name = query.value(1).toString();
        record.status = telemetryStatusFromString(query.value(2).toString());
        record.temperature = query.value(3).toDouble();
        record.pressure = query.value(4).toDouble();
        record.speed = query.value(5).toDouble();
        record.voltage = query.value(6).toDouble();
        record.updatedAt = TimeUtils::fromIso8601(query.value(7).toString());
        records.append(record);
    }

    return records;
}

qint64 DatabaseManager::telemetryRecordCount(QString *errorMessage)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM telemetry")) || !query.next()) {
        m_lastError = QStringLiteral("统计遥测历史数量失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return 0;
    }
    return query.value(0).toLongLong();
}

QList<HeartbeatRecord> DatabaseManager::latestHeartbeatRecords(QString *errorMessage)
{
    QList<HeartbeatRecord> records;

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT device_id, online, collecting, heartbeat_at, latency_ms "
            "FROM device_status ORDER BY device_id"))) {
        m_lastError = QStringLiteral("查询设备心跳状态失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return records;
    }

    while (query.next()) {
        HeartbeatRecord record;
        record.deviceId = query.value(0).toString();
        record.online = query.value(1).toInt() != 0;
        record.collecting = query.value(2).toInt() != 0;
        record.heartbeatAt = TimeUtils::fromIso8601(query.value(3).toString());
        record.latencyMs = query.value(4).toInt();
        records.append(record);
    }

    return records;
}
QList<TelemetryRecord> DatabaseManager::recentTelemetryRecords(int limit,
                                                              const QString &deviceId,
                                                              QString *errorMessage)
{
    QList<TelemetryRecord> records;
    if (limit <= 0) {
        return records;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT device_id, name, status, temperature, pressure, speed, voltage, recorded_at "
        "FROM telemetry WHERE (? = '' OR device_id = ?) "
        "ORDER BY recorded_at DESC LIMIT ?"));
    query.addBindValue(deviceId);
    query.addBindValue(deviceId);
    query.addBindValue(limit);

    if (!query.exec()) {
        m_lastError = QStringLiteral("查询最近遥测历史失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return records;
    }

    while (query.next()) {
        TelemetryRecord record;
        record.deviceId = query.value(0).toString();
        record.name = query.value(1).toString();
        record.status = telemetryStatusFromString(query.value(2).toString());
        record.temperature = query.value(3).toDouble();
        record.pressure = query.value(4).toDouble();
        record.speed = query.value(5).toDouble();
        record.voltage = query.value(6).toDouble();
        record.updatedAt = TimeUtils::fromIso8601(query.value(7).toString());
        records.append(record);
    }

    return records;
}

QList<TelemetryRecord> DatabaseManager::telemetryHistory(const QDateTime &start,
                                                        const QDateTime &end,
                                                        const QString &deviceId,
                                                        int limit,
                                                        QString *errorMessage)
{
    QList<TelemetryRecord> records;
    if (!start.isValid() || !end.isValid() || start > end || limit <= 0) {
        return records;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT device_id, name, status, temperature, pressure, speed, voltage, recorded_at "
        "FROM telemetry WHERE recorded_at >= ? AND recorded_at <= ? "
        "AND (? = '' OR device_id = ?) "
        "ORDER BY recorded_at DESC LIMIT ?"));
    query.addBindValue(TimeUtils::toUtcIso8601(start));
    query.addBindValue(TimeUtils::toUtcIso8601(end));
    query.addBindValue(deviceId);
    query.addBindValue(deviceId);
    query.addBindValue(limit);

    if (!query.exec()) {
        m_lastError = QStringLiteral("按条件查询遥测历史失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return records;
    }

    while (query.next()) {
        TelemetryRecord record;
        record.deviceId = query.value(0).toString();
        record.name = query.value(1).toString();
        record.status = telemetryStatusFromString(query.value(2).toString());
        record.temperature = query.value(3).toDouble();
        record.pressure = query.value(4).toDouble();
        record.speed = query.value(5).toDouble();
        record.voltage = query.value(6).toDouble();
        record.updatedAt = TimeUtils::fromIso8601(query.value(7).toString());
        records.append(record);
    }

    return records;
}
QList<TelemetryRecord> DatabaseManager::telemetryBetween(const QDateTime &start,
                                                         const QDateTime &end,
                                                         QString *errorMessage)
{
    QList<TelemetryRecord> records;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT device_id, name, status, temperature, pressure, speed, voltage, recorded_at "
        "FROM telemetry WHERE recorded_at >= ? AND recorded_at <= ? "
        "ORDER BY recorded_at, device_id"));
    query.addBindValue(TimeUtils::toUtcIso8601(start));
    query.addBindValue(TimeUtils::toUtcIso8601(end));

    if (!query.exec()) {
        m_lastError = QStringLiteral("按时间查询遥测数据失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return records;
    }

    while (query.next()) {
        TelemetryRecord record;
        record.deviceId = query.value(0).toString();
        record.name = query.value(1).toString();
        record.status = telemetryStatusFromString(query.value(2).toString());
        record.temperature = query.value(3).toDouble();
        record.pressure = query.value(4).toDouble();
        record.speed = query.value(5).toDouble();
        record.voltage = query.value(6).toDouble();
        record.updatedAt = TimeUtils::fromIso8601(query.value(7).toString());
        records.append(record);
    }

    return records;
}

QList<DeviceInfo> DatabaseManager::deviceInfos(QString *errorMessage)
{
    QList<DeviceInfo> devices;

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT device_id, name, model, location, ip_address, protocol, notes "
            "FROM devices ORDER BY device_id"))) {
        m_lastError = QStringLiteral("查询设备信息失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return devices;
    }

    while (query.next()) {
        DeviceInfo device;
        device.deviceId = query.value(0).toString();
        device.name = query.value(1).toString();
        device.model = query.value(2).toString();
        device.location = query.value(3).toString();
        device.ipAddress = query.value(4).toString();
        device.protocol = query.value(5).toString();
        device.notes = query.value(6).toString();
        devices.append(device);
    }

    return devices;
}

bool DatabaseManager::ensureDeviceInfos(const QList<DeviceInfo> &devices,
                                        QString *errorMessage)
{
    if (devices.isEmpty()) {
        return true;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO devices "
        "(device_id, name, model, location, ip_address, protocol, notes, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));

    const auto textValue = [](const QString &value) {
        return value.isNull() ? QStringLiteral("") : value;
    };

    for (const DeviceInfo &device : devices) {
        query.bindValue(0, textValue(device.deviceId));
        query.bindValue(1, textValue(device.name));
        query.bindValue(2, textValue(device.model));
        query.bindValue(3, textValue(device.location));
        query.bindValue(4, textValue(device.ipAddress));
        query.bindValue(5, textValue(device.protocol));
        query.bindValue(6, textValue(device.notes));
        query.bindValue(7, TimeUtils::toUtcIso8601());
        if (!query.exec()) {
            m_lastError = QStringLiteral("初始化设备 %1 失败：%2")
                              .arg(device.deviceId, query.lastError().text());
            if (errorMessage) *errorMessage = m_lastError;
            return false;
        }
    }

    return true;
}

bool DatabaseManager::updateDeviceInfo(const DeviceInfo &device, QString *errorMessage)
{
    if (device.deviceId.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("设备编号不能为空");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO devices "
        "(device_id, name, model, location, ip_address, protocol, notes, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    const auto textValue = [](const QString &value) {
        return value.isNull() ? QStringLiteral("") : value.trimmed();
    };

    query.addBindValue(textValue(device.deviceId));
    query.addBindValue(textValue(device.name));
    query.addBindValue(textValue(device.model));
    query.addBindValue(textValue(device.location));
    query.addBindValue(textValue(device.ipAddress));
    query.addBindValue(textValue(device.protocol));
    query.addBindValue(textValue(device.notes));
    query.addBindValue(TimeUtils::toUtcIso8601());

    if (!query.exec()) {
        m_lastError = QStringLiteral("保存设备信息失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::insertAlarmRecord(const QString &deviceId,
                                        const QString &level,
                                        const QString &message,
                                        QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO alarms (device_id, level, message, occurred_at) VALUES (?, ?, ?, ?)"));
    query.addBindValue(deviceId);
    query.addBindValue(level);
    query.addBindValue(message);
    query.addBindValue(TimeUtils::toUtcIso8601());

    if (!query.exec()) {
        m_lastError = QStringLiteral("写入报警历史失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    return true;
}

QList<AlarmRecord> DatabaseManager::alarmHistoryForDevice(const QString &deviceId,
                                                         int limit,
                                                         QString *errorMessage)
{
    QList<AlarmRecord> alarms;
    if (limit <= 0) {
        return alarms;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT device_id, level, message, occurred_at "
        "FROM alarms WHERE device_id = ? ORDER BY occurred_at DESC LIMIT ?"));
    query.addBindValue(deviceId);
    query.addBindValue(limit);

    if (!query.exec()) {
        m_lastError = QStringLiteral("查询设备报警历史失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return alarms;
    }

    while (query.next()) {
        AlarmRecord alarm;
        alarm.deviceId = query.value(0).toString();
        alarm.level = query.value(1).toString();
        alarm.message = query.value(2).toString();
        alarm.occurredAt = TimeUtils::fromIso8601(query.value(3).toString());
        alarms.append(alarm);
    }

    return alarms;
}
bool DatabaseManager::insertHeartbeatRecords(const QList<HeartbeatRecord> &records,
                                            QString *errorMessage)
{
    if (records.isEmpty()) {
        return true;
    }

    if (!m_database.transaction()) {
        m_lastError = QStringLiteral("无法开始心跳事务：%1").arg(m_database.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO device_status "
        "(device_id, online, collecting, heartbeat_at, latency_ms) "
        "VALUES (:device_id, :online, :collecting, :heartbeat_at, :latency_ms)"));

    for (const HeartbeatRecord &record : records) {
        const QDateTime heartbeatAt = record.heartbeatAt.isValid()
            ? record.heartbeatAt.toUTC()
            : QDateTime::currentDateTimeUtc();

        query.bindValue(QStringLiteral(":device_id"), record.deviceId);
        query.bindValue(QStringLiteral(":online"), record.online ? 1 : 0);
        query.bindValue(QStringLiteral(":collecting"), record.collecting ? 1 : 0);
        query.bindValue(QStringLiteral(":heartbeat_at"), TimeUtils::toUtcIso8601(heartbeatAt));
        query.bindValue(QStringLiteral(":latency_ms"), record.latencyMs);

        if (!query.exec()) {
            m_database.rollback();
            m_lastError = QStringLiteral("写入心跳失败：%1").arg(query.lastError().text());
            if (errorMessage) *errorMessage = m_lastError;
            return false;
        }
    }

    if (!m_database.commit()) {
        m_database.rollback();
        m_lastError = QStringLiteral("提交心跳失败：%1").arg(m_database.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    return true;
}

bool DatabaseManager::insertLog(const QString &level, const QString &source,
                                const QString &message, QString *errorMessage)
{
    if (!m_initialized && !initialize()) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO system_logs (level, source, message, created_at) "
        "VALUES (?, ?, ?, ?)"));
    query.addBindValue(level);
    query.addBindValue(source);
    query.addBindValue(message);
    query.addBindValue(TimeUtils::toUtcIso8601());

    if (!query.exec()) {
        m_lastError = QStringLiteral("写入日志失败：%1").arg(query.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }
    return true;
}

QString DatabaseManager::roleForUser(const QString &username)
{
    if (!m_initialized && !initialize()) {
        return QStringLiteral("user");
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT role FROM users WHERE username = ?"));
    query.addBindValue(username.trimmed());
    if (!query.exec() || !query.next()) {
        return QStringLiteral("user");
    }

    const QString role = query.value(0).toString().trimmed();
    return role.isEmpty() ? QStringLiteral("user") : role;
}

QString DatabaseManager::databasePath() const
{
    return m_databasePath;
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

QByteArray DatabaseManager::passwordHash(const QString &password, const QByteArray &salt) const
{
    QByteArray digest = salt + password.toUtf8();

    for (int i = 0; i < kPasswordIterations; ++i) {
        QCryptographicHash hasher(QCryptographicHash::Sha256);
        hasher.addData(digest);
        hasher.addData(salt);
        hasher.addData(QByteArray::number(i));
        digest = hasher.result();
    }

    return digest.left(kPasswordKeyLength);
}

QString DatabaseManager::generateSaltHex() const
{
    QByteArray salt(16, '\0');
    for (int i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return QString::fromLatin1(salt.toHex());
}

QString DatabaseManager::generateTokenHex() const
{
    QByteArray token(32, '\0');
    for (int i = 0; i < token.size(); ++i) {
        token[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return QString::fromLatin1(token.toHex());
}

QString DatabaseManager::tokenHash(const QString &rawToken) const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(rawToken.toUtf8(), QCryptographicHash::Sha256).toHex());
}
