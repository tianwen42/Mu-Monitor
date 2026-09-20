# Mu-Monitor 架构说明

## 1. 项目边界

Mu-Monitor 是一个单机 Windows 工业设备监控终端。

当前目标规模：

- 1 到 20 台设备。
- TCP 自定义二进制协议。
- SQLite 本地持久化。
- 单进程 Qt Widgets 客户端。
- 独立 `DeviceSimulator.exe` 模拟设备和故障。
- 后续可以接入真实 TCP 设备，但不在本阶段扩展 Modbus、MQTT 和串口。

核心目标不是让所有代码都能复用，而是让下面三件事彼此隔离：

1. 数据从哪里来。
2. 数据如何解析、判断和处理。
3. 数据如何展示和保存。

---

## 2. 两种模拟数据源必须区分

项目中会有两种“模拟”能力，它们不是同一个东西。

### 2.1 进程内 SimulationDataSource

```text
Mu-Monitor.exe
└── SimulationDataSource
```

用途：

- 开发和调整 UI。
- 在没有网络时快速运行主程序。
- 测试 `MonitoringService`、`AppController` 和 UI 状态流。
- 为自动化测试提供确定性的数据。

它不负责：

- 真实 TCP。
- 粘包和拆包。
- CRC。
- 网络断开和自动重连。
- 多进程集成测试。

特点：

- 位于 `src/app` 或 `src/network` 的适配层。
- 实现 `IDeviceDataSource`。
- 随机种子必须可配置。
- 可以在测试中按固定时间生成固定数据。

### 2.2 独立进程 DeviceSimulator

```text
DeviceSimulator.exe
└── TcpDeviceServer
        |
        | TCP
        v
Mu-Monitor.exe
└── TcpDeviceDataSource
```

用途：

- 模拟真实设备或设备网关。
- 验证正式二进制协议。
- 验证请求、响应、心跳、断开和重连。
- 注入高温、高压、离线、延迟、坏 CRC 和重复帧。
- 作为项目的独立设备生产者。
- 作为发布前的端到端测试桩。

它不负责：

- 展示 Mu-Monitor 的界面。
- 访问 Mu-Monitor 的 SQLite。
- 链接 `DatabaseManager`。
- 直接调用主程序的 Widget 或业务对象。
- 共享主程序进程状态。

特点：

- 独立 CMake 目标。
- 独立可执行文件。
- 通过 TCP 与主程序交互。
- 使用共享的 `mu_protocol` 进行编码。
- 可以单独启动、停止和重启。

---

## 3. 当前真实架构

当前项目处于从界面原型向分层架构迁移的中间状态。

```text
main
├── LoginDialog
│    └── DatabaseManager
├── MainWindow
│    ├── TelemetryTableModel
│    ├── TrendChartWidget
│    ├── SettingsDialog -> ExcelExporter
│    ├── QTimer + QRandomGenerator
│    ├── 心跳和采集状态缓存
│    ├── 告警判断
│    └── DatabaseManager（UI 线程同步 SQLite）
└── DatabaseManager

DeviceSimulator.exe
├── DeviceSimulatorWindow
└── DeviceSimulatorServer -> QTcpServer / QTcpSocket
```

当前已经存在：

- `Device`、`TelemetrySample`、`BusinessStates` 等领域模型。
- `SimulationDataSource` 尚未接入。
- 独立模拟器程序。
- QtTest 测试和目标检查脚本。
- SQLite 登录、遥测、心跳、日志和 Excel 导出。

当前主要问题：

- `MainWindow` 仍然生成模拟数据、维护定时器、判断告警并访问数据库。
- 模拟器当前发送 JSON Lines，正式二进制协议尚未冻结。
- 主程序还没有 `TcpDeviceDataSource`。
- `DatabaseManager` 仍是同步单例，尚未变成线程化 Repository。
- `TelemetryRecord` 和 `TelemetrySample` 暂时并存，属于迁移期状态。
- `DeviceInfo` 和 `Device` 有部分字段重叠，后续需要明确配置模型与运行时模型边界。

---

## 4. 目标架构

