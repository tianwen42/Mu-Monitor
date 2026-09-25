# Mu-Monitor Protocol v1

## 1. 目标与范围

Protocol v1 是 Mu-Monitor 与设备、网关、`DeviceSimulator` 之间的正式 TCP 帧协议。它负责在字节流上界定消息边界，并通过 CRC16 检测传输错误。

协议层只定义帧结构和公共编解码规则，不负责业务告警判定、SQLite 持久化或 UI 状态。当前遥测帧的 `payload` 约定为 UTF-8 紧凑 JSON；帧协议本身允许其他二进制负载。

## 2. 字节序与基本类型

- 所有多字节整数均使用网络字节序，即大端序。
- `uint8` 为 1 字节无符号整数。
- `uint16` 为 2 字节无符号整数。
- `uint32` 为 4 字节无符号整数。
- `int64` 为 8 字节有符号整数。
- 设备 ID 使用固定 32 字节字段，内容为 UTF-8 字节，不足部分补 `0x00`。设备 ID 不能为空，不能包含 NUL 字节，UTF-8 编码后不能超过 32 字节。
- UTC 时间戳为 Unix epoch 起的毫秒数，使用有符号 `int64` 表示。

## 3. 帧结构

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 2 | Magic | 固定为 `0x4D55`，即 ASCII `MU` |
| 2 | 1 | Version | 协议版本，v1 固定为 `0x01` |
| 3 | 1 | Message Type | 消息类型 |
| 4 | 2 | Frame Length | 整帧长度，包含 Magic、所有头和 CRC |
| 6 | 4 | Sequence | 发送序号，从 0 开始，按 `uint32` 自然回绕 |
| 10 | 32 | Device ID | UTF-8，`0x00` 右填充 |
| 42 | 8 | UTC Timestamp | Unix epoch 毫秒 |
| 50 | N | Payload | 负载，长度可为 0 到 4096 字节 |
| 50 + N | 2 | CRC16 | 对偏移 0 到 `50 + N - 1` 计算 |

固定头部长度为 50 字节，CRC 长度为 2 字节，最短帧长度为 52 字节。

`Frame Length` 是包含 CRC 的总长度，因此：

```text
Frame Length = 50 + Payload Length + 2
```

## 4. 消息类型

| 值 | 名称 | 用途 |
| ---: | --- | --- |
| `0x01` | Telemetry | 遥测数据，当前 payload 为紧凑 JSON 对象 |
| `0x02` | Heartbeat | 心跳 |
| `0x03` | Command | 命令 |
| `0x04` | Response | 命令响应 |
| `0x7F` | Error | 错误响应 |

Protocol v1 解码器拒绝未定义的消息类型。新增类型必须通过协议版本升级或扩展兼容规则管理。

## 5. 最大长度

- 最大 payload：4096 字节。
- 最大总帧长：4148 字节，即 `50 + 4096 + 2`。
- `Frame Length` 字段为 `uint16`，但 v1 的实现上限固定为 4148 字节，防止恶意长度或异常数据导致过量内存占用。

## 6. CRC16

Protocol v1 使用 `CRC-16/CCITT-FALSE`：

| 参数 | 值 |
| --- | --- |
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| Input reflected | 否 |
| Output reflected | 否 |
| XorOut | `0x0000` |
| Check value（`"123456789"`） | `0x29B1` |

CRC 覆盖除 CRC 字段本身以外的整帧内容。CRC 本身按网络字节序写入。

## 7. 解码与流式处理

TCP 是字节流，接收端不能假设一次 `readyRead()` 对应一帧。实现必须处理：

- 单帧被拆成多次读取。
- 多次 `readyRead()` 产生多个完整帧。
- 一个读取缓冲区包含多帧。
- 无效 Magic、非法长度、未知版本、未知消息类型、非法设备 ID 和 CRC 错误。

`FrameDecoder` 使用内部缓冲区增量解析。只有收到完整且校验通过的帧时才向上层发出 `Frame`。完整头可用但 CRC 错误时，接收端报告协议错误并按声明长度丢弃当前帧；非法 Magic、非法长度或截断帧上下文，则从候选 Magic 和完整有效帧位置重新同步。TCP 连接关闭或测试结束时调用 `finish()`，未组成完整帧的剩余数据会作为截断错误上报。

## 8. DeviceSimulator 兼容模式

`DeviceSimulator` 默认发送 Protocol v1 二进制帧。通过 `DeviceSimulatorServer::setWireFormat()` 可切换：

- `WireFormat::ProtocolV1`：每个 JSON 对象作为 `Telemetry` 帧 payload，由 `FrameCodec` 编码后发送，不再附加换行符。
- `WireFormat::JsonLines`：每行一个紧凑 JSON 对象，以 `\n` 结束，不使用 v1 帧；仅作为兼容和诊断测试模式保留。

模拟器场景与协议注入：

| 场景 | 行为 |
| --- | --- |
| `Normal` | 正常遥测。 |
| `HighTemperature` | 温度超过 80 °C，并携带 `temperature_high` 告警。 |
| `HighPressure` | 压力超过 1.80 MPa，并携带 `pressure_high` 告警。 |
| `Offline` | 设备在线和采集状态均为 false，采集值归零。 |
| `BadCrc` | 编码合法 v1 帧后篡改 CRC 字段，用于验证客户端错误隔离。 |
| `ActiveDisconnect` | 主动断开当前全部客户端，用于验证客户端自动重连。 |

主程序数据源应优先使用 `TcpDeviceDataSource` 和 v1 二进制帧；JSON Lines 仅保留为兼容和诊断路径。

## 9. 示例

遥测 payload：

```json
{"alarm":"","collecting":true,"deviceId":"DEV-001","name":"模拟设备 001","online":true,"pressure":1.15,"speed":1370.0,"status":"normal","temperature":63.0,"timestamp":"2026-09-23T00:00:00.000Z","type":"telemetry","version":1,"voltage":220.0}
```

该 JSON 经 UTF-8 编码后成为 v1 `Telemetry` 帧 payload。`Frame` 外层的版本、序号、设备 ID 和 UTC 时间戳仍由协议字段承载，业务 JSON 中的同名字段不能替代帧头字段。

## 10. 错误处理建议

- CRC 错误：记录协议错误，丢弃当前帧，不将数据传给业务层。
- 未知版本：拒绝当前帧，记录收到的版本。
- 超长帧：记录长度并丢弃，不分配超过 `kMaxFrameSize` 的缓冲区。
- TCP 断开：由数据源进入重连状态；应用层不直接创建 socket。
- 序号不连续：当前 v1 不强制断线，只用于后续丢帧检测和诊断。

## 11. 源码入口

- `src/protocol/Protocol.h`：版本、消息类型和长度常量。
- `src/protocol/Frame.h`：帧数据结构。
- `src/protocol/CRC16.h`：CRC16 算法。
- `src/protocol/FrameCodec.h`：单帧编码和解码。
- `src/network/FrameDecoder.h`：TCP 流式拆包和粘连处理。
- `src/network/TcpConnectionWorker.h`：自有线程 socket 工作器。
- `src/network/TcpDeviceDataSource.h`：面向应用层的异步数据源接口。
