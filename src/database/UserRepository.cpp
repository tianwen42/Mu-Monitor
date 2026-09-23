#include "database/UserRepository.h"

#include "utils/TimeUtils.h"

#include <QCryptographicHash>
#include <QSqlError>
#include <QSqlQuery>
UserRepository::UserRepository(const QString &connectionName)
    : m_connectionName(connectionName)
{
}

QSqlDatabase UserRepository::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool UserRepository::initialize(QString *errorMessage)
{
    if (m_initialized) {
        return database().isOpen();
    }

    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("用户仓储无法访问已打开的 SQLite 连接");
        }
        return false;
    }

    if (!ensureSchema(errorMessage) || !seedRoles(errorMessage)
        || !seedRoleAssignments(errorMessage)) {
        return false;
    }

    m_initialized = true;
    return true;
}

bool UserRepository::ensureSchema(QString *errorMessage)
{
    QSqlQuery query(database());

    const QString usersSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL UNIQUE,"
        "password_hash TEXT NOT NULL,"
        "salt TEXT NOT NULL,"
        "role TEXT NOT NULL DEFAULT 'viewer',"
        "created_at TEXT NOT NULL"
        ")");
    if (!query.exec(usersSql)) {
        if (errorMessage) *errorMessage = QStringLiteral("初始化 users 表失败：%1").arg(query.lastError().text());
        return false;
    }

    const QList<QPair<QString, QString>> userColumns = {
        {QStringLiteral("display_name"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("enabled"), QStringLiteral("INTEGER NOT NULL DEFAULT 1")},
        {QStringLiteral("password_scheme"), QStringLiteral("TEXT NOT NULL DEFAULT 'legacy_sha256'")},
        {QStringLiteral("password_iterations"), QStringLiteral("INTEGER NOT NULL DEFAULT 100000")},
        {QStringLiteral("updated_at"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
    };
    for (const auto &column : userColumns) {
        if (!ensureColumn(QStringLiteral("users"), column.first,
                          column.second, errorMessage)) {
            return false;
        }
    }

    const QString rolesSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS roles ("
        "code TEXT PRIMARY KEY,"
        "display_name TEXT NOT NULL,"
        "description TEXT NOT NULL DEFAULT '',"
        "built_in INTEGER NOT NULL DEFAULT 1"
        ")");
    if (!query.exec(rolesSql)) {
        if (errorMessage) *errorMessage = QStringLiteral("初始化 roles 表失败：%1").arg(query.lastError().text());
        return false;
    }

    const QString permissionsSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS role_permissions ("
        "role_code TEXT NOT NULL,"
        "permission_code TEXT NOT NULL,"
        "PRIMARY KEY (role_code, permission_code),"
        "FOREIGN KEY (role_code) REFERENCES roles(code)"
        ")");
    if (!query.exec(permissionsSql)) {
        if (errorMessage) *errorMessage = QStringLiteral("初始化 role_permissions 表失败：%1").arg(query.lastError().text());
        return false;
    }

    const QString assignmentsSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS role_assignments ("
        "user_id INTEGER PRIMARY KEY,"
        "role_code TEXT NOT NULL,"
        "assigned_at TEXT NOT NULL,"
        "assigned_by TEXT NOT NULL DEFAULT '',"
        "FOREIGN KEY (user_id) REFERENCES users(id),"
        "FOREIGN KEY (role_code) REFERENCES roles(code)"
        ")");
    if (!query.exec(assignmentsSql)) {
        if (errorMessage) *errorMessage = QStringLiteral("初始化 role_assignments 表失败：%1").arg(query.lastError().text());
        return false;
    }

    QSqlQuery normalize(database());
    normalize.prepare(QStringLiteral(
        "UPDATE users SET updated_at = created_at "
        "WHERE updated_at IS NULL OR updated_at = ''"));
    if (!normalize.exec()) {
        if (errorMessage) *errorMessage = QStringLiteral("补齐用户更新时间失败：%1").arg(normalize.lastError().text());
        return false;
    }

    return true;
}

bool UserRepository::ensureColumn(const QString &table, const QString &column,
                                  const QString &definition, QString *errorMessage)
{
    QSqlQuery info(database());
    if (!info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        if (errorMessage) *errorMessage = QStringLiteral("读取 %1 表结构失败：%2").arg(table, info.lastError().text());
        return false;
    }

    while (info.next()) {
        if (info.value(1).toString() == column) {
            return true;
        }
    }

    QSqlQuery alter(database());
    const QString sql = QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                            .arg(table, column, definition);
    if (!alter.exec(sql)) {
        if (errorMessage) *errorMessage = QStringLiteral("迁移字段 %1.%2 失败：%3").arg(table, column, alter.lastError().text());
        return false;
    }
    return true;
}

bool UserRepository::seedRoles(QString *errorMessage)
{
    QSqlDatabase db = database();
    if (!db.transaction()) {
        if (errorMessage) *errorMessage = QStringLiteral("无法开始角色初始化事务：%1").arg(db.lastError().text());
        return false;
    }

    const QList<QPair<QString, QString>> roles = {
        {QStringLiteral("admin"), QStringLiteral("拥有全部权限，包括用户管理、确认告警、修改设置和查看历史。")},
        {QStringLiteral("operator"), QStringLiteral("可确认告警和查看历史，不可管理用户或修改系统设置。")},
        {QStringLiteral("viewer"), QStringLiteral("只读角色，仅可查看历史数据。")},
    };

    QSqlQuery roleQuery(db);
    roleQuery.prepare(QStringLiteral(
        "INSERT INTO roles (code, display_name, description, built_in) "
        "VALUES (?, ?, ?, 1) "
        "ON CONFLICT(code) DO UPDATE SET display_name = excluded.display_name, "
        "description = excluded.description, built_in = 1"));
    for (const auto &role : roles) {
        roleQuery.bindValue(0, role.first);
        roleQuery.bindValue(1, Auth::roleDisplayName(role.first));
        roleQuery.bindValue(2, role.second);
        if (!roleQuery.exec()) {
            db.rollback();
            if (errorMessage) *errorMessage = QStringLiteral("写入内置角色失败：%1").arg(roleQuery.lastError().text());
            return false;
        }
    }

    QSqlQuery deletePermissions(db);
    if (!deletePermissions.exec(QStringLiteral(
            "DELETE FROM role_permissions WHERE role_code IN ('admin', 'operator', 'viewer')"))) {
        db.rollback();
        if (errorMessage) *errorMessage = QStringLiteral("清理角色权限失败：%1").arg(deletePermissions.lastError().text());
        return false;
    }

    const QList<Auth::Permission> permissions = {
        Auth::Permission::ManageUsers,
        Auth::Permission::AcknowledgeAlarm,
        Auth::Permission::ModifySettings,
        Auth::Permission::ViewHistory,
        Auth::Permission::ControlCollection,
    };

    QSqlQuery permissionQuery(db);
    permissionQuery.prepare(QStringLiteral(
        "INSERT INTO role_permissions (role_code, permission_code) VALUES (?, ?)"));
    for (const QString &role : Auth::builtInRoleCodes()) {
        const QStringList rolePermissions = Auth::permissionsForRole(role);
        for (Auth::Permission permission : permissions) {
            const QString code = Auth::permissionCode(permission);
            if (!rolePermissions.contains(code)) {
                continue;
            }
            permissionQuery.bindValue(0, role);
            permissionQuery.bindValue(1, code);
            if (!permissionQuery.exec()) {
                db.rollback();
                if (errorMessage) *errorMessage = QStringLiteral("写入角色权限失败：%1").arg(permissionQuery.lastError().text());
                return false;
            }
        }
    }

    if (!db.commit()) {
        db.rollback();
        if (errorMessage) *errorMessage = QStringLiteral("提交角色初始化失败：%1").arg(db.lastError().text());
        return false;
    }
    return true;
}

bool UserRepository::seedRoleAssignments(QString *errorMessage)
{
    QSqlQuery query(database());
    const QString sql = QStringLiteral(
        "INSERT OR IGNORE INTO role_assignments "
        "(user_id, role_code, assigned_at, assigned_by) "
        "SELECT id, CASE WHEN role IN ('admin', 'operator', 'viewer') "
        "THEN role ELSE 'viewer' END, created_at, 'system' FROM users");
    if (!query.exec(sql)) {
        if (errorMessage) *errorMessage = QStringLiteral("迁移用户角色分配失败：%1").arg(query.lastError().text());
        return false;
    }

    QSqlQuery normalize(database());
    if (!normalize.exec(QStringLiteral(
            "UPDATE users SET role = ("
            "SELECT role_code FROM role_assignments WHERE user_id = users.id"
            ") WHERE role NOT IN ('admin', 'operator', 'viewer')"))) {
        if (errorMessage) *errorMessage = QStringLiteral("规范用户角色失败：%1").arg(normalize.lastError().text());
        return false;
    }
    return true;
}

Auth::UserRecord UserRepository::userFromQuery(const QSqlQuery &query) const
{
    Auth::UserRecord user;
    user.id = query.value(0).toLongLong();
    user.username = query.value(1).toString();
    user.displayName = query.value(2).toString();
    user.role = query.value(3).toString().trimmed().toLower();
    user.enabled = query.value(4).toInt() != 0;
    user.createdAt = TimeUtils::fromIso8601(query.value(5).toString());
    user.updatedAt = TimeUtils::fromIso8601(query.value(6).toString());
    return user;
}

QList<Auth::UserRecord> UserRepository::listUsers(QString *errorMessage)
{
    QList<Auth::UserRecord> users;
    if (!initialize(errorMessage)) {
        return users;
    }

    QSqlQuery query(database());
    const QString sql = QStringLiteral(
        "SELECT u.id, u.username, "
        "COALESCE(NULLIF(u.display_name, ''), u.username), "
        "COALESCE(ra.role_code, u.role), u.enabled, u.created_at, u.updated_at "
        "FROM users u "
        "LEFT JOIN role_assignments ra ON ra.user_id = u.id "
        "ORDER BY u.username COLLATE NOCASE");
    if (!query.exec(sql)) {
        if (errorMessage) *errorMessage = QStringLiteral("读取用户列表失败：%1").arg(query.lastError().text());
        return users;
    }

    while (query.next()) {
        users.append(userFromQuery(query));
    }
    return users;
}

bool UserRepository::findById(qint64 userId, Auth::UserRecord *user,
                              QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT u.id, u.username, "
        "COALESCE(NULLIF(u.display_name, ''), u.username), "
        "COALESCE(ra.role_code, u.role), u.enabled, u.created_at, u.updated_at "
        "FROM users u "
        "LEFT JOIN role_assignments ra ON ra.user_id = u.id "
        "WHERE u.id = ?"));
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        if (errorMessage) *errorMessage = QStringLiteral("未找到指定用户");
        return false;
    }

    if (user) *user = userFromQuery(query);
    return true;
}

