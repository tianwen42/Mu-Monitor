# Mu-Monitor

Mu-Monitor 是一个基于 Qt 6 和 C++17 的工业设备监控与告警桌面应用，面向单机 Windows 部署场景。

项目提供设备数据采集、实时监控、历史存储、告警处理、用户权限和 Excel 导出能力，并通过独立设备模拟器支持无硬件环境下的开发和测试。

## 主要功能

- Qt Widgets 桌面上位机界面
- 总览、实时监控、历史数据和告警中心
- 模拟数据源与 TCP 自定义协议数据源
- Protocol v1 二进制帧、CRC16 和数据流拆包
- TCP 连接状态、超时和自动重连
- 独立 `DeviceSimulator` 设备生产者
- 告警阈值、确认、恢复和离线检测
- SQLite 历史数据与事务迁移
- 异步数据库工作线程和有界任务队列
- 用户、角色、权限和审计日志
- PBKDF2 密码存储
- Excel `.xlsx` 数据导出
- Windows 系统托盘
- QtTest 自动测试和 CMake 构建部署脚本

## 架构概览

```text
MainWindow
    |
    v
AppController
    |
    +----------------------+
    |                      |
    v                      v
MonitoringService     UserManagementService
    |                      |
    +--------+-------------+
             |
             +--> AlarmEngine
             |
             +--> IDeviceDataSource
             |       |
             |       +--> SimulationDataSource
             |       +--> TcpDeviceDataSourceAdapter
             |
             +--> TelemetryRepository
                     |
                     v
             SqliteTelemetryRepository
                     |
                     v
                  SQLite
```

数据流：

```text
DeviceSimulator / 真实设备
-> TCP
-> FrameDecoder
-> TcpDeviceDataSourceAdapter
-> MonitoringService
-> AlarmEngine
-> TelemetryRepository
-> SQLite
-> AppController
-> MainWindow
```

设计原则：

- UI 不直接访问 Socket 或 SQL。
- 网络、数据库和大文件导出不与 GUI 共享阻塞线程。
- 数据源通过 `IDeviceDataSource` 与业务层解耦。
- 数据库连接只在数据库工作线程中创建和使用。
- 高频数据通过有界队列传递并执行背压控制。

## 技术栈

| 组件 | 版本或说明 |
|---|---|
| C++ | C++17 |
| Qt | Qt 6.5 及以上，当前开发环境 Qt 6.11.2 |
| 构建系统 | CMake 3.19 及以上 |
| 编译器 | MinGW 13.1 64-bit |
| 数据库 | SQLite |
| 测试 | QtTest / CTest |
| Excel | QXlsx 1.5.1 |
| 平台 | Windows 10/11 |

## 目录结构

```text
src/
  alarm/       告警规则、事件、状态机和仓储接口
  app/         应用协调层和业务服务
  auth/        认证、用户管理、密码和审计
  config/      应用配置和数据源配置
  core/        领域模型与状态定义
  database/    SQLite 管理、Repository 和数据库工作线程
  network/     数据源、TCP 工作器、帧解码和协议映射
  protocol/    Protocol v1、CRC16 和帧编解码
  ui/          窗口、对话框、模型和自定义控件
  utils/       时间、Excel 和其他工具

resources/     图标、QSS 和 Qt 资源文件
tests/         QtTest 自动测试
tools/         独立工具，包括 DeviceSimulator
scripts/       构建、部署和检查脚本
third_party/   第三方依赖
```

## 构建要求

- Windows 10/11
- Qt 6.5 或更高版本
- Qt Core、Widgets、Sql、Network、LinguistTools 和 Test 模块
- MinGW 64-bit 或兼容工具链
- CMake 3.19 或更高版本
- Ninja
- Git

当前脚本默认使用以下路径，其他环境需要相应修改：

```text
D:\Qt\6.11.2\mingw_64
D:\Qt\Tools\mingw1310_64
D:\Qt\Tools\CMake_64
D:\Qt\Tools\Ninja
```

## 构建

### 使用 Qt Creator

1. 打开项目根目录的 `CMakeLists.txt`。
2. 选择 Qt 6 MinGW 64-bit Kit。
3. 选择 Debug 或 Release。
4. 构建 `Mu-Monitor` 和 `DeviceSimulator`。

### 使用命令行

```powershell
$env:PATH = "D:\Qt\6.11.2\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;" + $env:PATH

cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=D:\Qt\6.11.2\mingw_64

cmake --build build
```

生成目标：

```text
Mu-Monitor.exe
DeviceSimulator.exe
```

## 运行

### 启动设备模拟器

```powershell
cmake --build build --target DeviceSimulator
.\build\DeviceSimulator.exe
```

模拟器默认监听 `127.0.0.1:45454`，支持：

