#pragma once

#include "auth/AuthTypes.h"

#include <QString>

class AuditRepository;
class PasswordRepository;
class PasswordService;
class UserRepository;

class AuthenticationService
{
public:
    AuthenticationService(UserRepository &users,
                          PasswordRepository &passwords,
                          const PasswordService &passwordService,
                          AuditRepository &audit);

    bool authenticate(const QString &username,
                      const QString &password,
                      Auth::UserRecord *authenticatedUser = nullptr,
                      QString *errorMessage = nullptr);

private:
    void audit(const QString &username,
               const QString &result,
               const QString &context);

    UserRepository &m_users;
    PasswordRepository &m_passwords;
    const PasswordService &m_passwordService;
    AuditRepository &m_audit;
};
