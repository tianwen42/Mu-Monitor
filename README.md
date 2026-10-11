# Mu-Monitor

Mu-Monitor 是一个面向 Windows 单机部署的工业设备监控与告警上位机，使用 C++17 和 Qt 6 Widgets 开发。

项目支持实时设备监控、TCP 自定义二进制协议、设备模拟器、SQLite 历史存储、告警生命周期、用户权限、审计日志和 Excel 导出，适合作为无硬件条件下的工业上位机演示与二次开发基础。

## 项目状态

```text
开发状态：暂停开发，保留可用于演示和继续迭代的稳定版本
推荐环境：Qt 6.8.3 LTS + MinGW 13.1.0 + CMake 3.30.5 + Ninja 1.12.1
运行平台：Windows 10/11 x64
```

## 核心能力

- Qt Widgets 桌面上位机界面
- 内置模拟数据源和外部 TCP 设备模拟器
- Protocol v1 二进制协议、CRC16、粘包和拆包处理
- TCP 超时、坏帧隔离、断线检测和自动重连
- 20 台独立可控模拟设备
- 高温、高压、离线、坏 CRC 和主动断开场景
- 实时数据表、趋势图、设备搜索和状态筛选
- SQLite 历史数据、事务迁移、迁移前备份
- 异步数据库线程、有界任务队列和批量写入
- 告警触发、确认、恢复和状态同步
- 用户、角色、权限、审计日志和密码安全存储
- 历史数据 Excel 导出
- QtTest、CTest 和自动部署脚本

## 架构概览

```text
DeviceSimulator / 真实 TCP 设备
                |
                v
       TcpDeviceDataSourceAdapter
                |
                v
        MonitoringService ------> AlarmEngine
                |
                v
     SqliteTelemetryRepository
                |
                v
              SQLite
                |
                v
          AppController
                |
                v
            MainWindow
```

内置模拟模式使用 `SimulationDataSource`，不需要启动外部设备模拟器。

主要设计原则：

- UI 不直接访问 Socket 或数据库。
- 网络、数据库和 Excel 导出不阻塞 GUI 线程。
- 数据源通过 `IDeviceDataSource` 与业务层解耦。
- 数据库连接只在数据库工作线程中使用。
- 高频数据通过有界队列传递，避免无限堆积。

## 技术栈

| 组件 | 版本 |
|---|---|
| C++ | C++17 |
| Qt | Qt 6.8.3 LTS |
| 编译器 | MinGW 13.1.0 64-bit |
| CMake | 3.30.5 |
| Ninja | 1.12.1 |
| IDE | Qt Creator 14.0.2，其他兼容版本也可 |
| 数据库 | SQLite |
| 测试 | QtTest / CTest |
| Excel | QXlsx 1.5.1 |

## 从 0 到 1 部署

### 1. 安装 Qt 和工具链

通过 Qt Maintenance Tool 安装：

```text
Qt 6.8.3 -> MinGW 13.1.0 64-bit
MinGW 13.1.0 64-bit
CMake 3.30.x
Ninja 1.12.x
Qt Creator 14.x 或兼容版本
```

默认脚本使用以下目录：

```text
D:\Qt\6.8.3\mingw_64
D:\Qt\Tools\mingw1310_64
D:\Qt\Tools\CMake_64
D:\Qt\Tools\Ninja
```

如果安装位置不同，需要修改：

```text
scripts/check.ps1
scripts/build_and_deploy.ps1
```

### 2. 获取源码

```powershell
git clone https://github.com/tianwen42/Mu-Monitor.git
cd Mu-Monitor
```

### 3. 编译并运行测试

```powershell
.\scripts\check.ps1 -Configuration Debug -Clean
.\scripts\check.ps1 -Configuration Release -Clean
```

成功标准：

```text
100% tests passed, 0 tests failed out of 19
```

### 4. 生成便携发布包

```powershell
.\scripts\build_and_deploy.ps1
```

脚本会完成：

1. 停止工作区内的旧进程。
2. 配置并编译 Release 版本。
3. 构建 `Mu-Monitor` 和 `DeviceSimulator`。
4. 刷新 `dist` 中的 Qt 6.8.3 运行库和插件。
5. 保留 `dist/data`、数据库、日志和备份。
6. 创建或保留 `dist/portable.flag`。

发布产物：

```text
dist/
  Mu-Monitor.exe
  DeviceSimulator.exe
  Qt6Core.dll
  Qt6Gui.dll
  Qt6Widgets.dll
  Qt6Network.dll
  Qt6Sql.dll
  platforms/
  sqldrivers/
  portable.flag
  data/
```

### 5. 启动程序

先启动设备模拟器：

```powershell
.\dist\DeviceSimulator.exe
```

再启动主程序：

```powershell
.\dist\Mu-Monitor.exe
```

默认管理员：

```text
用户名：admin
密码：123456
```

默认密码只用于开发环境，首次使用后应立即修改。

