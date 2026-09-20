# C++ 编码约定

## 命名

- 类型使用 `PascalCase`，函数和局部变量使用 `camelCase`。
- 成员变量使用 `m_` 前缀，常量使用 `k` 前缀。
- Qt 信号使用过去式或状态语义，例如 `runningChanged`、`clientCountChanged`。
- 文件名与主要类型一致，头文件与实现文件成对维护。

## 文件组织

- 领域模型与接口放在 `src/core`，禁止依赖 Qt Widgets。
- 网络、数据库、告警、UI 和工具代码分别放入对应目录。
- 新模块必须加入 CMake，且能被独立测试目标引用。
- 第三方源码只放在 `third_party`，项目代码不得修改第三方头文件。

## 错误处理与日志

- 公共函数通过返回值表示失败，并使用可选的 `QString *errorMessage` 返回诊断信息。
- 不允许静默忽略数据库、网络或文件系统错误。
- 日志必须包含模块和关键上下文，禁止记录密码、令牌或敏感数据。
- UI 可以显示简短错误，详细信息应写入可查询的日志。

## 质量门槛

- 新代码默认启用 `-Wall -Wextra -Wpedantic`。
- 提交前执行 `scripts/check.ps1`。
- 关键逻辑必须有 QtTest；暂时无法测试时，在提交说明中记录原因。
- Git 提交信息遵循 `docs/COMMIT_CONVENTION.md`。