bool UserRepository::findByUsername(const QString &username, Auth::UserRecord *user,
                                    QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT u.id, u.username, "
        "COALESCE(NULLIF(u.display_name, ''), u.username), "
        "COALESCE(ra.role_code, u.role), u.enabled, u.created_at, u.updated_at "
        "FROM users u "
        "LEFT JOIN role_assignments ra ON ra.user_id = u.id "
        "WHERE u.username = ? COLLATE NOCASE"));
    query.addBindValue(username.trimmed());
    if (!query.exec() || !query.next()) {
        if (errorMessage) *errorMessage = QStringLiteral("未找到指定用户");
        return false;
    }

    if (user) *user = userFromQuery(query);
    return true;
}

bool UserRepository::usernameExists(const QString &username, qint64 excludeUserId,
                                    QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return true;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT 1 FROM users WHERE username = ? COLLATE NOCASE AND id != ? LIMIT 1"));
    query.addBindValue(username.trimmed());
    query.addBindValue(excludeUserId);
    if (!query.exec()) {
        if (errorMessage) *errorMessage = QStringLiteral("检查用户名失败：%1").arg(query.lastError().text());
        return true;
    }
    return query.next();
}

bool UserRepository::createUser(const Auth::UserRecord &user,
                                const Auth::PasswordCredential &credential,
                                const QString &assignedBy,
                                qint64 *createdUserId,
                                QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }
    if (!credential.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("用户凭据无效");
        return false;
    }

    QSqlDatabase db = database();
    if (!db.transaction()) {
        if (errorMessage) *errorMessage = QStringLiteral("无法开始新增用户事务：%1").arg(db.lastError().text());
        return false;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "INSERT INTO users "
        "(username, password_hash, salt, role, created_at, display_name, enabled, "
        "password_scheme, password_iterations, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(user.username.trimmed());
    insert.addBindValue(credential.hash);
    insert.addBindValue(credential.salt);
    insert.addBindValue(user.role.trimmed().toLower());
    insert.addBindValue(TimeUtils::toUtcIso8601(now));
    insert.addBindValue(user.displayName.trimmed());
    insert.addBindValue(user.enabled ? 1 : 0);
    insert.addBindValue(credential.scheme);
    insert.addBindValue(credential.iterations);
    insert.addBindValue(TimeUtils::toUtcIso8601(now));

    if (!insert.exec()) {
        db.rollback();
        if (errorMessage) {
            *errorMessage = insert.lastError().text().contains(QStringLiteral("UNIQUE"),
                Qt::CaseInsensitive)
                ? QStringLiteral("用户名已存在")
                : QStringLiteral("新增用户失败：%1").arg(insert.lastError().text());
        }
        return false;
    }

    const qint64 userId = insert.lastInsertId().toLongLong();
    QSqlQuery assignment(db);
    assignment.prepare(QStringLiteral(
        "INSERT INTO role_assignments (user_id, role_code, assigned_at, assigned_by) "
        "VALUES (?, ?, ?, ?)"));
    assignment.addBindValue(userId);
    assignment.addBindValue(user.role.trimmed().toLower());
    assignment.addBindValue(TimeUtils::toUtcIso8601(now));
    assignment.addBindValue(assignedBy);
    if (!assignment.exec()) {
        db.rollback();
        if (errorMessage) *errorMessage = QStringLiteral("保存用户角色失败：%1").arg(assignment.lastError().text());
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        if (errorMessage) *errorMessage = QStringLiteral("提交新增用户失败：%1").arg(db.lastError().text());
        return false;
    }

    if (createdUserId) *createdUserId = userId;
    return true;
}