- 多台模拟设备
- 正常遥测
- 高温和高压
- 设备离线
- 坏 CRC
- 主动断开客户端
- Protocol v1 和 JSON Lines 兼容模式

### 启动监控客户端

```powershell
cmake --build build --target Mu-Monitor
.\build\Mu-Monitor.exe
```

默认数据源为进程内模拟器。将 `QSettings` 中的 `dataSource/type` 设置为 `tcp` 后，可以使用 TCP 数据源连接 `DeviceSimulator`。

数据源配置键：

```text
dataSource/type
dataSource/host
dataSource/port
dataSource/samplingIntervalMs
dataSource/heartbeatIntervalMs
dataSource/reconnect/enabled
dataSource/reconnect/delayMs
dataSource/reconnect/maxDelayMs
dataSource/reconnect/connectTimeoutMs
dataSource/reconnect/readTimeoutMs
```

## 测试

推荐使用统一检查脚本：

```powershell
.\scripts\check.ps1 -Configuration Debug -Clean
.\scripts\check.ps1 -Configuration Release -Clean
```

也可以直接运行 CTest：

```powershell
ctest --test-dir build\check-debug --output-on-failure
```

测试覆盖范围包括：

- 领域模型和状态转换
- 时间和格式化工具
- 协议编解码、CRC16 和流式拆包
- TCP 连接、超时和重连
- 设备模拟器和端到端 TCP
- SQLite 初始化、迁移和备份
- 异步 Repository 和队列
- 用户、角色和审计
- 告警状态机
- 数据源工厂
- 应用层接线
- 主窗口响应式布局

## 数据存储

数据目录解析优先级：

```text
--data-dir
MU_MONITOR_DATA_DIR
portable.flag
QStandardPaths::AppLocalDataLocation
```

目录布局：

```text
<data-dir>/
  database/
    mu-monitor.db
    backups/
  logs/
  exports/
  runtime/
  config/
```

Windows 标准模式：

```text
%LOCALAPPDATA%\Mu-Monitor\Mu-Monitor\database\mu-monitor.db
```

便携模式：

```text
<应用目录>\data\database\mu-monitor.db
```

数据库初始化会：

- 检查写权限和 SQLite `quick_check`
- 启用 WAL、`busy_timeout` 和外键约束
- 使用 `schema_version` 执行事务迁移
- 在迁移前创建备份
- 保护损坏数据库，不自动覆盖或删除

旧版 `%APPDATA%` 数据库会在新位置为空时校验并复制迁移，原文件保留。

## 用户与安全

首次启动会创建默认管理员：

```text
用户名：admin
密码：123456
```

内置角色：

```text
admin
operator
viewer
```

已实现：

- 用户新增、编辑、禁用和启用
- 管理员重置密码
- 用户修改本人密码
- 最后一个管理员保护
- 登录和用户管理审计
- PBKDF2-SHA256 密码存储
- 禁用用户后撤销会话
- 修改密码后撤销其他会话

> 默认密码仅用于开发环境，部署前应完成首次修改密码流程。

## Excel 导出

系统设置中的“数据导出”页面支持：

- 导出全部设备最新信息
- 按时间范围导出遥测历史
- 生成 `.xlsx` 文件

导出由 MIT 许可的 QXlsx 实现，版本信息见 `third_party/VERSIONS.md`。

## 部署

执行 Release 构建和部署：

```powershell
.\scripts\build_and_deploy.ps1
```

脚本会：

1. 检查并停止工作区内的 Mu-Monitor 进程
2. 清理脚本专用构建目录并重新配置 Release
3. 编译 Mu-Monitor
4. 更新 `dist` 中的可执行文件和 Qt 运行时
5. 保留 `dist/data`、备份和日志
6. 创建或保留 `dist/portable.flag`

默认部署不会清理数据库。只有明确需要重置便携数据时才使用：

```powershell
.\scripts\build_and_deploy.ps1 -CleanData
```

## 当前限制

- 告警历史目前使用内存仓储，尚未持久化到 SQLite。
- 告警确认和清除信号尚未完整接入 UI。
- `SettingsDialog` 尚未完整接入统一数据源配置。
- UI 仍保留部分兼容性的同步历史查询入口。
- 尚未完成真实设备长期现场运行测试。
- QXlsx 会产生 Qt GuiPrivate 第三方 CMake 警告，不影响构建和测试。

## 相关文档

- 架构说明：`docs/architecture.md`
- 协议说明：`docs/protocol.md`
- 数据库设计：`docs/DATABASE_DESIGN.md`
- 项目路线：`docs/TODOLIST.md`
- 第三方版本：`third_party/VERSIONS.md`

## 许可证

当前项目用于技术验证和作品展示，尚未发布正式开源许可证。