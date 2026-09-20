# Git 提交信息规范

项目统一使用 emoji + type + 描述 的提交格式：

```text
<emoji> <type>: <description>
```

示例：

```text
✨ feat: add device alarm state machine
🐛 fix: prevent duplicate tray exit action
💄 style: refine monitoring dashboard layout
📝 docs: document commit message convention
🎨 refactor: split network and protocol modules
```

## 类型映射

| Emoji | Type | 用途 |
|---|---|---|
| ✨ | feat | 引入新功能 |
| 🐛 | fix | 修复 bug |
| 💄 | style | 更新 UI 样式 |
| 🥚 | format | 格式化代码 |
| 📝 | docs | 添加或更新文档 |
| 👌 | perf | 提高性能或优化 |
| 🎉 | init | 初次提交或初始化项目 |
| ✅ | test | 增加测试代码 |
| 🎨 | refactor | 改进代码结构 |
| 🚑 | patch | 添加重要补丁 |
| 📦 | file | 添加新文件 |
| 🚀 | publish | 发布新版本 |
| 📌 | tag | 发布版本或添加标签 |
| 🔧 | config | 修改配置文件 |
| 🙈 | git | 添加或修改 .gitignore |

## 项目示例

```text
🎉 init: complete stage 0 project baseline
🔧 config: add clean build and deploy workflow
✨ feat: simplify docks and add 100 simulated devices
✨ feat: add SQLite login with default admin
✨ feat: minimize to system tray on close
🐛 fix: remove toolbar exit action
✨ feat: show current account and role
✨ feat: remember login for 30 days
📝 docs: document commit message convention
```