bool UserRepository::updateUser(qint64 userId,
                                const QString &username,
                                const QString &displayName,
                                const QString &role,
                                bool enabled,
                                const QString &assignedBy,
                                QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }

    QSqlDatabase db = database();
    Auth::UserRecord existing;
    if (!findById(userId, &existing, errorMessage)) {
        return false;
    }

    if (!db.transaction()) {
        if (errorMessage) *errorMessage = QStringLiteral("无法开始更新用户事务：%1").arg(db.lastError().text());
        return false;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QSqlQuery update(db);
    update.prepare(QStringLiteral(
        "UPDATE users SET username = ?, display_name = ?, role = ?, enabled = ?, "
        "updated_at = ? WHERE id = ?"));
    update.addBindValue(username.trimmed());
    update.addBindValue(displayName.trimmed());
    update.addBindValue(role.trimmed().toLower());
    update.addBindValue(enabled ? 1 : 0);
    update.addBindValue(TimeUtils::toUtcIso8601(now));
    update.addBindValue(userId);
    if (!update.exec()) {
        db.rollback();
        if (errorMessage) {
            *errorMessage = update.lastError().text().contains(QStringLiteral("UNIQUE"),
                Qt::CaseInsensitive)
                ? QStringLiteral("用户名已存在")
                : QStringLiteral("更新用户失败：%1").arg(update.lastError().text());
        }
        return false;
    }

    QSqlQuery assignment(db);
    assignment.prepare(QStringLiteral(
        "INSERT INTO role_assignments (user_id, role_code, assigned_at, assigned_by) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(user_id) DO UPDATE SET role_code = excluded.role_code, "
        "assigned_at = excluded.assigned_at, assigned_by = excluded.assigned_by"));
    assignment.addBindValue(userId);
    assignment.addBindValue(role.trimmed().toLower());
    assignment.addBindValue(TimeUtils::toUtcIso8601(now));
    assignment.addBindValue(assignedBy);
    if (!assignment.exec()) {
        db.rollback();
        if (errorMessage) *errorMessage = QStringLiteral("保存用户角色失败：%1").arg(assignment.lastError().text());
        return false;
    }

    if (existing.username != username.trimmed()) {
        QSqlQuery deleteSessions(db);
        deleteSessions.prepare(QStringLiteral("DELETE FROM sessions WHERE username = ?"));
        deleteSessions.addBindValue(existing.username);
        if (!deleteSessions.exec()) {
            db.rollback();
            if (errorMessage) *errorMessage = QStringLiteral("撤销用户旧会话失败：%1").arg(deleteSessions.lastError().text());
            return false;
        }
    }

    if (!db.commit()) {
        db.rollback();
        if (errorMessage) *errorMessage = QStringLiteral("提交更新用户失败：%1").arg(db.lastError().text());
        return false;
    }
    return true;
}

