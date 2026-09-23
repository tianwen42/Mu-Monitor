#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace Auth {

constexpr int kPbkdf2Iterations = 120000;
constexpr int kPbkdf2KeyLength = 32;
constexpr int kLegacyIterations = 100000;
constexpr int kLegacyKeyLength = 32;

enum class Permission {
    ManageUsers = 1 << 0,
    AcknowledgeAlarm = 1 << 1,
    ModifySettings = 1 << 2,
    ViewHistory = 1 << 3,
    ControlCollection = 1 << 4,
};

struct UserRecord {
    qint64 id = -1;
    QString username;
    QString displayName;
    QString role;
    bool enabled = true;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct PasswordCredential {
    QString hash;
    QString salt;
    QString scheme;
    int iterations = 0;

    bool isValid() const;
};

struct AuditRecord {
    qint64 id = -1;
    QDateTime occurredAt;
    QString actorUsername;
    QString targetUsername;
    QString eventType;
    QString result;
    QString context;
};

QString permissionCode(Permission permission);
QString permissionDisplayName(Permission permission);
QStringList permissionsForRole(const QString &roleCode);
bool hasPermission(const QString &roleCode, Permission permission);
bool isValidRole(const QString &roleCode);
QString roleDisplayName(const QString &roleCode);
QStringList builtInRoleCodes();

bool validateUsername(const QString &username, QString *errorMessage = nullptr);
bool validatePasswordPolicy(const QString &password, QString *errorMessage = nullptr);

} // namespace Auth
