# Mu-Monitor 技术学习路线

> 本路线和 `TODOLIST.md` 对齐。  
> 2026-10-15 是目标日期，不是硬截止日期。  
> 学习目标是能够解释设计、阅读代码、运行测试并独立修改，而不是只完成功能。

---

## 使用方法

每个学习单元按照同一流程执行：

```text
先理解概念
-> 阅读对应文件
-> 运行或编写测试
-> 自己做一个小修改
-> 用自己的话解释
-> 再进入下一个单元
```

不要一次性阅读整个项目。每次只学习一个主题，追踪一条数据流。

---

# L0：C++ 与 CMake 基础

## 学习目标

理解一个 Qt C++ 项目如何组织、编译和测试。

## 核心知识

- 头文件 `.h` 与实现文件 `.cpp` 的职责。
- `#pragma once` 和重复包含保护。
- 值类型、引用、常量引用和对象复制。
- `const` 成员函数。
- `enum class` 和普通枚举的区别。
- RAII：资源在对象构造时获取、析构时释放。
- 栈对象和堆对象。
- `new` 创建对象后的所有权。
- Qt 父子对象为什么可以自动释放子对象。
- 编译、链接和运行时的区别。
- 静态库和可执行目标。

## 对应文件

```text
CMakeLists.txt
src/core/*.h
src/core/*.cpp
tests/CMakeLists.txt
```

## 动手任务

- 找到 `TelemetrySample` 的声明和实现。
- 增加一个无副作用的校验函数。
- 为它写一个 QtTest。
- 执行 Debug 构建和测试。
- 解释“未定义引用”和“编译错误”的区别。

## 验收问题

- 为什么头文件里通常只放声明？
- 为什么成员函数可以加 `const`？
- 什么情况下需要智能指针？
- Qt 父子对象和 `std::unique_ptr` 解决的是不是同一问题？

---

# L1：Qt 对象模型、信号槽和定时器

## 学习目标

掌握 Qt 最重要的事件驱动编程方式。

## 核心知识

- `QObject` 的对象树。
- `parent` 与子对象生命周期。
- signal 和 slot。
- `connect` 的连接类型。
- `Qt::AutoConnection`、`Qt::DirectConnection` 和 `Qt::QueuedConnection`。
- lambda 捕获中 `this` 的生命周期风险。
- `QTimer` 和事件循环。
- 定时器必须在拥有事件循环的线程中工作。
- `deleteLater()` 和直接 `delete` 的区别。

## 对应文件

```text
src/ui/mainwindow.*
src/network/SimulationDataSource.*
src/network/TcpConnectionWorker.*
src/app/MonitoringService.*
src/app/AppController.*
```

## 动手任务

- 画出 MainWindow 到 MonitoringService 的信号流。
- 增加一个状态变化信号并连接测试。
- 验证停止后定时器不会继续触发。
- 使用 `QSignalSpy` 检查信号次数。

## 验收问题

- 信号槽比直接函数调用多解决了什么问题？
- 为什么关闭窗口后仍可能有定时器继续运行？
- 跨线程信号默认使用什么连接方式？
- 为什么 Worker 对象不能属于错误线程？

---

# L2：应用分层和职责边界

## 学习目标

理解什么是可维护的架构，而不是把所有逻辑写进 MainWindow。

## 核心知识

- UI、Application、Domain、Infrastructure 的职责。
- 依赖方向和依赖倒置。
- 接口隔离。
- 组合根和依赖装配。
- 领域对象和 UI 字符串的区别。
- Controller 只协调，不生成数据。
- Service 管业务，不做界面。
- Repository 管存储，不判断告警。
- DataSource 管设备数据来源，不管 UI。

## 目标依赖

```text
MainWindow
-> AppController
-> MonitoringService
-> IDeviceDataSource
```

## 对应文件

```text
src/app/AppController.*
src/app/MonitoringService.*
src/network/IDeviceDataSource.h
src/network/SimulationDataSource.*
src/ui/mainwindow.*
```

## 动手任务

- 在 MainWindow 中搜索 `QRandomGenerator` 和业务 QTimer。
- 把其中一个状态字段移到 MonitoringService。
- 绘制重构前后的依赖图。
- 更换数据源时确认 UI 不需要修改。

## 验收问题

