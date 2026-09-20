#include "database/DatabaseManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {
constexpr int kPasswordIterations = 100000;
constexpr int kPasswordKeyLength = 32;
const char *kConnectionName = "mu_monitor_sqlite";
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
    shutdown();
}

bool DatabaseManager::initialize(QString *errorMessage)
{
    if (m_initialized) {
        return true;
    }

    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDirectory.isEmpty()) {
        m_lastError = QStringLiteral("无法获取应用数据目录");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    if (!QDir().mkpath(dataDirectory)) {
        m_lastError = QStringLiteral("无法创建应用数据目录：%1").arg(dataDirectory);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    m_databasePath = QDir(dataDirectory).filePath(QStringLiteral("mu-monitor.db"));

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        m_lastError = QStringLiteral("未找到 Qt SQLite 驱动 QSQLITE");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databasePath);

    if (!m_database.open()) {
        m_lastError = QStringLiteral("无法打开数据库：%1").arg(m_database.lastError().text());
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    if (!createTables(errorMessage)) {
        return false;
    }
    if (!ensureDefaultUser(errorMessage)) {
        return false;
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
    insert.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));

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
    insert.addBindValue(expiresAt.toString(Qt::ISODate));
    insert.addBindValue(now.toString(Qt::ISODate));
    insert.addBindValue(now.toString(Qt::ISODate));

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
    const QDateTime expiresAt = QDateTime::fromString(query.value(1).toString(), Qt::ISODate);
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
    update.addBindValue(now.toString(Qt::ISODate));
    update.addBindValue(now.addDays(30).toString(Qt::ISODate));
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
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.exec();
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
