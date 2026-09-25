# 数据库存储、初始化与用户管理设计

## 1. 设计目标

数据库方案必须同时满足：

- 首次启动可以自动创建数据库。
- 普通安装模式不会因为重装或升级丢失历史数据。
- 便携模式可以把数据放在程序目录旁边。
- 数据库路径不能被 Qt Creator 或命令行工作目录意外改变。
- 数据库迁移失败时可以从备份恢复。
- 明确区分主数据库、测试数据库、备份和日志。
- 支持用户、角色、权限和审计记录。

SQLite 固定作为本项目数据库，不引入服务端数据库。

---

## 2. 数据库路径策略

### 2.1 标准模式

Windows 默认使用：

```text
QStandardPaths::AppLocalDataLocation
```

实际目录通常为：

```text
C:\Users\<用户名>\AppData\Local\Mu-Monitor\Mu-Monitor\
```

主数据库：

```text
database/mu-monitor.db
```

使用 `AppLocalDataLocation` 而不是 `AppDataLocation`，因为当前数据库是设备监控运行数据，不应随 Windows 漫游配置同步。

### 2.2 便携模式

程序目录存在：

```text
portable.flag
```

时进入便携模式：

```text
<Mu-Monitor.exe所在目录>/data/database/mu-monitor.db
```

便携模式要求：

- 必须显式创建 `portable.flag`。
- 程序目录不可写时直接报错，不静默回退到其他目录。
- 部署脚本不能删除 `data/`。
- 卸载程序不能默认删除便携数据。

### 2.3 显式覆盖

优先级最高：

```text
--data-dir <目录>
```

环境变量：

```text
MU_MONITOR_DATA_DIR
```

主要用于：

- 测试。
- 多实例调试。
- 数据恢复到其他磁盘。
- 企业部署指定数据盘。

解析优先级：

```text
命令行 --data-dir
-> MU_MONITOR_DATA_DIR
-> portable.flag
-> AppLocalDataLocation
```

### 2.4 禁止使用当前工作目录

不能使用：

```cpp
QDir::currentPath()
```

因为 Qt Creator、快捷方式、PowerShell 和计划任务的工作目录可能不同。

便携模式必须使用：

```cpp
QCoreApplication::applicationDirPath()
```

---

### 2.5 旧路径迁移

当前版本已有的数据库路径：

```text
%APPDATA%\Mu-Monitor\Mu-Monitor\mu-monitor.db
```

切换到 `AppLocalDataLocation` 后，首次启动必须执行旧路径检测：

1. 新位置没有数据库，旧位置存在数据库时，先校验旧数据库。
2. 使用 SQLite 备份机制复制到新位置，不移动、不删除旧文件。
3. 对新数据库执行 `quick_check` 和关键表检查。
4. 校验通过后记录路径迁移完成标记。
5. 旧数据库继续保留为备份。
6. 复制或校验失败时禁止修改旧数据库，并回退到旧路径或提示用户处理。
7. 新旧数据库同时存在时不能自动合并，必须显示冲突并保留两份文件。

---

## 3. 数据目录结构

```text
<dataRoot>/
├── database/
│   ├── mu-monitor.db
│   ├── mu-monitor.db-wal
│   ├── mu-monitor.db-shm
│   └── backups/
│       ├── mu-monitor-20260920-163850.db
│       └── ...
├── logs/
│   ├── application.log
│   └── crash/
├── exports/
├── runtime/
│   └── mu-monitor.lock
└── config/
    └── database.json
```

规则：

- 数据库和备份必须位于同一数据根目录，便于整体迁移。
- 日志、导出和临时文件不能放进数据库目录。
- 备份文件必须带 UTC 时间戳。
- WAL 和 SHM 文件由 SQLite 管理，程序不主动删除。

---

## 4. 初始化流程

程序启动时按以下顺序执行：

1. 解析数据根目录。
2. 创建缺失的目录。
3. 获取 `QLockFile`，防止两个程序实例同时迁移。
4. 检查主数据库是否存在。
5. 数据库不存在时创建空数据库。
6. 数据库存在时执行 `PRAGMA quick_check`。
7. 读取当前 schema 版本。
8. 需要迁移时先创建备份。
9. 在事务中执行迁移。
10. 更新 schema 版本。
11. 设置运行时连接参数。
12. 开放登录和业务服务。

建议运行时参数：

```sql
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA busy_timeout = 5000;
PRAGMA trusted_schema = OFF;
```

要求：

- 每个 SQLite 连接只在其所属线程使用。
- 迁移只能由数据库工作线程执行。
- 迁移失败必须回滚并保留原始数据库。
- 禁止在迁移失败后自动删除数据库。

---

## 5. schema 版本管理

不能只依赖散落的 `ensureColumn()`。

使用两张表：

```sql
CREATE TABLE schema_migrations (
    version INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    applied_at TEXT NOT NULL,
    checksum TEXT NOT NULL
);
```

同时设置：

```sql
PRAGMA user_version = <当前版本>;
```

规则：

- 每个迁移有唯一递增版本号。
- 迁移文件按版本顺序执行。
- 已执行迁移不能修改 checksum。
- 发布版本必须能够从最近两个旧版本升级。
- 单元测试使用临时数据库，不能污染真实数据库。

---

## 6. 备份和恢复

### 迁移前备份

只要 schema 版本发生变化：

```text
database/mu-monitor.db
-> database/backups/mu-monitor-<UTC时间戳>.db
```

