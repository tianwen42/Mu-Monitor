#include "database/DataDirectory.h"
#include "database/DatabaseManager.h"

#include "utils/TimeUtils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>
#include <QtTest>

#include <memory>

class DatabaseManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void initializesDefaultUserAndLayout();
    void createsMissingDatabase();
    void configuresSqliteForReliability();
    void reopensExistingDatabase();
    void rejectsCorruptDatabaseWithoutOverwriting();
    void rejectsUnwritableDataDirectory();
    void rejectsReadOnlyDatabase();

    void migratesLegacySchemaSuccessfully();
    void rollsBackFailedMigration();
    void importsLegacyDatabaseAndKeepsOriginal();
    void rejectsNewAndLegacyDatabaseConflict();
    void createsBackupBeforeMigration();
    void migratesAuthSchemaWithBackup();

    void rejectsInvalidCredentials();
    void rememberSessionRoundTrip();
    void storesTelemetryAndQueriesTimeRange();
    void storesHeartbeatAndSystemLog();

private:
    bool initialize(QString *errorMessage = nullptr);
    bool createSqliteDatabase(const QString &path, const QStringList &statements,
                              QString *errorMessage = nullptr);
    QVariant scalarValue(const QString &path, const QString &sql, bool *ok = nullptr,
                         QString *errorMessage = nullptr);
    bool objectExists(const QString &path, const QString &type, const QString &name,
                      QString *errorMessage = nullptr);
    int backupCount(QString *errorMessage = nullptr) const;
    bool removeLegacyTestDirectory() const;

    std::unique_ptr<QTemporaryDir> m_temporaryDirectory;
    QString m_dataDirectory;
    QString m_databasePath;
    QString m_legacyDatabase;
};

void DatabaseManagerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Mu-MonitorTests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Mu-MonitorDatabaseTest-%1").arg(QCoreApplication::applicationPid()));
}

void DatabaseManagerTest::init()
{
    DatabaseManager::instance().shutdown();

    QString resolveError;
    const DataDirectory::Paths defaultPaths = DataDirectory::resolve(
        {QCoreApplication::applicationFilePath()},
        QCoreApplication::applicationDirPath(), &resolveError);
    QVERIFY2(resolveError.isEmpty(), qPrintable(resolveError));
    m_legacyDatabase = defaultPaths.legacyDatabase;
    removeLegacyTestDirectory();

    m_temporaryDirectory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temporaryDirectory->isValid());
    m_dataDirectory = QDir(m_temporaryDirectory->path()).filePath(QStringLiteral("data"));
    m_databasePath = QDir(m_dataDirectory).filePath(QStringLiteral("database/mu-monitor.db"));
    qputenv("MU_MONITOR_DATA_DIR", m_dataDirectory.toUtf8());
}

void DatabaseManagerTest::cleanup()
{
    DatabaseManager::instance().shutdown();
    removeLegacyTestDirectory();
    qunsetenv("MU_MONITOR_DATA_DIR");
    m_temporaryDirectory.reset();
}

bool DatabaseManagerTest::initialize(QString *errorMessage)
{
    QString message;
    const bool initialized = DatabaseManager::instance().initialize(&message);
    if (errorMessage) {
        *errorMessage = message;
    }
    return initialized;
}

