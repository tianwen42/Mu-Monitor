# Git 提交信息规范

项目统一使用 `emoji + type + 中文描述` 的提交格式。描述部分必须使用中文，type 保持英文约定：

```text
<emoji> <type>: <中文描述>
```

示例：

```text
✨ feat: 添加设备告警状态机
🐛 fix: 防止托盘退出操作重复创建
💄 style: 优化监控首页布局
📝 docs: 记录提交信息规范
🎨 refactor: 拆分网络与协议模块
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
🎉 init: 完成阶段零项目基线
🔧 config: 添加清理构建与部署流程
✨ feat: 简化停靠面板并添加一百台模拟设备
✨ feat: 添加 SQLite 登录与默认管理员
✨ feat: 关闭窗口时最小化到系统托盘
🐛 fix: 移除工具栏退出操作
✨ feat: 显示当前账号与角色
✨ feat: 支持三十天免登录
📝 docs: 记录提交信息规范
```