# GuLiStrike (UE 5.7) 项目规则

## 文件搜索禁令（重要）

本项目约 50GB，以下目录包含海量二进制/高频变动文件，**任何搜索工具都不得遍历**：

- `Intermediate/`（4.9G，构建产物，UE 运行时高频变动）
- `Saved/`（3.8G，日志与缓存）
- `DerivedDataCache/`
- `Binaries/`
- `Content/Assets/`（商城大资产，18G）
- `Downloads/`

具体要求：

1. 使用 Bash 的 `find` 时必须带 `-path` 排除参数，例如：
   `find . \( -name Intermediate -o -name Saved -o -name Binaries -o -name DerivedDataCache -o -name "Content/Assets" \) -prune -o -type f -name "*.cpp" -print`
2. 使用 `grep`/`rg` 时必须排除：`--glob '!Intermediate/**' --glob '!Saved/**' --glob '!Binaries/**' --glob '!Content/Assets/**'`（grep 用 `--exclude-dir`）
3. 永远不要读取 `.uasset`、`.umap`、`.uexp`、`.ubulk`、`.pak` 文件的内容（二进制，会撑爆上下文）。分析 UE 资产请用 `Scripts/` 下的 MCP 工具脚本。
4. 源代码只在 `Source/` 和 `Plugins/**/Source/` 里找；配置在 `Config/`；不要全盘搜索。

## 单位名称与路由

- **重防号**是指挥官兵种 `WM01`，`UnitTypeId=2`。肉鸽卡、导弹仓、Q 导弹、悬浮盘与该兵种模型任务沿此标识定位。
- 现有 `WarMachine` 类名、资源路径和历史源目录是稳定实现标识，保留引用；人类可读描述统一使用“重防号”。
- **地面机甲**是玩家 `Ground` 席位，使用独立的玩家角色与网络路径。`GuLiWarMachinePlaceholderPawn` 是该席位的早期占位类，不代表 WM01 兵种；不可将两个系统混路由。