```text
                         MainWindow
                    Qt Widgets / Model / Chart
                              |
                    命令、状态和展示数据
                              |
                       AppController
                    应用协调和依赖装配
                    /          |          \
                   /           |           \
                  v            v            v
      MonitoringService    AlarmEngine   TelemetryRepository
              |                 |               |
              |                 |               v
              |                 |         SqliteTelemetryRepository
              |                 |               |
              |                 |           SQLite 线程
              |                 |
              |                 v
              |            AlarmEvent
              |
              v
      IDeviceDataSource
        /              \
       v                v
SimulationDataSource  TcpDeviceDataSource
                          |
                    TcpConnectionWorker
                          |
                     FrameDecoder
                          |
                        TCP / QTcpSocket

DeviceSimulator.exe
  DeviceSimulatorWindow
      -> DeviceSimulationModel
      -> ScenarioEngine
      -> TcpDeviceServer
      -> FrameEncoder
```

---

## 5. 模块职责

### src/core

领域模型和纯业务规则，不依赖 Qt Widgets。

- `Device`
- `TelemetrySample`
- `MeasurementValue`
- `ConnectionState`
- `CollectionState`
- `AlarmSeverity`
- 后续 `AlarmEvent`

规则：

- 不使用 `QWidget`。
- 不访问数据库。
- 不打开网络连接。
- 不依赖 `MainWindow`。

### src/database

SQLite 持久化和数据访问。

目标职责：

- 实现 `TelemetryRepository`。
- 管理数据库连接和事务。
- 管理 schema migration。
- 批量写入遥测、心跳、日志和告警。
- 提供分页历史查询。
- 定期清理过期数据。

规则：

- 不依赖 UI。
- 不判断告警。
- 不解析 TCP。
- 一个 SQLite 连接只在其所属线程使用。

### src/network

网络传输、协议解析和数据源适配。

未来建议拆分为：

```text
src/network/
  IDeviceDataSource.h
  SimulationDataSource.*
  TcpDeviceDataSource.*
  TcpConnectionWorker.*
  FrameDecoder.*
```

规则：

- 不依赖 UI。
- 不直接写 SQLite。
- 只把完整的 `TelemetrySample`、连接状态和错误向上发出。

### src/protocol

主程序和模拟器共享的协议库。

```text
src/protocol/
  Protocol.h
  Frame.h
  FrameCodec.h
  FrameCodec.cpp
  Crc16.h
  Crc16.cpp
```

CMake 目标：

```text
mu_protocol
```

使用关系：

```text
Mu-Monitor
  -> mu_protocol

DeviceSimulator
  -> mu_protocol

ProtocolTest
  -> mu_protocol
```

规则：

- 编码和解码必须使用同一套协议定义。
- 所有多字节字段明确网络字节序。
- CRC16 有固定测试向量。
- 协议版本和最大帧长度必须在共享代码中校验。

### src/app

应用装配和业务协调。

目标类型：

- `AppController`
- `MonitoringService`
- 应用启动和依赖创建

`AppController` 职责：

- 接收 UI 命令。
- 调用 `MonitoringService`。
- 汇总连接、采集、告警和存储状态。
- 向 UI 发出展示状态。
- 创建和持有服务。

不负责：

- 生成设备数据。
- 解析协议。
- 直接执行 SQL。
- 绘制界面。

### src/alarm

告警规则和状态机。

目标类型：

- `AlarmRule`
- `AlarmEvent`
- `AlarmEngine`
- `AlarmState`

状态：

```text
Normal -> Active -> Acknowledged -> Cleared
```

告警事件由 Repository 保存，由 AppController 转发到 UI。

### src/ui

只负责展示和用户操作。

包含：

- `MainWindow`
- `LoginDialog`
- `SettingsDialog`
- `AboutDialog`
- `TelemetryTableModel`
- `TrendChartWidget`

规则：

- 不包含 `DatabaseManager.h`。
- 不包含 `QTcpSocket`。
- 不做阈值判断。
- 不生成模拟业务数据。
- 不持有业务定时器。

### tools/DeviceSimulator

独立设备生产者程序。

目标结构：

