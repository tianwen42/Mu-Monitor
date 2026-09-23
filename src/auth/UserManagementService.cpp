#include "auth/UserManagementService.h"

#include "auth/AuditRepository.h"
#include "auth/PasswordService.h"
#include "database/PasswordRepository.h"
#include "database/UserRepository.h"

UserManagementService::UserManagementService(
    UserRepository &users,
    PasswordRepository &passwords,
    const PasswordService &passwordService,
    AuditRepository &audit,
    const QString &actorUsername)
    : m_users(users)
    , m_passwords(passwords)
    , m_passwordService(passwordService)
    , m_audit(audit)
    , m_actorUsername(actorUsername.trimmed())
{
}

QString UserManagementService::actorUsername() const
{
    return m_actorUsername;
}

bool UserManagementService::can(Auth::Permission permission) const
{
    QString errorMessage;
    const QString role = m_users.roleForUsername(m_actorUsername, &errorMessage);
    return !role.isEmpty() && Auth::hasPermission(role, permission);
}

bool UserManagementService::listUsers(QList<Auth::UserRecord> *users,
                                      QString *errorMessage)
{
    if (!requirePermission(Auth::Permission::ManageUsers,
                           QStringLiteral("user_list"),
                           QString(), errorMessage)) {
        return false;
    }

    const QList<Auth::UserRecord> values = m_users.listUsers(errorMessage);
    if (errorMessage && !errorMessage->isEmpty()) {
        return false;
    }
    if (users) *users = values;
    return true;
}

bool UserManagementService::createUser(const QString &username,
                                       const QString &displayName,
                                       const QString &role,
                                       const QString &password,
                                       bool enabled,
                                       qint64 *createdUserId,
                                       QString *errorMessage)
{
    const QString normalizedUsername = username.trimmed();
    if (!requirePermission(Auth::Permission::ManageUsers,
                           QStringLiteral("user_create"),
                           normalizedUsername, errorMessage)) {
        return false;
    }

    if (!Auth::validateUsername(username, errorMessage)
        || !validateRole(role, QStringLiteral("user_create"),
                         normalizedUsername, errorMessage)
        || !validateNewPassword(password, QStringLiteral("user_create"),
                                normalizedUsername, errorMessage)) {
        return false;
    }

    QString uniqueError;
    if (m_users.usernameExists(normalizedUsername, -1, &uniqueError)) {
        if (!uniqueError.isEmpty()) {
            if (errorMessage) *errorMessage = uniqueError;
        } else if (errorMessage) {
            *errorMessage = QStringLiteral("用户名已存在");
        }
        audit(normalizedUsername, QStringLiteral("user_create"),
              QStringLiteral("failure"), QStringLiteral("reason=duplicate_username"));
        return false;
    }

    Auth::UserRecord user;
    user.username = normalizedUsername;
    user.displayName = displayName.trimmed().isEmpty()
        ? normalizedUsername
        : displayName.trimmed();
    user.role = role.trimmed().toLower();
    user.enabled = enabled;

    const Auth::PasswordCredential credential =
        m_passwordService.createCredential(password);
    if (!credential.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("创建密码凭据失败");
        audit(normalizedUsername, QStringLiteral("user_create"),
              QStringLiteral("failure"), QStringLiteral("reason=credential_error"));
        return false;
    }

    if (!m_users.createUser(user, credential, m_actorUsername,
                            createdUserId, errorMessage)) {
        audit(normalizedUsername, QStringLiteral("user_create"),
              QStringLiteral("failure"), QStringLiteral("reason=repository_error"));
        return false;
    }

    audit(normalizedUsername, QStringLiteral("user_create"),
          QStringLiteral("success"),
          QStringLiteral("role=%1;enabled=%2")
              .arg(user.role, enabled ? QStringLiteral("true") : QStringLiteral("false")));
    return true;
}

