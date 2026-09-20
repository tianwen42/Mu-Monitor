# Mu-Monitor

工业设备监控与告警桌面应用，基于 Qt 6 和 C++ 开发。

当前阶段：**阶段 0 项目基线完成，阶段 1 界面 MVP 进行中**。

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
- QSS 深色工业主题
- CMake 构建和 MinGW 部署

## 关闭与系统托盘

点击主窗口关闭按钮时，程序不会退出，而是隐藏到 Windows 系统托盘。

托盘菜单提供：

- 显示主界面
- 连接/断开设备
- 开始/暂停采集
- 退出 Mu-Monitor

单击或双击托盘图标也可以恢复主窗口。

只有以下操作会真正退出程序：

- 托盘菜单中的“退出 Mu-Monitor”
- 顶部工具栏中的“退出”

如果系统托盘不可用，关闭按钮会按普通方式退出程序。
## 默认账号与数据库

首次启动会自动创建 SQLite 数据库，并创建默认管理员账号：

```text
用户名：admin
密码：123456
```

数据库位置：

```text
%APPDATA%\Mu-Monitor\mu-monitor.db
```

密码不会以明文保存，数据库中使用带随机盐、10 万轮迭代的 SHA-256 哈希。

> 默认密码仅用于开发阶段，后续必须增加修改密码功能。

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

1. 检查 Mu-Monitor 是否正在运行
2. 清除 `build\script-release`
3. 从零执行 CMake Release 配置
4. 编译 Mu-Monitor
5. 清除旧的 `dist`
6. 复制新的 exe
7. 执行 `windeployqt`
8. 输出最终 exe 和 dist 大小
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

详细任务和验收标准见：

[docs/TODOLIST.md](docs/TODOLIST.md)

## 许可证

当前项目用于个人学习、技术验证和作品展示，尚未发布正式开源许可证。