```text
DeviceSimulatorWindow
        |
DeviceSimulationModel
        |
ScenarioEngine
        |
TcpDeviceServer
        |
FrameEncoder
```

职责：

- 模拟 1 到 20 台设备。
- 按照协议发送遥测和心跳。
- 控制设备在线和离线。
- 注入高温、高压和通信故障。
- 记录客户端数量、发送帧数、发送速率和错误数。
- 支持固定随机种子。
- 支持拒绝或主动断开客户端，用于测试重连。

规则：

- 不依赖 Mu-Monitor。
- 不依赖 SQLite。
- 不依赖主程序 UI。
- 可以单独启动、关闭和重复运行。

---

## 6. 运行时线程模型

### GUI 线程

- `MainWindow`
- Model 和 Chart
- `AppController`
- 轻量状态更新

不能执行：

- 阻塞 Socket I/O。
- 大批量 SQLite 写入。
- 长时间 Excel 导出。

### 网络线程

- `TcpConnectionWorker`
- `QTcpSocket`
- 连接状态机
- 心跳超时
- 自动重连

解析可以放在网络线程，也可以放在独立解析线程；在 1 到 20 台设备规模下，优先让网络线程负责解析，减少线程数量。

### 数据库线程

- `SqliteTelemetryRepository`
- SQLite 连接
- 批量事务写入
- 历史查询
- 数据清理

SQLite 连接不能跨线程使用。

线程之间使用 Qt queued signal/slot 或线程安全队列通信。

---

## 7. 一条遥测数据的完整路径

```text
DeviceSimulator
-> FrameEncoder
-> TCP 字节流
-> TcpConnectionWorker
-> FrameDecoder
-> TelemetrySample
-> MonitoringService
-> AlarmEngine
-> TelemetryRepository
-> SQLite
-> AppController
-> MainWindow
```

具体步骤：

1. 模拟器为每台设备生成 `TelemetrySample`。
2. `FrameEncoder` 将样本编码为二进制帧。
3. `TcpDeviceServer` 发送字节流。
4. 主程序网络线程接收字节流。
5. `FrameDecoder` 处理粘包、拆包、CRC 和协议版本。
6. 解码成功后生成 `TelemetrySample`。
7. `MonitoringService` 更新设备状态。
8. `AlarmEngine` 根据规则产生或恢复告警。
9. `TelemetryRepository` 在数据库线程批量写入。
10. `AppController` 向 UI 发出最新状态。
11. UI 刷新设备列表、表格、趋势图和告警中心。

---

## 8. 真实设备替换路径

开发和演示阶段：

```text
DeviceSimulator.exe
-> TCP
-> Mu-Monitor
```

接入真实设备后：

```text
真实设备 / 工业网关
-> TCP
-> Mu-Monitor
```

只要真实设备遵守相同协议，主程序不需要修改 UI、数据库或告警逻辑。

如果真实设备使用 Modbus TCP，则新增：

```text
ModbusTcpDeviceDataSource
```

并让它实现相同的 `IDeviceDataSource` 边界。

---

## 9. 演进顺序

```text
1. 保存并通过当前稳定基线
2. 提取 SimulationDataSource
3. 定义 IDeviceDataSource 和 MonitoringService
4. 建立 AppController
5. 冻结协议 v1
6. 建立 mu_protocol 共享库
7. 升级 DeviceSimulator 使用 FrameEncoder
8. 实现 TcpDeviceDataSource 和 FrameDecoder
9. 实现自动重连和故障注入
10. 迁移到 TelemetryRepository 和数据库线程
11. 实现 AlarmEngine
12. 完成长时间集成测试和演示材料
```

---

## 10. 架构约束

后续开发必须遵守：

- UI 不直接访问网络和数据库。
- DeviceSimulator 不链接主程序的业务和存储代码。
- 模拟器和客户端必须使用同一个协议库。
- 领域状态不能继续用中文界面字符串表示。
- 线程间通信必须有明确边界。
- 高频网络数据必须经过有界队列或受控信号链。
- SQLite 固定使用本地文件，不引入服务端数据库。
- 所有异常路径都必须有日志、指标或可重复测试。