bool UserManagementService::updateUser(qint64 userId,
                                       const QString &username,
                                       const QString &displayName,
                                       const QString &role,
                                       bool enabled,
                                       QString *errorMessage)
{
    Auth::UserRecord target;
    if (!requirePermission(Auth::Permission::ManageUsers,
                           QStringLiteral("user_edit"), username.trimmed(), errorMessage)
        || !loadTarget(userId, &target, QStringLiteral("user_edit"), errorMessage)) {
        return false;
    }

    if (!Auth::validateUsername(username, errorMessage)
        || !validateRole(role, QStringLiteral("user_edit"),
                         target.username, errorMessage)) {
        return false;
    }

    const QString normalizedUsername = username.trimmed();
    const QString normalizedRole = role.trimmed().toLower();
    QString uniqueError;
    if (m_users.usernameExists(normalizedUsername, userId, &uniqueError)) {
        if (!uniqueError.isEmpty()) {
            if (errorMessage) *errorMessage = uniqueError;
        } else if (errorMessage) {
            *errorMessage = QStringLiteral("用户名已存在");
        }
        audit(target.username, QStringLiteral("user_edit"),
              QStringLiteral("failure"), QStringLiteral("reason=duplicate_username"));
        return false;
    }

    Auth::UserRecord actor;
    const bool editingSelf = m_users.findByUsername(
        m_actorUsername, &actor)
        && actor.id == target.id;
    if (editingSelf && target.enabled && !enabled) {
        if (errorMessage) *errorMessage = QStringLiteral("当前用户不能禁用自己");
        audit(target.username, QStringLiteral("user_edit"),
              QStringLiteral("failure"), QStringLiteral("reason=self_disable"));
        return false;
    }

    const bool removesActiveAdmin = target.enabled
        && target.role == QStringLiteral("admin")
        && (!enabled || normalizedRole != QStringLiteral("admin"));
    if (removesActiveAdmin) {
        const qint64 count = m_users.enabledAdminCount(errorMessage);
        if (count < 0) {
            return false;
        }
        if (count <= 1) {
            if (errorMessage) *errorMessage = QStringLiteral("不能禁用或降权最后一个启用的管理员");
            audit(target.username, QStringLiteral("user_edit"),
                  QStringLiteral("failure"), QStringLiteral("reason=last_admin"));
            return false;
        }
    }

    if (target.enabled && !enabled) {
        if (!m_users.revokeAllSessions(target.username, errorMessage)) {
            audit(target.username, QStringLiteral("user_disable"),
                  QStringLiteral("failure"), QStringLiteral("reason=session_revoke_error"));
            return false;
        }
    }

    if (!m_users.updateUser(userId, normalizedUsername,
                            displayName.trimmed().isEmpty()
                                ? normalizedUsername
                                : displayName.trimmed(),
                            normalizedRole, enabled, m_actorUsername,
                            errorMessage)) {
        audit(target.username, QStringLiteral("user_edit"),
              QStringLiteral("failure"), QStringLiteral("reason=repository_error"));
        return false;
    }

    audit(target.username, QStringLiteral("user_edit"),
          QStringLiteral("success"),
          QStringLiteral("username=%1;role=%2;enabled=%3")
              .arg(normalizedUsername, normalizedRole,
                   enabled ? QStringLiteral("true") : QStringLiteral("false")));

    if (target.role != normalizedRole) {
        audit(target.username, QStringLiteral("role_change"),
              QStringLiteral("success"),
              QStringLiteral("from=%1;to=%2").arg(target.role, normalizedRole));
    }
    if (target.enabled != enabled) {
        audit(target.username,
              enabled ? QStringLiteral("user_enable") : QStringLiteral("user_disable"),
              QStringLiteral("success"),
              QStringLiteral("username=%1").arg(normalizedUsername));
    }
    return true;
}

