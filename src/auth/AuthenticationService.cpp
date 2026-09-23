#include "auth/AuthenticationService.h"

#include "auth/AuditRepository.h"
#include "auth/PasswordService.h"
#include "database/PasswordRepository.h"
#include "database/UserRepository.h"

AuthenticationService::AuthenticationService(UserRepository &users,
                                             PasswordRepository &passwords,
                                             const PasswordService &passwordService,
                                             AuditRepository &audit)
    : m_users(users)
    , m_passwords(passwords)
    , m_passwordService(passwordService)
    , m_audit(audit)
{
}

bool AuthenticationService::authenticate(const QString &username,
                                         const QString &password,
                                         Auth::UserRecord *authenticatedUser,
                                         QString *errorMessage)
{
    const QString name = username.trimmed();
    if (name.isEmpty() || password.isEmpty()) {
        audit(name, QStringLiteral("failure"), QStringLiteral("reason=empty_credentials"));
        if (errorMessage) *errorMessage = QStringLiteral("请输入用户名和密码");
        return false;
    }

    Auth::UserRecord user;
    QString lookupError;
    if (!m_users.findByUsername(name, &user, &lookupError)) {
        audit(name, QStringLiteral("failure"), QStringLiteral("reason=unknown_user"));
        if (errorMessage) *errorMessage = QStringLiteral("用户名或密码错误");
        return false;
    }

    if (!user.enabled) {
        audit(name, QStringLiteral("failure"), QStringLiteral("reason=disabled"));
        if (errorMessage) *errorMessage = QStringLiteral("账号已禁用，请联系管理员");
        return false;
    }

    Auth::PasswordCredential credential;
    if (!m_passwords.credentialForUser(user.id, &credential, &lookupError)) {
        audit(name, QStringLiteral("failure"), QStringLiteral("reason=credential_error"));
        if (errorMessage) *errorMessage = QStringLiteral("用户名或密码错误");
        return false;
    }

    bool needsUpgrade = false;
    if (!m_passwordService.verifyPassword(password, credential, &needsUpgrade)) {
        audit(name, QStringLiteral("failure"), QStringLiteral("reason=invalid_credentials"));
        if (errorMessage) *errorMessage = QStringLiteral("用户名或密码错误");
        return false;
    }

    bool upgraded = false;
    if (needsUpgrade) {
        const Auth::PasswordCredential replacement =
            m_passwordService.createCredential(password);
        QString updateError;
        upgraded = replacement.isValid()
            && m_passwords.updateCredential(user.id, replacement, &updateError);
    }

    audit(name, QStringLiteral("success"),
          QStringLiteral("password_scheme=pbkdf2_sha256;password_upgraded=%1")
              .arg(upgraded ? QStringLiteral("true") : QStringLiteral("false")));
    if (authenticatedUser) *authenticatedUser = user;
    return true;
}

void AuthenticationService::audit(const QString &username,
                                  const QString &result,
                                  const QString &context)
{
    m_audit.record(username, username,
                   result == QStringLiteral("success")
                       ? QStringLiteral("login_success")
                       : QStringLiteral("login_failure"),
                   result, context);
}
