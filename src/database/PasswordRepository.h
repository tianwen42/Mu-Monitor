#pragma once

#include "auth/AuthTypes.h"

#include <QSqlDatabase>
#include <QString>

class PasswordRepository
{
public:
    explicit PasswordRepository(
        const QString &connectionName = QStringLiteral("mu_monitor_sqlite"));

    bool initialize(QString *errorMessage = nullptr);
    bool credentialForUser(qint64 userId, Auth::PasswordCredential *credential,
                           QString *errorMessage = nullptr);
    bool updateCredential(qint64 userId,
                          const Auth::PasswordCredential &credential,
                          QString *errorMessage = nullptr);

private:
    QSqlDatabase database() const;

    QString m_connectionName;
    bool m_initialized = false;
};