- 为什么 UI 不应该直接访问 SQLite？
- Controller 和 Service 的区别是什么？
- 为什么接口应该由高层定义？
- 如果 SimulationDataSource 换成 TcpDeviceDataSource，哪些文件不应变化？

---

# L3：TCP 字节流、协议帧和 CRC

## 学习目标

能够独立解释和实现 TCP 粘包、拆包和帧校验。

## 核心知识

- TCP 是连续字节流，不保留消息边界。
- 一次 `readyRead()` 可能包含半帧、一帧或多帧。
- 帧头 Magic 的作用。
- 版本和消息类型。
- 长度字段为什么是协议核心。
- 序号和重复帧。
- UTC 时间戳。
- 主机字节序与网络字节序。
- CRC16 校验范围。
- 最大帧长度限制。
- 增量解析和内部缓冲区。
- 错误帧后如何重新同步。

## 目标协议

```text
Magic
-> Version
-> MessageType
-> PayloadLength
-> Sequence
-> DeviceId
-> Timestamp
-> Payload
-> CRC16
```

## 对应文件

```text
src/protocol/Protocol.*
src/protocol/Frame.*
src/protocol/FrameCodec.*
src/protocol/CRC16.*
src/network/FrameDecoder.*
tests/ProtocolCodecTest.cpp
docs/protocol.md
```

## 动手任务

- 手工编码一帧并计算 CRC。
- 把一帧拆成三次喂给 FrameDecoder。
- 把三帧拼在一起交给 FrameDecoder。
- 修改一个字节验证坏 CRC。
- 画出解码状态循环。

## 验收问题

- TCP 为什么会粘包和拆包？
- 只有 CRC 没有长度字段可以吗？
- 长度字段为什么必须限制最大值？
- 收到坏帧后为什么不能直接丢掉全部缓冲区？

---

# L4：Qt 线程和连接生命周期

## 学习目标

掌握 Socket 所在线程、对象移动、重连和资源释放。

## 核心知识

- GUI 线程与事件循环。
- `QThread` 和 `moveToThread()`。
- QObject 的线程归属不能随意修改。
- `QTcpSocket` 为什么要在所属线程操作。
- queued connection 的线程边界。
- 连接状态机。
- 连接超时、读写超时和心跳超时。
- 指数退避。
- 随机抖动避免重连风暴。
- 主动断开和网络异常断开的区别。
- 停止线程时如何安全释放资源。

## 状态机

```text
Disconnected
-> Connecting
-> Connected
-> Reconnecting
-> Connecting
```

## 对应文件

```text
src/network/TcpConnectionWorker.*
src/network/TcpDeviceDataSource.*
tests/TcpDeviceDataSourceTest.cpp
tools/DeviceSimulator/**
```

## 动手任务

- 停止模拟器，观察连接状态变化。
- 记录重连时间。
- 修改退避参数并说明影响。
- 主动关闭客户端，确认 Worker 正常退出。
- 使用 AddressSanitizer 或调试器检查对象生命周期。

## 验收问题

- 为什么 `QThread` 对象本身不一定运行在线程里？
- 为什么不能从 GUI 线程直接调用 Socket 阻塞读取？
- 固定重连间隔会有什么问题？
- 应用退出时如何避免 Socket 和 Worker 泄漏？

---

# L5：SQLite 持久化与数据保护

## 学习目标

理解数据库连接、事务、迁移、备份和长期数据安全。

## 核心知识

- SQLite 和普通服务器数据库的区别。
- `QSqlDatabase` 的线程归属。
- 一个连接只在一个线程使用。
- prepared statement 和 SQL 注入。
- 事务和批量写入。
- WAL、`busy_timeout` 和 `synchronous`。
- 索引与查询计划。
- schema version 和 migration。
- 迁移前备份。
- `quick_check` 和数据损坏处理。
- 标准模式与便携模式的数据目录。
- 旧数据库复制迁移。
- 自动清理与历史数据保留。

## 目录策略

```text
--data-dir
-> MU_MONITOR_DATA_DIR
-> portable.flag
-> AppLocalDataLocation
```

## 对应文件

```text
src/database/DataDirectory.*
src/database/DatabaseManager.*
src/database/*Repository.*
tests/DataDirectoryTest.cpp
tests/DatabaseManagerTest.cpp
docs/DATABASE_DESIGN.md
```

## 动手任务

