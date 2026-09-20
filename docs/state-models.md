# 状态模型

## 连接状态

```text
Disconnected -> Connecting -> Connected
                    |             |
                    v             v
               Reconnecting <-----+
                    |
                    v
                 Connecting / Connected / Disconnected

Connected -> Stopping -> Disconnected
```

规则：

- 只有 `Disconnected -> Connecting` 可以主动开始连接。
- 连接失败或断线进入 `Reconnecting`，重连动作再进入 `Connecting`。
- 主动停止先进入 `Stopping`，资源释放完成后进入 `Disconnected`。
- 不允许重复进入相同状态，避免 UI 和日志产生重复事件。

## 采集状态

```text
Stopped -> Running -> Paused -> Running
   ^          |          |
   |          v          v
   +------ Stopped / Faulted
              |
              v
        Stopped / Running
```

规则：

- 只有 `Running` 或 `Paused` 可以暂停。
- 网络错误或数据源故障进入 `Faulted`。
- `Faulted` 恢复时需要显式进入 `Running` 或 `Stopped`。
- 采集状态与连接状态独立，连接断开时采集必须停止或进入故障态。

## 告警等级

- `Info`：信息事件，不需要操作。
- `Warning`：需要关注，允许确认。
- `Critical`：需要优先处理，通常伴随显著 UI 提示。

告警生命周期和确认/恢复状态机将在阶段 6 实现；当前只冻结等级定义。