bool UserManagementService::setUserEnabled(qint64 userId, bool enabled,
                                           QString *errorMessage)
{
    Auth::UserRecord target;
    if (!requirePermission(Auth::Permission::ManageUsers,
                           enabled ? QStringLiteral("user_enable")
                                   : QStringLiteral("user_disable"),
                           QString(), errorMessage)
        || !loadTarget(userId, &target,
                       enabled ? QStringLiteral("user_enable")
                               : QStringLiteral("user_disable"),
                       errorMessage)) {
        return false;
    }

    if (target.enabled == enabled) {
        return true;
    }

    Auth::UserRecord actor;
    if (m_users.findByUsername(m_actorUsername, &actor) && actor.id == target.id
        && !enabled) {
        if (errorMessage) *errorMessage = QStringLiteral("当前用户不能禁用自己");
        audit(target.username, QStringLiteral("user_disable"),
              QStringLiteral("failure"), QStringLiteral("reason=self_disable"));
        return false;
    }

    if (!enabled && target.role == QStringLiteral("admin")) {
        const qint64 count = m_users.enabledAdminCount(errorMessage);
        if (count < 0) {
            return false;
        }
        if (count <= 1) {
            if (errorMessage) *errorMessage = QStringLiteral("不能禁用或降权最后一个启用的管理员");
            audit(target.username, QStringLiteral("user_disable"),
                  QStringLiteral("failure"), QStringLiteral("reason=last_admin"));
            return false;
        }
    }

    if (target.enabled && !enabled) {
        if (!m_users.revokeAllSessions(target.username, errorMessage)) {
            audit(target.username, QStringLiteral("user_disable"),
                  QStringLiteral("failure"), QStringLiteral("reason=session_revoke_error"));
            return false;
        }
    }

    if (!m_users.setEnabled(userId, enabled, errorMessage)) {
        audit(target.username,
              enabled ? QStringLiteral("user_enable") : QStringLiteral("user_disable"),
              QStringLiteral("failure"), QStringLiteral("reason=repository_error"));
        return false;
    }

    audit(target.username,
          enabled ? QStringLiteral("user_enable") : QStringLiteral("user_disable"),
          QStringLiteral("success"),
          QStringLiteral("username=%1").arg(target.username));
    return true;
}

bool UserManagementService::setUserRole(qint64 userId, const QString &role,
                                        QString *errorMessage)
{
    Auth::UserRecord target;
    if (!requirePermission(Auth::Permission::ManageUsers,
                           QStringLiteral("role_change"), QString(), errorMessage)
        || !loadTarget(userId, &target, QStringLiteral("role_change"), errorMessage)) {
        return false;
    }

    const QString normalizedRole = role.trimmed().toLower();
    if (!validateRole(normalizedRole, QStringLiteral("role_change"),
                      target.username, errorMessage)) {
        return false;
    }
    if (target.role == normalizedRole) {
        return true;
    }

    if (target.enabled && target.role == QStringLiteral("admin")
        && normalizedRole != QStringLiteral("admin")) {
        const qint64 count = m_users.enabledAdminCount(errorMessage);
        if (count < 0) {
            return false;
        }
        if (count <= 1) {
            if (errorMessage) *errorMessage = QStringLiteral("不能禁用或降权最后一个启用的管理员");
            audit(target.username, QStringLiteral("role_change"),
                  QStringLiteral("failure"), QStringLiteral("reason=last_admin"));
            return false;
        }
    }

    if (!m_users.setRole(userId, normalizedRole, m_actorUsername, errorMessage)) {
        audit(target.username, QStringLiteral("role_change"),
              QStringLiteral("failure"), QStringLiteral("reason=repository_error"));
        return false;
    }

    audit(target.username, QStringLiteral("role_change"),
          QStringLiteral("success"),
          QStringLiteral("from=%1;to=%2").arg(target.role, normalizedRole));
    return true;
}

bool UserManagementService::resetPassword(qint64 userId,
                                          const QString &newPassword,
                                          QString *errorMessage)
{
    Auth::UserRecord target;
    if (!requirePermission(Auth::Permission::ManageUsers,
                           QStringLiteral("password_reset"), QString(), errorMessage)
        || !loadTarget(userId, &target, QStringLiteral("password_reset"), errorMessage)) {
        return false;
    }

    if (!validateNewPassword(newPassword, QStringLiteral("password_reset"),
                             target.username, errorMessage)) {
        return false;
    }

    const Auth::PasswordCredential credential =
        m_passwordService.createCredential(newPassword);
    if (!credential.isValid()
        || !m_passwords.updateCredential(target.id, credential, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("重置密码失败");
        }
        audit(target.username, QStringLiteral("password_reset"),
              QStringLiteral("failure"), QStringLiteral("reason=credential_error"));
        return false;
    }

    if (!m_users.revokeAllSessions(target.username, errorMessage)) {
        audit(target.username, QStringLiteral("password_reset"),
              QStringLiteral("failure"), QStringLiteral("reason=session_revoke_error"));
        return false;
    }

    audit(target.username, QStringLiteral("password_reset"),
          QStringLiteral("success"),
          QStringLiteral("password_scheme=pbkdf2_sha256;sessions_revoked=true"));
    return true;
}

