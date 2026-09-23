#pragma once

#include "auth/AuthTypes.h"

#include <QString>

class PasswordService
{
public:
    Auth::PasswordCredential createCredential(
        const QString &password, QString *errorMessage = nullptr) const;

    bool verifyPassword(const QString &password,
                        const Auth::PasswordCredential &credential,
                        bool *needsUpgrade,
                        QString *errorMessage = nullptr) const;

private:
    QByteArray randomSalt() const;
    QByteArray legacyHash(const QString &password, const QByteArray &salt,
                          int iterations) const;
    QByteArray pbkdf2HmacSha256(const QString &password,
                                const QByteArray &salt,
                                int iterations,
                                int keyLength) const;
    QByteArray hmacSha256(const QByteArray &key, const QByteArray &data) const;
    bool constantTimeEquals(const QByteArray &left,
                            const QByteArray &right) const;
};
