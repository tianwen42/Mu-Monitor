#pragma once

#include "auth/AuthTypes.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

class QSqlQuery;

class UserRepository
{
public:
    explicit UserRepository(
        const QString &connectionName = QStringLiteral("mu_monitor_sqlite"));

    bool initialize(QString *errorMessage = nullptr);
    QList<Auth::UserRecord> listUsers(QString *errorMessage = nullptr);
    bool findById(qint64 userId, Auth::UserRecord *user,
                  QString *errorMessage = nullptr);
    bool findByUsername(const QString &username, Auth::UserRecord *user,
                        QString *errorMessage = nullptr);
    bool usernameExists(const QString &username, qint64 excludeUserId = -1,
                        QString *errorMessage = nullptr);

    bool createUser(const Auth::UserRecord &user,
                    const Auth::PasswordCredential &credential,
                    const QString &assignedBy,
                    qint64 *createdUserId,
                    QString *errorMessage = nullptr);
    bool updateUser(qint64 userId,
                    const QString &username,
                    const QString &displayName,
                    const QString &role,
                    bool enabled,
                    const QString &assignedBy,
                    QString *errorMessage = nullptr);
    bool setEnabled(qint64 userId, bool enabled, QString *errorMessage = nullptr);
    bool setRole(qint64 userId, const QString &role, const QString &assignedBy,
                 QString *errorMessage = nullptr);

    QString roleForUsername(const QString &username,
                            QString *errorMessage = nullptr);
    qint64 enabledAdminCount(QString *errorMessage = nullptr);

    bool revokeAllSessions(const QString &username,
                           QString *errorMessage = nullptr);
    bool revokeOtherSessions(const QString &username,
                             const QString &preserveRawToken,
                             QString *errorMessage = nullptr);

private:
    QSqlDatabase database() const;
    bool ensureSchema(QString *errorMessage);
    bool ensureColumn(const QString &table, const QString &column,
                      const QString &definition, QString *errorMessage);
    bool seedRoles(QString *errorMessage);
    bool seedRoleAssignments(QString *errorMessage);
    Auth::UserRecord userFromQuery(const QSqlQuery &query) const;

    QString m_connectionName;
    bool m_initialized = false;
};
