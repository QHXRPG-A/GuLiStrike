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
