# Mu-Monitor 学习进度

> 每次只学习一个课程。完成内容阅读、代码观察、动手练习和验收问题后，才能把状态改为“已掌握”。

## 总进度

| 课程 | 主题 | 状态 |
|---|---|---|
| L0 | C++、编译流程和 CMake | 学习中 |
| L1 | QObject、信号槽和 QTimer | 未开始 |
| L2 | 应用分层和职责边界 | 未开始 |
| L3 | TCP、协议帧和 CRC | 未开始 |
| L4 | Qt 线程和连接生命周期 | 未开始 |
| L5 | SQLite 持久化和数据保护 | 未开始 |
| L6 | 用户、认证、角色和审计 | 未开始 |
| L7 | 告警引擎和真实数据 UI | 未开始 |
| L8 | 测试、性能和发布 | 未开始 |

## L0：C++、编译流程和 CMake

### 目标

理解一个 `.cpp` 文件如何经过编译和链接，最终成为 `Mu-Monitor.exe`。

### 阅读文件

```text
CMakeLists.txt
src/main.cpp
src/core/TelemetryRecord.h
src/core/TelemetrySample.h
tests/TimeUtilsTest.cpp
```

### 学习检查

- [ ] 能解释预处理、编译、链接三个阶段。
- [ ] 能解释 `.h` 和 `.cpp` 的区别。
- [ ] 能解释 CMake 的作用。
- [ ] 能解释目标、源文件和依赖库。
- [ ] 能解释 Qt Widgets、Sql、Network 模块的作用。
- [ ] 能说清 `main()` 做了哪些事。

### 动手任务

- [ ] 找到 `QApplication` 的创建位置。
- [ ] 找到数据库初始化位置。
- [ ] 找到登录窗口和主窗口的创建位置。
- [ ] 执行一次 Debug 构建。
- [ ] 执行一次 `TimeUtilsTest`。
- [ ] 用自己的话说出构建成功后生成了哪些文件。

### 验收问题

1. 为什么 `.h` 文件里通常不写函数实现？
2. 为什么源文件修改后不一定需要重新编译整个项目？
3. CMake 和编译器、链接器分别是什么关系？
4. 为什么 Qt 项目需要在 `main()` 中创建 `QApplication`？
5. `QApplication::exec()` 为什么不会立即返回？