# HUDuiPRO 资源入口

**本轮状态：已完成。** 完整动效、独立组件动画、AE 源清理和会话临时文件清理均已完成，当前没有待执行的导入或清理任务。实际播放效果仍由用户按需核验。

本包已导入 UE：83 张透明静态纹理、3 组完整动效，以及 81 个可独立调用的组件动画。资源统一在 `/Game/Assets/UI/HUDuiPRO`。

## 独立组件动画

入口：`/Game/Assets/UI/HUDuiPRO/Animations`。完整的组件名称、尺寸、时长和目录见[独立组件调用索引](Components20261030/Catalog.md)，全部对象路径见[清单](Components20261030/manifest.json)。

81 个组件按原分类保存，每个目录都有以下 5 个资源，共 405 个新增 UE 对象：

| 前缀 | 用途 |
|---|---|
| `IMS_HUDC_` | 项目相对路径的透明 PNG 动画序列媒体源 |
| `MP_HUDC_` | 独立播放器，打开即播、循环 |
| `MPL_HUDC_` | 仅引用该组件媒体源的播放列表 |
| `MT_HUDC_` | 已绑定对应播放器的媒体纹理 |
| `M_UI_HUDC_` | 透明 UI 材质，RGB 接颜色、Alpha 接透明度 |

使用时，将对应 `M_UI_HUDC_` 材质设为 UMG Image 的 Brush Material，再对该组件的 `MP_HUDC_` 调用 `Open Source`，传入同目录的 `IMS_HUDC_`。停止使用或 Widget 移除时调用 `Close`；其他组件有各自的播放器。暂停用 `Pause`，从头播放可重新打开媒体源。

同一播放器的多个显示实例共享进度；同一个组件需要不同播放进度时，分别配置播放器、媒体纹理和绑定各自纹理的材质。动画数字和文字来自 AE 烘焙；玩法数据绑定可另接 UMG 文本层。

组件动画保留原生尺寸、24 fps 和完整时长，共 111,426 帧 RGBA8 PNG，约 12.08 GiB。序列位于 `Content/Movies/HUDuiPRO/Components`。3 个全局颜色控制合成不作为可播放组件导出。

## 其他资源

| UE 目录 | 内容 |
|---|---|
| `Textures/Final` | 4K 与 1080p 完整驾驶舱静态纹理 |
| `Textures/Others` | 独立仪表、准星、圆环、进度条、信息框、地图与文字静态纹理 |
| `Media` | 三组完整动效的媒体源、播放器、播放列表和媒体纹理 |
| `Materials` | 三个完整动态 UI 材质及驾驶舱静态 UI 材质 |
| `Source` | 原迁入的 earth4 UE 纹理；AE 工程及引用的原始素材已清理 |

83 张静态纹理取对应合成第 5 秒，保留原生尺寸和 Alpha，使用 UI 分组、UserInterface2D 压缩、无 Mip、sRGB、边缘 Clamp。整屏静态材质为 `/Game/Assets/UI/HUDuiPRO/Materials/M_UI_HUD_Cockpit2_Still`。

| 完整动效 | 帧率、尺寸与时长 | 媒体源 / UI 材质 |
|---|---|---|
| 驾驶舱 | 24 fps，1920×1080，50 秒 / 1200 帧 | `IMS_HUD_Cockpit2_HD` / `M_UI_HUD_Cockpit2_HD` |
| 直升机 | 24 fps，1920×725，101 帧 | `IMS_HUD_Helicopter` / `M_UI_HUD_Helicopter` |
| 卫星 | 24 fps，1171×1080，101 帧 | `IMS_HUD_Satellite` / `M_UI_HUD_Satellite` |

完整动效的播放器和媒体源在 `Media`，材质在 `Materials`；同样通过匹配的 MP 打开 IMS，并在停止使用时关闭。三组序列位于 `Content/Movies/HUDuiPRO/<序列名>`，共约 2.56 GB。

## 文件与运行依赖

依赖现有 ImgMedia、MediaAssets。运行时由媒体源读取 `Content/Movies/HUDuiPRO` 下的 PNG；这些帧无需逐张导成 Texture2D。当前编辑器及 `Config/DefaultEditorPerProjectUserSettings.ini` 已为 `Movies/HUDuiPRO/*` 与 `Assets/UI/HUDuiPRO/Source/*` 添加自动导入排除，保留其他自动导入规则。

打包前需将 `Movies/HUDuiPRO` 作为额外非资产目录暂存，并确认目标平台 ImgMedia 支持。本次未运行打包或播放测试。

按用户 2026-10-30 的指示，全部入库后已删除原始 AEP、AE 2025 适配 AEP、批渲染 AEP、两个原始 MOV 及原始 earth4 PNG，共 6 个文件。UE 对象、动画 PNG、静态纹理重导入源和调用索引保留。旧 earth4 纹理的重导入源已改到 `ArtSource/UI/HUDuiPRO/Exports/Legacy/earth4.png`，复制前后 SHA-256 相同。

- 静态纹理源：[Exports/Textures](Exports/Textures)；旧 earth4 重导入源：[Exports/Legacy/earth4.png](Exports/Legacy/earth4.png)。
- 首次入库记录：[Import20261030](Import20261030)。
- 本次交付与清理记录：[delivery-summary.json](Components20261030/delivery-summary.json)、[cleanup-report.json](Components20261030/cleanup-report.json)。
- 字体及 OFL 许可：[Fonts](Fonts)。导出时使用 Orbitron / Rajdhani 七个字体，将 Rajdhani-Semibold 对齐 Rajdhani-SemiBold，缺失的 LetterGothicStd 使用已安装的 Consolas；运行烘焙动画无需 AE 或这些字体。
- 制作与核对脚本：`Scripts/Art/HUDuiPRO`。源工程的 567 条表达式已在首次入库时完成中文属性名适配；本轮沿用原包配色。

当前 HUDuiPRO 根目录共 505 个 UE 对象：首次新增 99 个、本次新增 405 个，以及原有 earth4 纹理。帧连续性、PNG 容器、资源类型与引用核对通过；视觉、实际播放、透明边缘和游戏性能由用户核验。

首次三个完整动效目录留下的 1,402 个自动生成 Texture2D 副本已清理，1,402 张运行 PNG 保留；媒体序列目录的额外 UE 资产数为 0。记录见 [最终状态](Components20261030/post-cleanup-state.json)。

会话结束时已删除 AE 渲染／转换日志、完整调试转储、API 探测、旧进度快照和一次性轮询／清理脚本；可复用的导入与核对脚本、最终证据和调用索引继续保留。准确删除路径、体积与哈希见 [临时文件清理记录](Components20261030/session-cleanup.json)。
