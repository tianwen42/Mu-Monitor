#include "database/PasswordRepository.h"

#include "utils/TimeUtils.h"

#include <QSqlError>
#include <QSqlQuery>

PasswordRepository::PasswordRepository(const QString &connectionName)
    : m_connectionName(connectionName)
{
}

QSqlDatabase PasswordRepository::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool PasswordRepository::initialize(QString *errorMessage)
{
    if (m_initialized) {
        return database().isOpen();
    }

    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorMessage) *errorMessage = QStringLiteral("密码仓储无法访问已打开的 SQLite 连接");
        return false;
    }

    QSqlQuery columns(db);
    if (!columns.exec(QStringLiteral(
            "SELECT password_hash, salt, password_scheme, password_iterations "
            "FROM users LIMIT 0"))) {
        if (errorMessage) *errorMessage = QStringLiteral("密码字段尚未迁移：%1").arg(columns.lastError().text());
        return false;
    }

    m_initialized = true;
    return true;
}

bool PasswordRepository::credentialForUser(qint64 userId,
                                           Auth::PasswordCredential *credential,
                                           QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT password_hash, salt, password_scheme, password_iterations "
        "FROM users WHERE id = ?"));
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        if (errorMessage) *errorMessage = QStringLiteral("未找到用户密码凭据");
        return false;
    }

    Auth::PasswordCredential value;
    value.hash = query.value(0).toString().trimmed().toLower();
    value.salt = query.value(1).toString().trimmed().toLower();
    value.scheme = query.value(2).toString().trimmed().toLower();
    value.iterations = query.value(3).toInt();
    if (!value.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("用户密码凭据不完整");
        return false;
    }

    if (credential) *credential = value;
    return true;
}

bool PasswordRepository::updateCredential(qint64 userId,
                                          const Auth::PasswordCredential &credential,
                                          QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }
    if (!credential.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("新密码凭据无效");
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "UPDATE users SET password_hash = ?, salt = ?, password_scheme = ?, "
        "password_iterations = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(credential.hash);
    query.addBindValue(credential.salt);
    query.addBindValue(credential.scheme);
    query.addBindValue(credential.iterations);
    query.addBindValue(TimeUtils::toUtcIso8601());
    query.addBindValue(userId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        if (errorMessage) *errorMessage = QStringLiteral("更新用户密码失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}