## 使用方式

### 模式一：内置模拟数据

主程序默认使用内置模拟数据源，不需要启动 `DeviceSimulator.exe`。

### 模式二：外部 TCP 设备模拟器

1. 启动 `DeviceSimulator.exe`。
2. 发送格式选择 `Protocol v1（二进制）`。
3. 监听地址填写 `127.0.0.1`，端口填写 `45454`。
4. 点击“启动服务”。
5. 在主程序中选择“设置 -> 连接 -> 数据源类型 -> TCP 设备”。
6. 填写 `127.0.0.1:45454` 并保存。
7. 从托盘菜单完全退出主程序，再重新启动。
8. 模拟器显示“已连接客户端：1”后，主程序开始接收数据。

设备模拟器支持对每台设备独立执行：

```text
启动设备
停止设备
高温
高压
离线
坏 CRC
主动断开
恢复正常
```

主程序会同步显示设备的在线、采集中、已停止和告警状态。

## 数据目录

数据目录按以下顺序解析：

```text
--data-dir
MU_MONITOR_DATA_DIR
portable.flag
QStandardPaths::AppLocalDataLocation
```

便携模式目录：

```text
<应用目录>\data\
  database\
    mu-monitor.db
    backups\
  logs\
  exports\
  runtime\
  config\
```

数据库行为：

- 目标数据库存在时直接接入。
- 目标数据库不存在时，会检查旧数据库并校验后复制接入。
- 旧数据库原文件保留。
- 迁移前创建带时间戳的备份。
- 损坏数据库不会被自动覆盖或删除。
- 默认部署不会清理 `dist/data`。

只有明确需要重置便携数据时才执行：

```powershell
.\scripts\build_and_deploy.ps1 -CleanData
```

## 项目结构

```text
src/
  alarm/       告警规则、事件、状态机和仓储接口
  app/         应用协调层和业务服务
  auth/        认证、用户管理、密码和审计
  config/      数据源和应用配置
  core/        领域模型和状态定义
  database/    SQLite、Repository 和数据库线程
  network/     TCP 数据源、连接工作器和协议映射
  protocol/    Protocol v1、CRC16 和帧编解码
  ui/          窗口、对话框、模型和自定义控件
  utils/       时间、Excel 和通用工具

tests/         QtTest 自动测试
tools/         独立 DeviceSimulator
scripts/       构建、测试和部署脚本
third_party/   第三方依赖
docs/          架构、协议、数据库和项目文档
```

## 测试

推荐使用统一脚本：

```powershell
.\scripts\check.ps1 -Configuration Debug -Clean
.\scripts\check.ps1 -Configuration Release -Clean
```

也可以直接运行 CTest：

```powershell
ctest --test-dir build\check-release --output-on-failure
```

测试覆盖：

- 协议编解码、CRC16 和流式拆包
- TCP 连接、超时和重连
- 设备模拟器和端到端 TCP
- SQLite 初始化、迁移和备份
- 异步数据库仓库和队列
- 用户、角色、权限和审计
- 告警状态机和设备状态同步
- 数据源工厂和应用层接线
- 主窗口响应式布局

## 常见问题

### 主程序没有数据

依次检查：

1. `DeviceSimulator.exe` 是否已启动服务。
2. 主程序数据源是否选择为“TCP 设备”。
3. 主机和端口是否与模拟器一致。
4. 模拟器是否显示“已连接客户端：1”。
5. 修改数据源后是否从托盘菜单完全退出并重启主程序。

### 运行提示缺少 Qt DLL

不要只复制 `Mu-Monitor.exe`。应使用完整 `dist` 目录，或重新执行：

```powershell
.\scripts\build_and_deploy.ps1
```

### 数据库初始化失败

检查：

- 数据目录是否可写。
- SQLite 文件是否被其他进程占用。
- `database/backups` 是否存在。
- 数据库 `quick_check` 是否通过。

程序不会自动删除或覆盖疑似损坏的数据库。

### 修改设置后没有生效

连接、启动页面和告警阈值等配置需要重启主程序后生效。请从系统托盘菜单选择“退出”，不要只关闭主窗口。

## 当前限制

- 告警运行时状态仍使用内存仓储，重启恢复需要继续完善 SQLite 告警仓库。
- 数据源修改后需要重启主程序。
- UI 仍保留部分同步历史查询入口。
- 尚未完成真实设备长期现场运行测试。
- QXlsx 使用 Qt GuiPrivate 私有接口，会出现第三方 CMake 警告，不影响当前构建。

## 相关文档

- `docs/architecture.md`：架构说明
- `docs/protocol.md`：Protocol v1 协议
- `docs/DATABASE_DESIGN.md`：数据库设计
- `docs/TODOLIST.md`：后续路线
- `third_party/VERSIONS.md`：第三方依赖版本

## 许可证

当前项目用于技术验证和作品展示，尚未发布正式开源许可证。