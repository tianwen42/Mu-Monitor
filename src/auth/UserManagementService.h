#pragma once

#include "auth/AuthTypes.h"

#include <QList>
#include <QString>

class AuditRepository;
class PasswordRepository;
class PasswordService;
class UserRepository;

class UserManagementService
{
public:
    UserManagementService(UserRepository &users,
                          PasswordRepository &passwords,
                          const PasswordService &passwordService,
                          AuditRepository &audit,
                          const QString &actorUsername);

    QString actorUsername() const;
    bool can(Auth::Permission permission) const;

    bool listUsers(QList<Auth::UserRecord> *users,
                   QString *errorMessage = nullptr);

    bool createUser(const QString &username,
                    const QString &displayName,
                    const QString &role,
                    const QString &password,
                    bool enabled,
                    qint64 *createdUserId = nullptr,
                    QString *errorMessage = nullptr);

    bool updateUser(qint64 userId,
                    const QString &username,
                    const QString &displayName,
                    const QString &role,
                    bool enabled,
                    QString *errorMessage = nullptr);

    bool setUserEnabled(qint64 userId, bool enabled,
                        QString *errorMessage = nullptr);
    bool setUserRole(qint64 userId, const QString &role,
                     QString *errorMessage = nullptr);

    bool resetPassword(qint64 userId,
                       const QString &newPassword,
                       QString *errorMessage = nullptr);

    bool changeOwnPassword(const QString &oldPassword,
                           const QString &newPassword,
                           const QString &preserveRawToken = QString(),
                           QString *errorMessage = nullptr);

private:
    bool requirePermission(Auth::Permission permission,
                           const QString &eventType,
                           const QString &targetUsername,
                           QString *errorMessage);
    bool loadTarget(qint64 userId, Auth::UserRecord *user,
                    const QString &eventType,
                    QString *errorMessage);
    bool validateRole(const QString &role,
                      const QString &eventType,
                      const QString &targetUsername,
                      QString *errorMessage);
    bool validateNewPassword(const QString &password,
                             const QString &eventType,
                             const QString &targetUsername,
                             QString *errorMessage);
    void audit(const QString &targetUsername,
               const QString &eventType,
               const QString &result,
               const QString &context = QString());

    UserRepository &m_users;
    PasswordRepository &m_passwords;
    const PasswordService &m_passwordService;
    AuditRepository &m_audit;
    QString m_actorUsername;
};
