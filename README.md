# Mu-Monitor

工业设备监控与告警桌面应用，基于 Qt 6 和 C++ 开发。

当前阶段：**阶段 0 和阶段 1 已完成，准备进入 MainWindow 拆分；独立 TCP 设备模拟器已具备基础功能**。

## 项目目标

Mu-Monitor 旨在形成一套完整的工业数据链路：

```text
设备模拟器
→ TCP / Modbus / MQTT
→ 协议解析
→ 数据管道
→ SQLite
→ 实时界面
→ 告警中心
```

## 当前功能

- 标准 Qt 主窗口
- 顶部 QToolBar 操作栏
- 总览、实时监控、历史数据主页面
- 可移动、可关闭的设备/告警/日志 Dock
- KPI 卡片和实时趋势图
- 模拟设备数据
- 告警产生、显示、确认和清空
- 运行日志面板
- 独立系统设置对话框
- 独立关于对话框
- SQLite 历史数据查询
- 顶部告警中心 Tab，点击告警可定位对应设备
- 独立 DeviceSimulator（TCP Server、4 台模拟设备、故障场景）
- QSS 浅色工业主题
- CMake 构建和 MinGW 部署

## Excel 数据导出

系统设置中的“数据导出”页面提供：

- 导出全部设备信息
- 选择开始时间和结束时间
- 按时间范围导出 SQLite 遥测历史
- 生成真正的 `.xlsx` 文件

导出使用 MIT 许可的 QXlsx 库。
## 关闭与系统托盘

点击主窗口关闭按钮时，程序不会退出，而是隐藏到 Windows 系统托盘。

托盘菜单提供：

- 显示主界面
- 开始/暂停采集
- 退出 Mu-Monitor

单击或双击托盘图标也可以恢复主窗口。

只有托盘菜单中的“退出 Mu-Monitor”会真正退出程序。

如果系统托盘不可用，关闭按钮会按普通方式退出程序。
## 数据存储与目录

账户、登录会话、遥测历史、设备心跳和系统日志都会写入 SQLite 数据库。

数据目录按固定优先级解析：

1. 命令行 `--data-dir <绝对路径>` 或 `--data-dir=<绝对路径>`
2. 环境变量 `MU_MONITOR_DATA_DIR`
3. 可执行文件旁的 `portable.flag`，使用 `<exe目录>\data`
4. `QStandardPaths::AppLocalDataLocation`

程序不会使用当前工作目录作为数据目录。标准布局如下：

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

Windows 默认数据库位置：

```text
%LOCALAPPDATA%\Mu-Monitor\Mu-Monitor\database\mu-monitor.db
```

便携部署数据库位置：

```text
<安装目录>\data\database\mu-monitor.db
```

首次启动会自动创建目录和 SQLite 数据库，并创建默认管理员账号：

```text
用户名：admin
密码：123456
```

已有数据库会直接复用。初始化前会检查写权限和 SQLite `quick_check`，启用 WAL、`busy_timeout` 和外键约束。结构升级使用 `schema_version` 事务迁移，并在迁移前把备份写入 `database/backups`。

数据库默认位于 `AppLocalDataLocation` 的 `database/mu-monitor.db`。程序目录存在 `portable.flag` 时，使用程序目录下的 `data/database/mu-monitor.db`。也可以通过 `--data-dir` 或 `MU_MONITOR_DATA_DIR` 显式指定数据目录。

旧版 `%APPDATA%\Mu-Monitor\Mu-Monitor\mu-monitor.db` 会在新位置没有数据库时先校验并复制迁移，旧文件保留。新旧数据库同时存在时会报冲突，不会静默选择或自动合并。重新发布时，`data`、`backups` 和 `logs` 目录不会被发布脚本清理。

密码不会以明文保存，数据库中使用带随机盐、10 万轮迭代的 SHA-256 哈希。

登录窗口提供“30 天内记住登录状态”。勾选后：

- SQLite `sessions` 表保存令牌哈希和过期时间
- Windows `QSettings` 保存本地登录令牌
- 有效期内启动程序会跳过登录窗口
- 每次成功自动登录后会重新续期 30 天

> 默认密码仅用于开发阶段，后续必须增加修改密码功能。

更多设计见 [docs/DATABASE_DESIGN.md](docs/DATABASE_DESIGN.md)。

## 开发环境

- Windows 10/11
- Qt 6.11.2
- Qt Creator
- MinGW 13.1 64-bit
- CMake 3.30+
- Ninja
- Git

## 目录结构

