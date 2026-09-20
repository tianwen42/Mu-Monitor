# 第三方依赖版本

## QXlsx

- 版本：`1.5.1`
- 来源：<https://github.com/QtExcel/QXlsx>
- 许可证：MIT
- 用途：生成 `.xlsx` 导出文件
- 引入位置：`third_party/QXlsx`

升级步骤：

1. 获取所需 QXlsx 标签或提交，并记录版本号。
2. 替换 `third_party/QXlsx`，不要修改其公共头文件。
3. 更新本文件中的版本与来源。
4. 执行 `scripts/check.ps1 -Clean`。
5. 执行 `scripts/build_and_deploy.ps1`，确认导出和发布包仍可用。
