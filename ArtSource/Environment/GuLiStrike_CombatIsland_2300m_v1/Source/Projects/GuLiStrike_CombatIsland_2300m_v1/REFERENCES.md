# 提示词与工程参考

检索日期：2026-10-01。提示词和战斗布局依据用户确认的计划独立编写；以下资料用于节点组织、输入位深、相对路径和导出格式的核对。

1. [AndrewAltimit · Gaea2 MCP Quick Reference](https://github.com/AndrewAltimit/template-repo/blob/main/tools/mcp/mcp_gaea2/docs/GAEA2_QUICK_REFERENCE.md)
   - 阅读时文件 SHA：`2a9220befb496e8318f2f544bcbab0067e73c740`。
   - 参考显式生成器、侵蚀、Export 的工作流组织。其另一套 MCP 的接口名称不用于本工程。
2. [aguai2018 · FILE_FORMAT.md](https://github.com/aguai2018/gaea-mcp/blob/main/skill/gaea-terrain/reference/FILE_FORMAT.md)
   - 文件 SHA：`fad8fcbd79ad380c27dc2e69c7e034e1e58be4a6`。
   - 参考 Terrain.Width/Height 的物理含义、PNG16/RAW16 格式与 Newtonsoft 引用结构。
3. [aguai2018 · NODES.md](https://github.com/aguai2018/gaea-mcp/blob/main/skill/gaea-terrain/reference/NODES.md)
   - 文件 SHA：`2843f014dc5752346af1f07d3c9b4e2e4369fbdd`。
   - 参考 File 的相对路径、16 位灰度遮罩及 Export 参数。节点最终以本机实际构建和数据验收为准。
4. QuadSpinner 官方：[File](https://docs.gaea.app/reference/nodes/primitive/file.html)、[Combine](https://docs.gaea.app/reference/nodes/utility/combine.html)、[Island](https://docs.gaea.app/reference/nodes/terrain/island.html)。
   - 参考数据输入、空间合成与岛形制作。宏观布局采用可复现源遮罩，细节通过 Gaea 节点生成。
5. 本机 Gaea `Examples/` 内的 `Canyon River with Sea.terrain`、`Mesa.terrain`、`Cartography - 3D Map.terrain`。
   - 核对实际原生参数及双输入 Combine 的端口结构；未复制示例地形或整体节点图。

本机已安装的 `yushimatenjin/gaea-mcp` 用于工程操作和 Swarm 构建；无需安装上述其他 MCP。Gaea 的颜色空间沿用本机接受的 `sRGB` 枚举，高度与遮罩的数值精度通过实际 PNG 检查。
