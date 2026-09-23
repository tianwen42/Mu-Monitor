#include "auth/AuditRepository.h"

#include "utils/TimeUtils.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

AuditRepository::AuditRepository(const QString &connectionName)
    : m_connectionName(connectionName)
{
}

QSqlDatabase AuditRepository::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool AuditRepository::initialize(QString *errorMessage)
{
    if (m_initialized) {
        return database().isOpen();
    }

    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorMessage) *errorMessage = QStringLiteral("审计仓储无法访问已打开的 SQLite 连接");
        return false;
    }

    QSqlQuery query(db);
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS audit_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "occurred_at TEXT NOT NULL,"
        "actor_username TEXT NOT NULL,"
        "target_username TEXT NOT NULL,"
        "event_type TEXT NOT NULL,"
        "result TEXT NOT NULL,"
        "context TEXT NOT NULL DEFAULT ''"
        ")");
    if (!query.exec(sql)) {
        if (errorMessage) *errorMessage = QStringLiteral("创建 audit_logs 表失败：%1").arg(query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_audit_logs_time ON audit_logs(occurred_at)"))) {
        if (errorMessage) *errorMessage = QStringLiteral("创建审计索引失败：%1").arg(query.lastError().text());
        return false;
    }

    m_initialized = true;
    return true;
}

bool AuditRepository::record(const QString &actorUsername,
                             const QString &targetUsername,
                             const QString &eventType,
                             const QString &result,
                             const QString &context,
                             QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO audit_logs "
        "(occurred_at, actor_username, target_username, event_type, result, context) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(TimeUtils::toUtcIso8601());
    query.addBindValue(actorUsername.trimmed());
    query.addBindValue(targetUsername.trimmed());
    query.addBindValue(eventType.trimmed());
    query.addBindValue(result.trimmed());
    query.addBindValue(sanitizeContext(context));
    if (!query.exec()) {
        if (errorMessage) *errorMessage = QStringLiteral("写入审计日志失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

QList<Auth::AuditRecord> AuditRepository::recentEntries(int limit,
                                                       QString *errorMessage)
{
    QList<Auth::AuditRecord> records;
    if (!initialize(errorMessage)) {
        return records;
    }

    if (limit <= 0) {
        limit = 100;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT id, occurred_at, actor_username, target_username, event_type, "
        "result, context FROM audit_logs ORDER BY id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        if (errorMessage) *errorMessage = QStringLiteral("读取审计日志失败：%1").arg(query.lastError().text());
        return records;
    }

    while (query.next()) {
        Auth::AuditRecord record;
        record.id = query.value(0).toLongLong();
        record.occurredAt = TimeUtils::fromIso8601(query.value(1).toString());
        record.actorUsername = query.value(2).toString();
        record.targetUsername = query.value(3).toString();
        record.eventType = query.value(4).toString();
        record.result = query.value(5).toString();
        record.context = query.value(6).toString();
        records.append(record);
    }
    return records;
}

QString AuditRepository::sanitizeContext(const QString &context)
{
    QString sanitized = context.left(1024);
    static const QRegularExpression sensitiveValue(
        QStringLiteral("(?i)(password|passwd|pwd|token|password_hash|salt)\\s*[:=]\\s*([^,;\\s]+)"));
    sanitized.replace(sensitiveValue, QStringLiteral("\\1=[REDACTED]"));
    return sanitized;
}
