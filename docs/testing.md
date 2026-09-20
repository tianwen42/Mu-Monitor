# 测试说明

## 运行方式

Debug 构建并运行全部测试：

```powershell
.\scripts\check.ps1 -Clean
```

Release 构建并运行全部测试：

```powershell
.\scripts\check.ps1 -Configuration Release -Clean
```

也可以只运行已有构建目录：

```powershell
ctest --test-dir build/check-debug --output-on-failure
```

## 当前测试分层

- `CoreModelsTest`：设备模型、遥测样本、状态名称、状态转换和无效值。
- `TimeUtilsTest`：UTC/本地时间转换、ISO 8601 解析和无效输入。
- `TelemetryTableModelTest`：设备记录插入、更新、格式化列和状态颜色。
- `DatabaseManagerTest`：SQLite 初始化、用户、遥测、心跳和日志。
- `ExcelExporterTest`：XLSX 导出结果。
- `DeviceSimulatorServerTest`：真实 TCP 监听、客户端连接、JSON Lines 和高温场景。
- `MainWindowResponsiveTest`：主窗口缩放、KPI 自动重排和趋势/告警布局。

## 编写原则

- 领域逻辑优先与 Qt Widgets 解耦，测试不应依赖主窗口。
- 测试必须覆盖正常路径和至少一个错误路径。
- 网络和数据库测试只能使用临时端口或临时数据库，不能污染用户数据。
- 每个阶段至少增加一个可重复运行的测试，并在 `ctest` 中注册。



## 2026-09-20 回归结果

统一检查脚本 `scripts/check.ps1 -Configuration Release -Clean` 已从空构建目录执行成功：

| 测试目标 | 业务用例 | 主要覆盖 | Release | Debug |
| --- | ---: | --- | --- | --- |
| `CoreModelsTest` | 4 | 设备、遥测样本、状态名称和状态转换 | 通过 | 通过 |
| `TimeUtilsTest` | 4 | UTC、ISO 8601、文件时间戳和无效输入 | 通过 | 通过 |
| `TelemetryTableModelTest` | 3 | 插入、更新、列格式化、状态颜色 | 通过 | 通过 |
| `DatabaseManagerTest` | 8 | 用户会话、遥测、时间范围、心跳、日志和持久化 | 通过 | 通过 |
| `ExcelExporterTest` | 3 | 正常导出、空路径和空工作簿 | 通过 | 通过 |
| `DeviceSimulatorServerTest` | 7 | 真实 TCP、JSON Lines、场景、客户端计数和端口冲突 | 通过 | 通过 |
| `MainWindowResponsiveTest` | 1 | 主窗口缩放、KPI 重排、趋势/告警布局 | 通过 | 通过 |

结果：Debug 和 Release 均为 7/7 测试目标通过，共 30 个业务用例。Release 一次干净检查构建包含全部主程序和测试目标；Debug 回归同样通过。

`MainWindowResponsiveTest` 支持设置 `MU_RESPONSIVE_SCREENSHOT_DIR`，在测试完成后输出宽屏和紧凑布局截图，便于人工检查裁切与重叠。

发布链路 `scripts/build_and_deploy.ps1` 同时通过，`windeployqt` 部署和运行库裁剪后的 `dist` 总计约 35.5 MB。

### 本轮发现并修复

1. `DatabaseManager::latestDeviceRecords()` 在“同一设备同一时间戳存在多条记录”时返回重复设备。
   已增加回归用例 `latestRecordReturnsOneWhenTimestampsTie`，并将查询改为按 `recorded_at DESC, rowid DESC` 选择唯一最新记录。
2. `TelemetryTableModelTest` 原先把 `61.25` 的一位小数格式化结果误写为 `61.2`。
   实际 Qt 结果四舍五入为 `61.3`，测试断言已修正。

### 尚未覆盖

- 登录窗口、托盘菜单、Dock 拖动和设置对话框的真实 GUI 交互。
- TCP 客户端、协议解码、重连和粘包拆包，因为对应功能尚未实现。
- 8/24 小时稳定性、磁盘写满、数据库损坏和真实硬件联调。
