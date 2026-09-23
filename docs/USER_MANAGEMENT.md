# 用户管理、角色与审计

本文说明 Mu-Monitor 的用户数据模型、角色权限、密码策略、审计记录以及账号禁用和恢复流程。

## 设计边界

用户管理按以下职责拆分：

| 类型 | 职责 |
| --- | --- |
| `UserRepository` | 用户、角色目录、权限目录、角色分配和会话撤销的持久化 |
| `PasswordRepository` | 读取和更新密码凭据，不执行哈希或密码校验 |
| `PasswordService` | 密码策略、随机盐、PBKDF2 派生、旧算法校验和登录升级 |
| `AuthenticationService` | 登录校验、禁用账号拒绝、成功登录后的密码升级和登录审计 |
| `AuditRepository` | 审计日志写入、查询和敏感上下文脱敏 |
| `UserManagementService` | 用户 CRUD、启停、角色变更、密码重置和权限二次校验 |

Widget 只负责收集输入、调用服务并按权限禁用操作，不直接执行 SQL，也不实现密码算法。

## 表结构

### `users`

在原有登录表上通过可重复执行的 `ALTER TABLE` 迁移增加字段：

```sql
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    salt TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'viewer',
    created_at TEXT NOT NULL
);

ALTER TABLE users ADD COLUMN display_name TEXT NOT NULL DEFAULT '';
ALTER TABLE users ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1;
ALTER TABLE users ADD COLUMN password_scheme TEXT NOT NULL DEFAULT 'legacy_sha256';
ALTER TABLE users ADD COLUMN password_iterations INTEGER NOT NULL DEFAULT 100000;
ALTER TABLE users ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `username` | 唯一用户名，比较时不区分大小写 |
| `password_hash` | 旧算法或 PBKDF2 的十六进制摘要 |
| `salt` | 随机盐的十六进制文本；新密码使用 16 字节安全随机盐 |
| `role` | 兼容旧代码的角色镜像 |
| `display_name` | UI 显示名；为空时使用用户名 |
| `enabled` | `1` 表示启用，`0` 表示禁用，不提供删除 |
| `password_scheme` | `legacy_sha256` 或 `pbkdf2_sha256` |
| `password_iterations` | 当前凭据的迭代次数 |
| `created_at` / `updated_at` | UTC ISO 8601 时间 |

### `roles`

内置角色目录，由程序初始化并保持可迁移：

```sql
CREATE TABLE IF NOT EXISTS roles (
    code TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    built_in INTEGER NOT NULL DEFAULT 1
);
```

### `role_permissions`

持久化角色与权限码的映射：

```sql
CREATE TABLE IF NOT EXISTS role_permissions (
    role_code TEXT NOT NULL,
    permission_code TEXT NOT NULL,
    PRIMARY KEY (role_code, permission_code),
    FOREIGN KEY (role_code) REFERENCES roles(code)
);
```

### `role_assignments`

用户当前角色分配的权威来源：

```sql
CREATE TABLE IF NOT EXISTS role_assignments (
    user_id INTEGER PRIMARY KEY,
    role_code TEXT NOT NULL,
    assigned_at TEXT NOT NULL,
    assigned_by TEXT NOT NULL DEFAULT '',
    FOREIGN KEY (user_id) REFERENCES users(id),
    FOREIGN KEY (role_code) REFERENCES roles(code)
);
```

迁移时从 `users.role` 回填角色分配。此后服务读取 `role_assignments`，并同步更新 `users.role` 以兼容旧接口。

### `audit_logs`

```sql
CREATE TABLE IF NOT EXISTS audit_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    occurred_at TEXT NOT NULL,
    actor_username TEXT NOT NULL,
    target_username TEXT NOT NULL,
    event_type TEXT NOT NULL,
    result TEXT NOT NULL,
    context TEXT NOT NULL DEFAULT ''
);
```

### `sessions`

沿用现有免登录会话表。密码修改或重置后，通过删除相应用户的 `token_hash` 记录撤销会话，不保存或记录原始令牌。

## 内置角色与权限

权限码如下：

| 权限码 | 功能 |
| --- | --- |
| `manage_users` | 新增、编辑、启用、禁用、分配角色、重置密码 |
| `acknowledge_alarm` | 确认告警 |
| `modify_settings` | 修改系统设置 |
| `view_history` | 查看历史数据 |
| `control_collection` | 控制采集启停 |

角色矩阵：

| 角色 | 用户管理 | 确认告警 | 修改设置 | 查看历史 | 控制采集 |
| --- | --- | --- | --- | --- | --- |
| `admin` 管理员 | 是 | 是 | 是 | 是 | 是 |
| `operator` 操作员 | 否 | 是 | 否 | 是 | 是 |
| `viewer` 只读用户 | 否 | 否 | 否 | 是 | 否 |

UI 根据权限码禁用按钮或工具栏操作。所有用户管理方法仍在 `UserManagementService` 中再次检查权限，不能通过直接调用或构造对话框绕过。

## 密码策略

### 新密码

新密码、管理员重置密码和用户修改密码必须满足：

- 长度 8 到 128 个字符。
- 至少包含一个字母和一个数字。
- 使用 PBKDF2-HMAC-SHA256。
- 使用 16 字节安全随机盐。
- 派生 32 字节摘要，默认迭代 120000 次。
- 数据库中只保存十六进制摘要和盐，不保存明文密码。

旧版默认账号密码为 `123456`，仅用于开发阶段。旧密码不受新密码长度策略影响，仍可登录一次，成功后会升级为 PBKDF2。

### 旧算法兼容与升级

旧算法为 100000 次自定义 SHA-256 迭代。登录流程通过 `PasswordService` 识别：

1. 旧摘要校验成功。
2. 立即用用户输入的密码生成新的 PBKDF2 凭据。
3. 在同一个用户记录上更新哈希、盐、算法和迭代次数。
4. 写入 `login_success` 审计，并标记 `password_upgraded=true`。
5. 校验失败时不升级，只写登录失败审计。

### 会话撤销

- 管理员重置用户密码后，删除该用户全部 `sessions` 记录。
- 禁用用户前删除该用户全部 `sessions` 记录，避免已保存会话绕过禁用状态。
- 用户修改自己的密码后，删除该用户其他会话；若存在当前免登录令牌，则保留当前令牌对应会话。
- 修改用户名时删除旧用户名对应的会话。

## 审计策略

记录以下事件：

| 事件类型 | 触发条件 |
| --- | --- |
| `login_success` | 登录成功 |
| `login_failure` | 用户名、密码、状态或凭据校验失败 |
| `user_create` | 新增用户成功或失败 |
| `user_edit` | 编辑用户名、显示名、角色或状态 |
| `user_enable` | 启用用户 |
| `user_disable` | 禁用用户 |
| `role_change` | 角色变更 |
| `password_change` | 用户修改自己的密码 |
| `password_reset` | 管理员重置密码 |

每条记录包含：

- `occurred_at`：UTC 时间。
- `actor_username`：操作者；登录失败时为目标用户名。
- `target_username`：目标用户。
- `event_type`：事件类型。
- `result`：`success` 或 `failure`。
- `context`：角色、状态、算法、失败原因等必要上下文。

禁止写入密码、密码摘要、盐、免登录令牌或原始请求。`AuditRepository` 对常见 `password`、`passwd`、`pwd`、`token`、`password_hash`、`salt` 键值再次执行脱敏。

## 用户 CRUD 与安全约束

- 新增用户必须校验用户名唯一、角色合法和密码策略。
- 编辑用户名时必须继续保证唯一。
- 不提供删除用户的方法；离职或停用只设置 `enabled=0`。
- 当前用户不能禁用自己。
- 最后一个启用的管理员不能被禁用或降权。
- 管理员重置密码需要 `manage_users` 权限。
- 用户修改自己的密码需要验证旧密码，不需要 `manage_users` 权限。
- 所有失败操作记录审计，但不记录输入密码。

## 禁用与恢复流程

禁用：

1. 管理员在“用户管理”中选择用户并点击“禁用用户”。
2. 服务层检查 `manage_users` 权限。
3. 服务层拒绝当前用户自禁用。
4. 若目标是启用状态的管理员，统计启用管理员数量；只剩一个时拒绝。
5. 删除该用户全部免登录会话。
6. 将 `users.enabled` 设为 `0`，写入 `user_disable` 审计。
7. 被禁用账号不能重新登录，旧免登录会话也已失效。

恢复：

1. 管理员选择已禁用用户并点击“启用用户”。
2. 服务层检查 `manage_users` 权限。
3. 将 `users.enabled` 设为 `1`，写入 `user_enable` 审计。
4. 用户可使用当前密码重新登录；若密码已由管理员重置，则使用新密码。

角色降权与恢复使用同一套保护规则。即使 `DatabaseManager` 为兼容旧版本重新同步默认账号的 `users.role`，`role_assignments` 仍是授权判断来源，因此不会覆盖已经审计过的角色分配。


