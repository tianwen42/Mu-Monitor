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
        "created_at TEXT NOT NULL"
        ")");

    if (!query.exec(sql)) {
        m_lastError = QStringLiteral("创建 users 表失败：%1").arg(query.lastError().text());
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
        return true;
    }

    const QString saltHex = generateSaltHex();
    const QByteArray salt = QByteArray::fromHex(saltHex.toLatin1());
    const QString hashHex = QString::fromLatin1(passwordHash(QStringLiteral("123456"), salt).toHex());

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO users (username, password_hash, salt, created_at) VALUES (?, ?, ?, ?)"));
    insert.addBindValue(QStringLiteral("admin"));
    insert.addBindValue(hashHex);
    insert.addBindValue(saltHex);
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