```text
src/
  app/        应用启动与依赖装配
  ui/         窗口、对话框、自定义控件和表格模型
  core/       领域模型与接口
  network/    TCP、Modbus、MQTT 和协议解析
  database/   SQLite 与数据访问
  alarm/      告警规则和状态机
  utils/      通用工具
resources/    图标、QSS、QRC 和翻译
tests/        QtTest 单元测试
docs/         设计文档和开发清单
scripts/      构建和部署脚本
third_party/  第三方源码
build/        CMake 构建目录，自动生成且不提交
dist/         windeployqt 发布目录，不提交
```

## 使用 Qt Creator 构建

1. 打开 Qt Creator。
2. 打开：

```text
D:\qtProject\Mu-Monitor\CMakeLists.txt
```

3. 选择 Kit：

```text
Desktop Qt 6.11.2 MinGW 64-bit
```

4. 选择 `Debug` 或 `Release`。
5. 点击构建，再点击运行。

## 一键清理、编译并部署

Windows 下双击或从终端运行：

```bat
scripts\build_and_deploy.bat
```

或者直接运行 PowerShell 脚本：

```powershell
.\scripts\build_and_deploy.ps1
```

脚本会执行：

1. 检查并停止工作区内的 Mu-Monitor 进程
2. 清理脚本专用构建目录并重新配置 Release
3. 编译 Mu-Monitor
4. 只更新 `dist` 中的 exe 和 Qt 运行时
5. 保留 `dist/data` 中的数据库、日志和备份
6. 写入或保留 `dist/portable.flag`
7. 输出最终 exe、数据目录和 dist 大小

默认部署不会清理数据。只有明确需要重置便携数据时才执行：

```powershell
.\scripts\build_and_deploy.ps1 -CleanData
```

`-CleanData` 是唯一会删除 `dist/data` 的部署参数。
## 命令行构建

先配置 MinGW 和 Qt 环境：

```powershell
$env:PATH = "D:\Qt\6.11.2\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;" + $env:PATH
```

配置 CMake：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

构建：

```powershell
cmake --build build
```

直接使用 Qt Creator 已有构建目录：

```powershell
cmake --build D:/qtProject/Mu-Monitor/build/Desktop_Qt_6_11_2_MinGW_64_bit_Release --target Mu-Monitor
```

## 独立设备模拟器

模拟器默认监听 `127.0.0.1:45454`。启动后点击“启动服务”，程序会每秒发送 4 台设备的换行分隔 JSON 遥测数据，并可对选中设备触发高温、高压、离线和恢复场景。

```powershell
cmake --build build --target DeviceSimulator
.\build\DeviceSimulator.exe
```

## 自动化测试

推荐使用统一检查脚本，从独立构建目录执行配置、构建和测试：

```powershell
.\scripts\check.ps1 -Clean
.\scripts\check.ps1 -Configuration Release -Clean
```

也可以只运行已有构建目录中的 QtTest：

```powershell
ctest --test-dir build --output-on-failure
```

当前覆盖领域模型与状态转换、时间工具、遥测表格模型、主窗口缩放响应、数据目录解析、SQLite 初始化与迁移、Excel 导出和模拟器 TCP 收发。
## Debug 与 Release

- `Debug`：用于断点调试，速度较慢。
- `Release`：用于测试和发布，开启优化。

调试快捷键：

```text
F9        设置或取消断点
F5        开始调试或继续
F10       单步跳过
F11       单步进入
Shift+F11 跳出
Shift+F5  停止调试
```

## 发布部署

先构建 Release，然后将 exe 复制到 `dist`：

```powershell
Copy-Item `
  "build\Desktop_Qt_6_11_2_MinGW_64_bit_Release\Mu-Monitor.exe" `
  "dist\Mu-Monitor.exe" -Force
```

执行 Qt 部署：

```powershell
$env:PATH = "D:\Qt\6.11.2\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;" + $env:PATH

& "D:\Qt\6.11.2\mingw_64\bin\windeployqt.exe" `
  --release `
  --compiler-runtime `
  --no-translations `
  "dist\Mu-Monitor.exe"
```

最后直接运行：

```text
dist\Mu-Monitor.exe
```

## Git 工作流

查看状态：

```powershell
git status
```

提交修改：

```powershell
git add -A
git commit -m "feat: describe the change"
```

## 开发计划

长期工业化路线见：

[docs/TODOLIST.md](docs/TODOLIST.md)

2026-10-15 项目交付计划见：

[docs/DELIVERY_PLAN.md](docs/DELIVERY_PLAN.md)

技术学习路线见：

[docs/LEARNING_PATH.md](docs/LEARNING_PATH.md)

当前学习进度见：

[docs/LEARNING_PROGRESS.md](docs/LEARNING_PROGRESS.md)

## 许可证

当前项目用于个人学习、技术验证和作品展示，尚未发布正式开源许可证。