- 删除临时数据库，观察自动初始化。
- 制造 schema 变化，验证迁移前备份。
- 开启 WAL 并观察 `.db-wal` 文件。
- 执行按设备和时间范围查询。
- 测试目录只读和数据库损坏场景。

## 验收问题

- 为什么数据库连接不能跨线程复用？
- 为什么一条数据一次提交事务性能差？
- 为什么自增主键不是设备 ID？
- 为什么迁移失败不能直接删除数据库重来？

---

# L6：用户、认证、角色和审计

## 学习目标

理解一个本地桌面系统的完整用户管理设计。

## 核心知识

- 用户、认证、授权和审计的区别。
- 密码不能明文保存。
- 盐和 PBKDF2 的作用。
- 密码算法版本和参数升级。
- 角色和权限的映射。
- 最后一个管理员保护。
- 当前用户不能禁用自己。
- 修改密码后撤销其他会话。
- 登录失败锁定。
- QSettings 中保存令牌的风险。
- 危险操作审计。

## 角色模型

```text
admin
operator
viewer
```

## 对应文件

```text
src/auth/PasswordService.*
src/auth/AuthenticationService.*
src/auth/UserManagementService.*
src/auth/AuditRepository.*
src/database/UserRepository.*
src/ui/UserManagementDialog.*
docs/USER_MANAGEMENT.md
```

## 动手任务

- 创建 operator 用户并验证权限。
- 禁用用户后验证不能登录。
- 尝试禁用最后一个管理员。
- 修改密码后验证旧会话被撤销。
- 检查审计日志是否记录关键操作。

## 验收问题

- 哈希和加密有什么区别？
- 为什么每个用户需要独立盐？
- 角色和权限为什么要分开？
- 审计日志为什么不记录密码和原始令牌？

---

# L7：告警引擎和真实数据 UI

## 学习目标

把界面消息升级为可确认、可恢复的领域事件。

## 核心知识

- 阈值规则。
- 持续时间过滤。
- 上升沿和下降沿。
- 回差和抖动过滤。
- 去重。
- 告警状态机。
- 告警确认和恢复的区别。
- 告警事件持久化。
- Model/View 刷新策略。
- UI 刷新节流。
- 历史数据分页。

## 告警状态

```text
Normal
-> Active
-> Acknowledged
-> Cleared
-> Normal
```

## 对应文件

```text
src/alarm/**
src/app/MonitoringService.*
src/ui/mainwindow.*
src/ui/TelemetryTableModel.*
tests/AlarmEngineTest.cpp
```

## 动手任务

- 注入一次高温并观察状态转换。
- 重复注入故障，确认不会产生重复事件。
- 确认告警并关闭故障。
- 查询数据库中的告警事件。

## 验收问题

- 为什么告警不能用每个采样周期一条日志表示？
- 回差和持续时间分别解决什么问题？
- 告警确认是否等于告警恢复？

---

# L8：测试、性能和发布

## 学习目标

证明程序在正常和异常情况下都可以稳定运行。

## 核心知识

- 单元测试、集成测试和端到端测试。
- 测试替身。
- 固定时间和随机种子。
- 故障注入。
- 日志分级和滚动。
- CPU、内存、队列和数据库增长指标。
- 长时间稳定性测试。
- `windeployqt`。
- 发布包和数据库升级。

## 对应命令

```powershell
.\scripts\check.ps1 -Configuration Debug
.\scripts\check.ps1 -Configuration Release
```

## 动手任务

- 运行全部测试。
- 关闭模拟器并检查重连。
- 注入坏 CRC 并检查统计。
- 连续运行 2 小时。
- 记录内存和数据库增长。
- 从干净目录启动便携版本。

## 验收问题

- 为什么测试不应依赖真实等待时间？
- 为什么最终还要做人工和集成验收？
- 如何证明 UI 没有被网络和数据库阻塞？
- 如何验证升级不会丢失历史数据？

---

# 推荐学习顺序

```text
L0 C++ 和 CMake
-> L1 QObject、信号槽和定时器
-> L2 应用分层
-> L3 TCP 和协议
-> L4 线程和连接生命周期
-> L5 SQLite
-> L6 用户和权限
-> L7 告警和 UI
-> L8 测试、性能和发布
```

每完成一个 L 单元，都应能做到：

```text
能读懂相关代码
能运行相关测试
能解释关键设计
能自己修改一个小功能
能说明失败场景
```