bool UserManagementService::changeOwnPassword(const QString &oldPassword,
                                              const QString &newPassword,
                                              const QString &preserveRawToken,
                                              QString *errorMessage)
{
    Auth::UserRecord actor;
    if (!m_users.findByUsername(m_actorUsername, &actor, errorMessage)) {
        return false;
    }

    Auth::PasswordCredential current;
    if (!m_passwords.credentialForUser(actor.id, &current, errorMessage)) {
        return false;
    }

    bool needsUpgrade = false;
    if (!m_passwordService.verifyPassword(oldPassword, current, &needsUpgrade)) {
        if (errorMessage) *errorMessage = QStringLiteral("旧密码不正确");
        audit(actor.username, QStringLiteral("password_change"),
              QStringLiteral("failure"), QStringLiteral("reason=old_password_invalid"));
        return false;
    }
    if (oldPassword == newPassword) {
        if (errorMessage) *errorMessage = QStringLiteral("新密码不能与旧密码相同");
        audit(actor.username, QStringLiteral("password_change"),
              QStringLiteral("failure"), QStringLiteral("reason=same_password"));
        return false;
    }
    if (!validateNewPassword(newPassword, QStringLiteral("password_change"),
                             actor.username, errorMessage)) {
        return false;
    }

    const Auth::PasswordCredential replacement =
        m_passwordService.createCredential(newPassword);
    if (!replacement.isValid()
        || !m_passwords.updateCredential(actor.id, replacement, errorMessage)) {
        audit(actor.username, QStringLiteral("password_change"),
              QStringLiteral("failure"), QStringLiteral("reason=credential_error"));
        return false;
    }

    if (!m_users.revokeOtherSessions(actor.username, preserveRawToken, errorMessage)) {
        audit(actor.username, QStringLiteral("password_change"),
              QStringLiteral("failure"), QStringLiteral("reason=session_revoke_error"));
        return false;
    }

    audit(actor.username, QStringLiteral("password_change"),
          QStringLiteral("success"),
          QStringLiteral("password_scheme=pbkdf2_sha256;other_sessions_revoked=true"));
    return true;
}

bool UserManagementService::requirePermission(Auth::Permission permission,
                                              const QString &eventType,
                                              const QString &targetUsername,
                                              QString *errorMessage)
{
    if (can(permission)) {
        return true;
    }

    if (errorMessage) *errorMessage = QStringLiteral("当前账号没有执行该操作的权限");
    audit(targetUsername, eventType, QStringLiteral("failure"),
          QStringLiteral("reason=permission_denied"));
    return false;
}

bool UserManagementService::loadTarget(qint64 userId,
                                       Auth::UserRecord *user,
                                       const QString &eventType,
                                       QString *errorMessage)
{
    if (m_users.findById(userId, user, errorMessage)) {
        return true;
    }

    audit(QString(), eventType, QStringLiteral("failure"),
          QStringLiteral("reason=target_not_found"));
    return false;
}

bool UserManagementService::validateRole(const QString &role,
                                         const QString &eventType,
                                         const QString &targetUsername,
                                         QString *errorMessage)
{
    if (Auth::isValidRole(role)) {
        return true;
    }

    if (errorMessage) *errorMessage = QStringLiteral("不支持的角色");
    audit(targetUsername, eventType, QStringLiteral("failure"),
          QStringLiteral("reason=invalid_role"));
    return false;
}

bool UserManagementService::validateNewPassword(const QString &password,
                                                const QString &eventType,
                                                const QString &targetUsername,
                                                QString *errorMessage)
{
    if (Auth::validatePasswordPolicy(password, errorMessage)) {
        return true;
    }

    audit(targetUsername, eventType, QStringLiteral("failure"),
          QStringLiteral("reason=password_policy"));
    return false;
}

void UserManagementService::audit(const QString &targetUsername,
                                  const QString &eventType,
                                  const QString &result,
                                  const QString &context)
{
    m_audit.record(m_actorUsername, targetUsername, eventType, result, context);
}