bool DatabaseManagerTest::createSqliteDatabase(const QString &path,
                                               const QStringList &statements,
                                               QString *errorMessage)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建数据库目录");
        return false;
    }

    const QString connectionName = QStringLiteral("DatabaseManagerTestCreate-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool success = false;
    QString failure;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                          connectionName);
        database.setDatabaseName(path);
        if (!database.open()) {
            failure = database.lastError().text();
        } else {
            success = true;
            for (const QString &statement : statements) {
                QSqlQuery query(database);
                if (!query.exec(statement)) {
                    failure = query.lastError().text();
                    success = false;
                    break;
                }
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (!success && errorMessage) {
        *errorMessage = failure;
    }
    return success;
}

QVariant DatabaseManagerTest::scalarValue(const QString &path, const QString &sql,
                                          bool *ok, QString *errorMessage)
{
    const QString connectionName = QStringLiteral("DatabaseManagerTestQuery-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool success = false;
    QVariant value;
    QString failure;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                          connectionName);
        database.setDatabaseName(path);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!database.open()) {
            failure = database.lastError().text();
        } else {
            QSqlQuery query(database);
            if (query.exec(sql) && query.next()) {
                value = query.value(0);
                success = true;
            } else {
                failure = query.lastError().text();
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (ok) *ok = success;
    if (!success && errorMessage) *errorMessage = failure;
    return value;
}

bool DatabaseManagerTest::objectExists(const QString &path, const QString &type,
                                       const QString &name, QString *errorMessage)
{
    bool ok = false;
    QString queryError;
    const QVariant value = scalarValue(
        path,
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = '%1' AND name = '%2'")
            .arg(type, name),
        &ok, &queryError);
    if (!ok) {
        if (errorMessage) *errorMessage = queryError;
        return false;
    }
    return value.toInt() > 0;
}

int DatabaseManagerTest::backupCount(QString *errorMessage) const
{
    const QString backupDirectory = QDir(m_dataDirectory).filePath(QStringLiteral("database/backups"));
    if (!QFileInfo(backupDirectory).exists()) {
        return 0;
    }
    if (!QFileInfo(backupDirectory).isDir()) {
        if (errorMessage) *errorMessage = QStringLiteral("备份路径不是目录");
        return -1;
    }
    return QDir(backupDirectory).entryList(
        {QStringLiteral("mu-monitor-before-v*.db")}, QDir::Files).size();
}

bool DatabaseManagerTest::removeLegacyTestDirectory() const
{
    if (m_legacyDatabase.isEmpty()) {
        return false;
    }

    const QString directory = QFileInfo(m_legacyDatabase).absolutePath();
    const QString normalized = QDir::fromNativeSeparators(directory).toLower();
    if (!normalized.contains(QStringLiteral("qttest"))
        && !normalized.contains(QStringLiteral("mu-monitordatabasetest"))) {
        return false;
    }
    return QDir(directory).removeRecursively() || !QFileInfo::exists(directory);
}

void DatabaseManagerTest::initializesDefaultUserAndLayout()
{
    QVERIFY(initialize());
    QCOMPARE(DatabaseManager::instance().databasePath(), m_databasePath);
    QVERIFY(QFileInfo::exists(m_databasePath));

    const QStringList directories = {
        QDir(m_dataDirectory).filePath(QStringLiteral("database")),
        QDir(m_dataDirectory).filePath(QStringLiteral("database/backups")),
        QDir(m_dataDirectory).filePath(QStringLiteral("logs")),
        QDir(m_dataDirectory).filePath(QStringLiteral("exports")),
        QDir(m_dataDirectory).filePath(QStringLiteral("runtime")),
        QDir(m_dataDirectory).filePath(QStringLiteral("config")),
    };
    for (const QString &directory : directories) {
        QVERIFY2(QFileInfo(directory).isDir(), qPrintable(directory));
    }

    QCOMPARE(DatabaseManager::instance().roleForUser(QStringLiteral("admin")),
             QStringLiteral("admin"));
    QCOMPARE(scalarValue(m_databasePath,
                         QStringLiteral("SELECT MAX(version) FROM schema_version")).toInt(),
             2);
}

void DatabaseManagerTest::createsMissingDatabase()
{
    QVERIFY(!QFileInfo::exists(m_databasePath));
    QString errorMessage;
    QVERIFY2(initialize(&errorMessage), qPrintable(errorMessage));
    QVERIFY(QFileInfo(m_databasePath).isFile());
    QVERIFY(QFileInfo(m_databasePath).size() > 0);
}

void DatabaseManagerTest::configuresSqliteForReliability()
{
    QVERIFY(initialize());

    QSqlDatabase activeConnection =
        QSqlDatabase::database(QStringLiteral("mu_monitor_sqlite"));
    QVERIFY(activeConnection.isOpen());

    QSqlQuery query(activeConnection);
    QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);

    QVERIFY(query.exec(QStringLiteral("PRAGMA busy_timeout")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 5000);

    QVERIFY(query.exec(QStringLiteral("PRAGMA journal_mode")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString().toLower(), QStringLiteral("wal"));
}

void DatabaseManagerTest::reopensExistingDatabase()
{
    QVERIFY(initialize());

    TelemetryRecord record;
    record.deviceId = QStringLiteral("DEV-REOPEN");
    record.name = QStringLiteral("重复打开设备");
    record.status = TelemetryStatus::Online;
    record.temperature = 66.6;
    record.pressure = 1.33;
    record.speed = 1777.0;
    record.voltage = 219.5;
    record.updatedAt = QDateTime::currentDateTimeUtc();

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().insertTelemetryRecords({record}, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(initialize());
    QCOMPARE(DatabaseManager::instance().telemetryRecordCount(&errorMessage), qint64(1));

    DatabaseManager::instance().shutdown();
    QVERIFY2(initialize(&errorMessage), qPrintable(errorMessage));

    const QList<TelemetryRecord> records =
        DatabaseManager::instance().latestDeviceRecords(&errorMessage);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.constFirst().deviceId, QStringLiteral("DEV-REOPEN"));
}

void DatabaseManagerTest::rejectsCorruptDatabaseWithoutOverwriting()
{
    QVERIFY(QDir().mkpath(QFileInfo(m_databasePath).absolutePath()));
    const QByteArray corruptContents("this is not a sqlite database");
    {
        QFile corrupt(m_databasePath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(corrupt.write(corruptContents), qint64(corruptContents.size()));
    }

    QString errorMessage;
    QVERIFY2(!initialize(&errorMessage), "损坏数据库不应初始化成功");
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(errorMessage.contains(QStringLiteral("quick_check"), Qt::CaseInsensitive));

    QFile corrupt(m_databasePath);
    QVERIFY(corrupt.open(QIODevice::ReadOnly));
    QCOMPARE(corrupt.readAll(), corruptContents);
}

void DatabaseManagerTest::rejectsUnwritableDataDirectory()
{
    QVERIFY(QDir().mkpath(m_temporaryDirectory->path()));
    {
        QFile dataPath(m_dataDirectory);
        QVERIFY(dataPath.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(dataPath.write("file"), qint64(4));
    }

    QString errorMessage;
    QVERIFY2(!initialize(&errorMessage), "文件不能作为数据目录");
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(errorMessage.contains(QStringLiteral("不是目录")));
}

void DatabaseManagerTest::rejectsReadOnlyDatabase()
{
    QString errorMessage;
    QVERIFY2(createSqliteDatabase(
                 m_databasePath,
                 {QStringLiteral("CREATE TABLE marker (value TEXT)")},
                 &errorMessage),
             qPrintable(errorMessage));

    const QFile::Permissions originalPermissions = QFile::permissions(m_databasePath);
    QVERIFY(QFile::setPermissions(m_databasePath, QFileDevice::ReadOwner));

    const bool initialized = initialize(&errorMessage);
    QFile::setPermissions(m_databasePath, originalPermissions);

    if (initialized) {
        QSKIP("当前平台仍允许以读写方式打开只读数据库文件");
    }
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(errorMessage.contains(QStringLiteral("不可写")));
}

void DatabaseManagerTest::migratesLegacySchemaSuccessfully()
{
    QString errorMessage;
    QVERIFY2(createSqliteDatabase(
                 m_databasePath,
                 {
                     QStringLiteral(
                         "CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT, "
                         "password_hash TEXT, salt TEXT, role TEXT, created_at TEXT)"),
                     QStringLiteral(
                         "INSERT INTO users "
                         "(id, username, password_hash, salt, role, created_at) "
                         "VALUES (1, 'legacy-user', 'hash', 'salt', 'user', "
                         "'2026-01-01T00:00:00.000Z')"),
                 },
                 &errorMessage),
             qPrintable(errorMessage));

    QVERIFY2(initialize(&errorMessage), qPrintable(errorMessage));
    QCOMPARE(DatabaseManager::instance().roleForUser(QStringLiteral("legacy-user")),
             QStringLiteral("user"));
    QCOMPARE(scalarValue(m_databasePath,
                         QStringLiteral("SELECT MAX(version) FROM schema_version")).toInt(),
             2);
    QCOMPARE(scalarValue(m_databasePath,
                         QStringLiteral("SELECT COUNT(*) FROM users WHERE username='legacy-user'"))
                 .toInt(),
             1);
    QCOMPARE(backupCount(&errorMessage), 1);
}

void DatabaseManagerTest::rollsBackFailedMigration()
{
    QString errorMessage;
    QVERIFY2(createSqliteDatabase(
                 m_databasePath,
                 {
                     QStringLiteral(
                         "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, "
                         "description TEXT NOT NULL, applied_at TEXT NOT NULL)"),
                     QStringLiteral(
                         "INSERT INTO schema_version "
                         "(version, description, applied_at) VALUES (0, 'test', 'now')"),
                     QStringLiteral(
                         "CREATE TABLE idx_telemetry_device_time (value INTEGER)"),
                 },
                 &errorMessage),
             qPrintable(errorMessage));

    QVERIFY2(!initialize(&errorMessage), "迁移应因对象名冲突失败");
    QVERIFY(!errorMessage.isEmpty());
    QCOMPARE(scalarValue(m_databasePath,
                         QStringLiteral("SELECT MAX(version) FROM schema_version")).toInt(),
             0);
    QString inspectionError;
    QVERIFY(!objectExists(m_databasePath, QStringLiteral("table"), QStringLiteral("users"),
                          &inspectionError));
    QVERIFY2(inspectionError.isEmpty(), qPrintable(inspectionError));
    QVERIFY(objectExists(m_databasePath, QStringLiteral("table"),
                         QStringLiteral("idx_telemetry_device_time"), &inspectionError));
    QVERIFY2(inspectionError.isEmpty(), qPrintable(inspectionError));
    QCOMPARE(backupCount(&inspectionError), 1);
    QVERIFY2(inspectionError.isEmpty(), qPrintable(inspectionError));
}

void DatabaseManagerTest::importsLegacyDatabaseAndKeepsOriginal()
{
    QVERIFY(!m_legacyDatabase.isEmpty());
    QString errorMessage;
    QVERIFY2(createSqliteDatabase(
                 m_legacyDatabase,
                 {
                     QStringLiteral(
                         "CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT, "
                         "password_hash TEXT, salt TEXT, role TEXT, created_at TEXT)"),
                     QStringLiteral(
                         "INSERT INTO users "
                         "(id, username, password_hash, salt, role, created_at) "
                         "VALUES (1, 'old-user', 'hash', 'salt', 'user', "
                         "'2026-01-01T00:00:00.000Z')"),
                 },
                 &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(QFileInfo::exists(m_legacyDatabase));
    QVERIFY(!QFileInfo::exists(m_databasePath));

    QVERIFY2(initialize(&errorMessage), qPrintable(errorMessage));
    QVERIFY(QFileInfo::exists(m_legacyDatabase));
    QVERIFY(QFileInfo::exists(m_databasePath));
    QCOMPARE(scalarValue(m_databasePath,
                         QStringLiteral("SELECT COUNT(*) FROM users WHERE username='old-user'"))
                 .toInt(),
             1);
    QCOMPARE(scalarValue(m_legacyDatabase,
                         QStringLiteral("SELECT COUNT(*) FROM users WHERE username='old-user'"))
                 .toInt(),
             1);
    QCOMPARE(scalarValue(m_databasePath,
                         QStringLiteral("SELECT MAX(version) FROM schema_version")).toInt(),
             2);
}

void DatabaseManagerTest::rejectsNewAndLegacyDatabaseConflict()
{
    QString errorMessage;
    QVERIFY2(createSqliteDatabase(
                 m_databasePath,
                 {QStringLiteral("CREATE TABLE new_marker (value TEXT)"),
                  QStringLiteral("INSERT INTO new_marker VALUES ('new')")},
                 &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(createSqliteDatabase(
                 m_legacyDatabase,
                 {QStringLiteral("CREATE TABLE old_marker (value TEXT)"),
                  QStringLiteral("INSERT INTO old_marker VALUES ('old')")},
                 &errorMessage),
             qPrintable(errorMessage));

    QVERIFY2(!initialize(&errorMessage), "新旧数据库同时存在时必须报冲突");
    QVERIFY(errorMessage.contains(QStringLiteral("新旧数据库同时存在")));
    QCOMPARE(scalarValue(m_databasePath,
                         QStringLiteral("SELECT value FROM new_marker")).toString(),
             QStringLiteral("new"));
    QCOMPARE(scalarValue(m_legacyDatabase,
                         QStringLiteral("SELECT value FROM old_marker")).toString(),
             QStringLiteral("old"));
}

void DatabaseManagerTest::createsBackupBeforeMigration()
{
    QString errorMessage;
    QVERIFY2(createSqliteDatabase(
                 m_databasePath,
                 {QStringLiteral("CREATE TABLE app_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)")},
                 &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(backupCount(&errorMessage), 0);

    QVERIFY2(initialize(&errorMessage), qPrintable(errorMessage));
    QCOMPARE(backupCount(&errorMessage), 1);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
}

void DatabaseManagerTest::migratesAuthSchemaWithBackup()
{
    QString errorMessage;
    QVERIFY2(createSqliteDatabase(
                 m_databasePath,
                 {
                     QStringLiteral(
                         "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, "
                         "description TEXT NOT NULL, applied_at TEXT NOT NULL)"),
                     QStringLiteral(
                         "INSERT INTO schema_version "
                         "(version, description, applied_at) VALUES (1, 'baseline', 'now')"),
                     QStringLiteral(
                         "CREATE TABLE users ("
                         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                         "username TEXT NOT NULL UNIQUE,"
                         "password_hash TEXT NOT NULL,"
                         "salt TEXT NOT NULL,"
                         "role TEXT NOT NULL DEFAULT 'user',"
                         "created_at TEXT NOT NULL)"),
                     QStringLiteral(
                         "INSERT INTO users "
                         "(username, password_hash, salt, role, created_at) "
                         "VALUES ('legacy-user', 'hash', 'salt', 'operator', "
                         "'2026-09-20T10:00:00.000Z')"),
                 },
                 &errorMessage),
             qPrintable(errorMessage));

    QVERIFY2(initialize(&errorMessage), qPrintable(errorMessage));
    QCOMPARE(scalarValue(m_databasePath,
                         QStringLiteral("SELECT MAX(version) FROM schema_version")).toInt(),
             2);
    QCOMPARE(backupCount(&errorMessage), 1);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(scalarValue(
                 m_databasePath,
                 QStringLiteral("SELECT display_name FROM users WHERE username='legacy-user'"))
                 .toString(),
             QStringLiteral("legacy-user"));
    QCOMPARE(scalarValue(
                 m_databasePath,
                 QStringLiteral("SELECT password_scheme FROM users WHERE username='legacy-user'"))
                 .toString(),
             QStringLiteral("legacy_sha256"));
    QVERIFY2(objectExists(m_databasePath, QStringLiteral("table"),
                          QStringLiteral("roles"), &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(objectExists(m_databasePath, QStringLiteral("table"),
                          QStringLiteral("role_permissions"), &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(objectExists(m_databasePath, QStringLiteral("table"),
                          QStringLiteral("role_assignments"), &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(objectExists(m_databasePath, QStringLiteral("table"),
                          QStringLiteral("audit_logs"), &errorMessage),
             qPrintable(errorMessage));
}

void DatabaseManagerTest::rejectsInvalidCredentials()
{
    QVERIFY(initialize());
    QVERIFY(DatabaseManager::instance().validateUser(QStringLiteral("admin"),
                                                     QStringLiteral("123456")));
    QVERIFY(!DatabaseManager::instance().validateUser(QStringLiteral("admin"),
                                                      QStringLiteral("wrong-password")));
    QVERIFY(!DatabaseManager::instance().validateUser(QStringLiteral("missing"),
                                                      QStringLiteral("123456")));
}

void DatabaseManagerTest::rememberSessionRoundTrip()
{
    QVERIFY(initialize());

    QString token;
    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().createRememberSession(
                 QStringLiteral("admin"), &token, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(!token.isEmpty());

    QString username;
    QVERIFY2(DatabaseManager::instance().validateRememberSession(
                 token, &username, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(username, QStringLiteral("admin"));

    DatabaseManager::instance().revokeRememberSession(token);
    QVERIFY(!DatabaseManager::instance().validateRememberSession(token, &username));
}

void DatabaseManagerTest::storesTelemetryAndQueriesTimeRange()
{
    QVERIFY(initialize());

    const QDateTime base(QDate(2026, 9, 20), QTime(10, 0), QTimeZone::UTC);
    QList<TelemetryRecord> records;
    for (int i = 0; i < 3; ++i) {
        TelemetryRecord record;
        record.deviceId = i < 2 ? QStringLiteral("DEV-001") : QStringLiteral("DEV-002");
        record.name = QStringLiteral("测试设备 %1").arg(i + 1);
        record.status = TelemetryStatus::Online;
        record.temperature = 60.0 + i;
        record.pressure = 1.1 + i * 0.1;
        record.speed = 1400.0 + i * 100.0;
        record.voltage = 220.0 + i;
        record.updatedAt = base.addSecs(i * 10);
        records.append(record);
    }

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().insertTelemetryRecords(records, &errorMessage),
             qPrintable(errorMessage));

    const QList<TelemetryRecord> latest =
        DatabaseManager::instance().latestDeviceRecords(&errorMessage);
    QCOMPARE(latest.size(), 2);

    const QList<TelemetryRecord> range = DatabaseManager::instance().telemetryBetween(
        base.addSecs(-1), base.addSecs(21), &errorMessage);
    QCOMPARE(range.size(), 3);
    QCOMPARE(range.constFirst().deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(range.constFirst().updatedAt.toUTC(), base);
    QCOMPARE(range.constLast().deviceId, QStringLiteral("DEV-002"));
}

void DatabaseManagerTest::storesHeartbeatAndSystemLog()
{
    QVERIFY(initialize());

    HeartbeatRecord heartbeat;
    heartbeat.deviceId = QStringLiteral("DEV-001");
    heartbeat.heartbeatAt = QDateTime::currentDateTimeUtc();
    heartbeat.online = true;
    heartbeat.collecting = true;
    heartbeat.latencyMs = 42;

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().insertHeartbeatRecords({heartbeat}, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(DatabaseManager::instance().insertLog(
                 QStringLiteral("INFO"), QStringLiteral("test"),
                 QStringLiteral("数据库测试日志"), &errorMessage),
             qPrintable(errorMessage));
}

QTEST_MAIN(DatabaseManagerTest)

#include "DatabaseManagerTest.moc"
