# SQLite 数据可靠性与持久化设计

## 目标

Mu-Monitor 使用统一的数据目录解析规则，避免程序启动位置影响数据库位置。数据库初始化、旧版本兼容和结构迁移必须可诊断、可回滚，并且不得删除或静默覆盖用户已有数据。

## 数据目录解析

数据目录按固定优先级解析：

| 优先级 | 来源 | 结果 |
|---|---|---|
| 1 | 命令行 `--data-dir <绝对路径>` 或 `--data-dir=<绝对路径>` | 使用显式目录 |
| 2 | 环境变量 `MU_MONITOR_DATA_DIR` | 使用绝对路径 |
| 3 | 可执行文件旁的 `portable.flag` 文件 | 使用可执行文件旁的 `data` 目录 |
| 4 | `QStandardPaths::AppLocalDataLocation` | 使用 Qt 平台本地应用数据目录 |

显式路径必须是绝对路径。程序不会使用 `QDir::currentPath()`、启动工作目录或当前目录作为数据目录的默认值。

Windows 默认数据库位置示例：

```text
%LOCALAPPDATA%\Mu-Monitor\Mu-Monitor\database\mu-monitor.db
```

便携部署位置：

```text
<安装目录>\portable.flag
<安装目录>\data\database\mu-monitor.db
```

## 标准目录布局

首次初始化时自动创建：

```text
<data-dir>/
  database/
    mu-monitor.db
    backups/
  logs/
  exports/
  runtime/
    database-migration.lock
  config/
```

`database` 仅保存数据库和备份，`runtime` 保存进程级锁和临时运行文件，`logs`、`exports`、`config` 分别用于日志、导出和配置。已有数据库通过 `sqlite_master` 检查后直接复用。

## 初始化流程

1. 解析数据目录并创建标准目录布局。
2. 检查目录和数据库文件是否可写。
3. 检查新旧数据库冲突，必要时先校验旧库，再创建一致的新副本；旧文件始终保留。
4. 获取 `QLockFile` 迁移锁，防止多个实例同时执行旧库导入或结构迁移。
5. 使用 QSQLITE 打开数据库，并执行 `PRAGMA quick_check`。
6. 启用 `PRAGMA foreign_keys = ON`、`PRAGMA journal_mode = WAL` 和 `PRAGMA busy_timeout = 5000`。
7. 读取 `schema_version`，在需要时于单个事务内执行结构迁移和兼容性数据整理。
8. 确认默认管理员账号并完成初始化。

初始化失败时返回带上下文的错误，不会删除损坏数据库。损坏数据库还会保留原始字节，便于人工恢复。

## 旧数据库兼容

旧版本数据库位置为：

```text
%APPDATA%\Mu-Monitor\Mu-Monitor\mu-monitor.db
```

迁移规则：

- 新位置没有数据库、旧位置存在数据库时，先以只读连接执行 `quick_check`。
- 校验通过后，使用 SQLite `VACUUM INTO` 生成一致副本，再原子移动到新数据库路径。
- 旧数据库不删除、不覆盖，继续作为原始备份保留。
- 新旧数据库同时存在时明确报错并列出两个路径，不自动选择、不自动合并。
- `MU_MONITOR_DATA_DIR` 或 `--data-dir` 指定其他目录时，同样会检查平台旧数据库位置并执行上述规则。

## Schema 版本与迁移

`schema_version` 表记录已应用迁移：

```sql
CREATE TABLE schema_version (
    version INTEGER PRIMARY KEY,
    description TEXT NOT NULL,
    applied_at TEXT NOT NULL
);
```

当前版本为 `1`，表示基线数据库结构。迁移流程如下：

1. 读取 `schema_version`；旧库没有该表时视为版本 `0`。
2. 拒绝数据库版本高于程序支持版本的情况，防止意外降级。
3. 对已有数据库执行迁移前备份。
4. 开启 SQLite 事务。
5. 按版本顺序执行迁移；结构创建、兼容性数据整理和 `schema_version` 记录处于同一事务。
6. 任一步失败立即回滚整个迁移事务，不提交部分结构或部分数据转换。
7. 事务提交成功后才允许应用继续启动。

迁移备份位于 `database/backups`，文件名为：

```text
mu-monitor-before-v<旧版本>-<UTC时间>-<随机后缀>.db
```

备份使用 `VACUUM INTO` 生成一致的 SQLite 快照。备份文件不会覆盖已有文件，也不会因初始化失败而删除。

## 并发与故障恢复

- 迁移锁位于 `runtime/database-migration.lock`，由 `QLockFile` 管理。
- 获取锁超时或失败时，初始化返回明确错误。
- 程序退出或进程异常结束后，锁文件可被后续实例按 Qt 锁规则恢复。
- WAL 模式减少读写阻塞，`busy_timeout` 限制短暂锁竞争产生的立即失败。
- 外键约束在每个数据库连接上显式启用。

## 部署数据保留

`scripts/build_and_deploy.ps1` 默认只更新程序文件和 Qt 运行时，不删除 `dist/data`。便携模式的数据库、日志、导出、配置、运行时文件和备份都位于该目录下。

只有显式传入 `-CleanData` 才会清理：

```powershell
.\scripts\build_and_deploy.ps1 -CleanData
```

普通重新部署不会清理数据库或备份。

## 测试覆盖

`tests/DataDirectoryTest.cpp` 覆盖路径优先级、便携模式和默认位置。`tests/DatabaseManagerTest.cpp` 覆盖：

- 首次初始化和标准目录创建
- 重复打开与数据持久化
- `quick_check` 和损坏数据库保护
- WAL、`busy_timeout`、外键约束
- 迁移成功和迁移失败回滚
- 旧数据库导入且保留原文件
- 新旧数据库冲突
- 迁移前备份创建
- 不可写数据目录和只读数据库拒绝初始化
