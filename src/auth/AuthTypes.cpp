#include "auth/AuthTypes.h"

#include <QRegularExpression>

namespace Auth {
namespace {

const QString kAdminRole = QStringLiteral("admin");
const QString kOperatorRole = QStringLiteral("operator");
const QString kViewerRole = QStringLiteral("viewer");

const QStringList kBuiltInRoles = {
    kAdminRole,
    kOperatorRole,
    kViewerRole,
};

bool isBuiltInRole(const QString &roleCode)
{
    return kBuiltInRoles.contains(roleCode.trimmed().toLower());
}

} // namespace

bool PasswordCredential::isValid() const
{
    return !hash.trimmed().isEmpty()
        && !salt.trimmed().isEmpty()
        && !scheme.trimmed().isEmpty()
        && iterations > 0;
}

QString permissionCode(Permission permission)
{
    switch (permission) {
    case Permission::ManageUsers:
        return QStringLiteral("manage_users");
    case Permission::AcknowledgeAlarm:
        return QStringLiteral("acknowledge_alarm");
    case Permission::ModifySettings:
        return QStringLiteral("modify_settings");
    case Permission::ViewHistory:
        return QStringLiteral("view_history");
    case Permission::ControlCollection:
        return QStringLiteral("control_collection");
    }
    return {};
}

QString permissionDisplayName(Permission permission)
{
    switch (permission) {
    case Permission::ManageUsers:
        return QStringLiteral("用户管理");
    case Permission::AcknowledgeAlarm:
        return QStringLiteral("确认告警");
    case Permission::ModifySettings:
        return QStringLiteral("修改设置");
    case Permission::ViewHistory:
        return QStringLiteral("查看历史");
    case Permission::ControlCollection:
        return QStringLiteral("控制采集");
    }
    return {};
}

QStringList permissionsForRole(const QString &roleCode)
{
    const QString role = roleCode.trimmed().toLower();
    if (role == kAdminRole) {
        return {
            permissionCode(Permission::ManageUsers),
            permissionCode(Permission::AcknowledgeAlarm),
            permissionCode(Permission::ModifySettings),
            permissionCode(Permission::ViewHistory),
            permissionCode(Permission::ControlCollection),
        };
    }
    if (role == kOperatorRole) {
        return {
            permissionCode(Permission::AcknowledgeAlarm),
            permissionCode(Permission::ViewHistory),
            permissionCode(Permission::ControlCollection),
        };
    }
    if (role == kViewerRole) {
        return {
            permissionCode(Permission::ViewHistory),
        };
    }
    return {};
}

bool hasPermission(const QString &roleCode, Permission permission)
{
    return permissionsForRole(roleCode).contains(permissionCode(permission));
}

bool isValidRole(const QString &roleCode)
{
    return isBuiltInRole(roleCode);
}

QString roleDisplayName(const QString &roleCode)
{
    const QString role = roleCode.trimmed().toLower();
    if (role == kAdminRole) {
        return QStringLiteral("管理员");
    }
    if (role == kOperatorRole) {
        return QStringLiteral("操作员");
    }
    if (role == kViewerRole) {
        return QStringLiteral("只读用户");
    }
    return QStringLiteral("未知角色");
}

QStringList builtInRoleCodes()
{
    return kBuiltInRoles;
}

bool validateUsername(const QString &username, QString *errorMessage)
{
    const QString normalized = username.trimmed();
    static const QRegularExpression allowed(
        QStringLiteral("^[\\p{L}\\p{N}_.-]{3,64}$"));

    if (normalized != username || !allowed.match(normalized).hasMatch()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("用户名需为 3-64 个字符，仅允许字母、数字、下划线、点和连字符");
        }
        return false;
    }
    return true;
}

bool validatePasswordPolicy(const QString &password, QString *errorMessage)
{
    if (password.size() < 8 || password.size() > 128) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("密码长度需为 8-128 个字符");
        }
        return false;
    }

    static const QRegularExpression letter(QStringLiteral("[\\p{L}]"));
    static const QRegularExpression digit(QStringLiteral("[\\p{N}]"));
    if (!letter.match(password).hasMatch() || !digit.match(password).hasMatch()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("密码必须同时包含字母和数字");
        }
        return false;
    }
    return true;
}

} // namespace Auth