备份必须验证：

- 文件大小大于 0。
- SQLite 可以打开。
- `quick_check` 通过。
- 关键表存在。

### 日常备份

可配置：

- 每日首次启动备份一次。
- 默认保留最近 7 份。
- 只删除超过保留数量的备份。
- 不直接删除主数据库。

### 数据损坏处理

如果 `quick_check` 失败：

1. 禁止继续写入。
2. 将损坏文件重命名为：

```text
mu-monitor.corrupt-<UTC时间戳>.db
```

3. 保留 WAL 和 SHM。
4. 提示用户备份和恢复。
5. 不自动覆盖、不自动删除损坏数据库。

---

## 7. 部署与数据保护

`scripts/build_and_deploy.ps1` 必须遵守：

- 可以清理 exe、DLL、插件和发布临时文件。
- 不能递归删除整个 `dist/data`。
- 便携模式的 `data/` 必须默认保留。
- 只有显式执行 `-ResetData` 时才允许清理数据。
- 清理前必须输出最终绝对路径并要求确认。

程序卸载时：

- 默认保留数据库、日志和备份。
- 可以选择删除用户数据，但不能默认勾选。
- 删除数据库前必须二次确认。

自动清理策略：

- 默认关闭。
- 启用后只删除超过用户设置天数的遥测和日志。
- 告警、用户和审计记录默认不自动删除。
- 清理前记录日志并写入操作者。

---

## 8. 用户管理模块

### 8.1 数据表

建议表结构：

```text
users
roles
permissions
user_roles
role_permissions
sessions
audit_logs
```

### users

```text
id
username
display_name
password_hash
password_salt
password_algorithm
password_iterations
enabled
must_change_password
failed_login_count
locked_until
created_at
updated_at
last_login_at
```

规则：

- `username` 唯一。
- 用户默认使用禁用，不物理删除。
- 最后一个管理员不能被禁用。
- 当前登录用户不能禁用自己。

### roles

系统内置角色：

```text
admin
operator
viewer
```

- `admin`：系统配置、用户管理、设备控制、告警确认、导出。
- `operator`：设备查看、设备控制、告警确认、历史查询、导出。
- `viewer`：设备查看、历史查询和只读操作。

### permissions

建议权限代码：

```text
device.view
device.control
alarm.view
alarm.acknowledge
history.view
history.export
user.manage
system.configure
audit.view
```

### audit_logs

记录：

```text
id
actor_user_id
action
target_type
target_id
result
details
created_at
```

至少审计：

- 登录成功和失败。
- 用户创建、编辑和禁用。
- 角色和权限变更。
- 密码重置和修改。
- 数据库迁移和备份。
- 历史数据清理。
- 设备控制命令。

不能记录：

- 明文密码。
- 原始登录令牌。
- 无必要的敏感配置。

---

## 9. 密码和登录安全

当前自实现迭代 SHA-256 只能作为迁移期兼容方案。

目标方案：

- 使用 `QPasswordDigestor::deriveKeyPbkdf2` 或 Argon2id。
- 优先 PBKDF2-HMAC-SHA256，避免额外第三方依赖。
- 每个用户使用独立随机盐。
- 记录算法、参数和版本。
- 登录成功后按需升级旧密码哈希。
- 修改密码后撤销其他会话。
- 默认管理员必须修改初始密码。

建议基础策略：

- 连续失败 5 次后锁定 15 分钟。
- 登录失败不暴露用户名是否存在。
- 用户禁用后立即撤销会话。
- 审计记录不包含密码。
- 管理员重置密码后要求下次登录修改。

---

## 10. 用户管理界面

页面：

```text
用户管理
├── 用户列表
├── 新建用户
├── 编辑用户
├── 重置密码
├── 启用/禁用
├── 角色分配
└── 审计日志
```

功能要求：

- 支持按用户名和状态筛选。
- 所有危险操作必须二次确认。
- 用户列表不显示密码哈希和盐。
- 当前用户不能直接删除自己。
- 最后一个管理员不能被禁用。
- 密码框默认隐藏，可临时显示。
- 权限不足时隐藏或禁用操作入口，后端仍需校验。

---

## 11. 实施顺序

1. 数据目录解析和便携标记。
2. 数据库目录结构。
3. `schema_migrations` 和版本迁移。
4. 迁移前备份。
5. 部署脚本保留数据。
6. 用户、角色、权限表迁移。
7. PBKDF2 密码方案和旧数据升级。
8. 用户 CRUD 和角色分配。
9. 登录锁定、会话撤销和审计。
10. 用户管理与数据恢复测试。

---

## 12. 验收标准

- [ ] 删除程序文件和重新部署不会删除数据库。
- [ ] 便携模式数据库始终位于 `<exe目录>/data/database`。
- [ ] 标准模式数据库始终位于 `AppLocalDataLocation`。
- [ ] 不存在的数据库可以自动初始化。
- [ ] 迁移前自动创建可验证备份。
- [ ] 迁移失败不会破坏原数据库。
- [ ] 数据损坏时不会自动删除文件。
- [ ] 自动清理默认关闭且只影响配置的数据类型。
- [ ] 用户创建、编辑、禁用和角色变更均有审计记录。
- [ ] 连续登录失败会触发锁定。
- [ ] 最后一个管理员不能被禁用。
- [ ] 历史数据库和测试数据库完全隔离。