bool UserRepository::setEnabled(qint64 userId, bool enabled, QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "UPDATE users SET enabled = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(enabled ? 1 : 0);
    query.addBindValue(TimeUtils::toUtcIso8601());
    query.addBindValue(userId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        if (errorMessage) *errorMessage = QStringLiteral("更新用户状态失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

bool UserRepository::setRole(qint64 userId, const QString &role,
                             const QString &assignedBy, QString *errorMessage)
{
    Auth::UserRecord user;
    if (!findById(userId, &user, errorMessage)) {
        return false;
    }
    return updateUser(user.id, user.username, user.displayName, role,
                      user.enabled, assignedBy, errorMessage);
}

QString UserRepository::roleForUsername(const QString &username,
                                        QString *errorMessage)
{
    Auth::UserRecord user;
    if (!findByUsername(username, &user, errorMessage)) {
        return {};
    }
    return user.role;
}

qint64 UserRepository::enabledAdminCount(QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return -1;
    }

    QSqlQuery query(database());
    const QString sql = QStringLiteral(
        "SELECT COUNT(*) FROM users u "
        "LEFT JOIN role_assignments ra ON ra.user_id = u.id "
        "WHERE u.enabled = 1 AND COALESCE(ra.role_code, u.role) = 'admin'");
    if (!query.exec(sql) || !query.next()) {
        if (errorMessage) *errorMessage = QStringLiteral("统计启用管理员失败：%1").arg(query.lastError().text());
        return -1;
    }
    return query.value(0).toLongLong();
}

bool UserRepository::revokeAllSessions(const QString &username,
                                       QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral("DELETE FROM sessions WHERE username = ?"));
    query.addBindValue(username.trimmed());
    if (!query.exec()) {
        if (errorMessage) *errorMessage = QStringLiteral("撤销用户会话失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

bool UserRepository::revokeOtherSessions(const QString &username,
                                         const QString &preserveRawToken,
                                         QString *errorMessage)
{
    if (!initialize(errorMessage)) {
        return false;
    }

    if (preserveRawToken.trimmed().isEmpty()) {
        return revokeAllSessions(username, errorMessage);
    }

    const QString tokenHash = QString::fromLatin1(
        QCryptographicHash::hash(preserveRawToken.trimmed().toUtf8(),
                                 QCryptographicHash::Sha256).toHex());
    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "DELETE FROM sessions WHERE username = ? AND token_hash != ?"));
    query.addBindValue(username.trimmed());
    query.addBindValue(tokenHash);
    if (!query.exec()) {
        if (errorMessage) *errorMessage = QStringLiteral("撤销其他会话失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}
