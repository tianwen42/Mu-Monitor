#pragma once

#include "auth/AuthTypes.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

class AuditRepository
{
public:
    explicit AuditRepository(
        const QString &connectionName = QStringLiteral("mu_monitor_sqlite"));

    bool initialize(QString *errorMessage = nullptr);
    bool record(const QString &actorUsername,
                const QString &targetUsername,
                const QString &eventType,
                const QString &result,
                const QString &context = QString(),
                QString *errorMessage = nullptr);
    QList<Auth::AuditRecord> recentEntries(int limit = 100,
                                           QString *errorMessage = nullptr);

    static QString sanitizeContext(const QString &context);

private:
    QSqlDatabase database() const;

    QString m_connectionName;
    bool m_initialized = false;
};
