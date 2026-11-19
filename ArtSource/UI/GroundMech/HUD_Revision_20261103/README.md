# Ground HUD 图标与原包循环素材

**会话状态：已完成（2026-11-03，按用户收尾指令）。** 正式资源与最终结构化证据保留；旧过程日志、截图和一次性回执已按清单清理，正文中的历史链接只记录当时验证。未执行的视觉或完整性能验收未改记为通过。


2026-11-03 正式接入。实际造型、循环接缝和画面亮度等待玩家验收；未执行助手截图、读图或视口目检。

## 八张生成图标

使用内置 image_gen 分别生成透明 PNG，源文件原样保留在 `Icons/`。完整提示词与输出资源路径见 [icon_prompts.json](icon_prompts.json)，来源、尺寸、透明通道和 SHA256 见 [icon_manifest.json](icon_manifest.json)。全部 RGBA，透明通道极值均为 0～255。

| IconId | 文件后缀 | 用途 |
|---:|---|---|
| 1 | JetCapacity | 喷气容量 |
| 2 | Ricochet | 主体子弹与两次折转弹道 |
| 3 | Burning | 包裹火焰的子弹 |
| 4 | BurningBurst | 子弹、火焰与外扩爆炸 |
| 5 | Fire | 射击 |
| 6 | Aim | 瞄准 |
| 7 | Jump | 喷气跳跃 |
| 8 | Blink | 闪现 |

正式资产统一为 `/Game/GuLiStrike/GroundMech/UI/Icons/T_GroundIcon_<后缀>`。纹理使用 UI 组、透明通道、sRGB、无 Mip、Clamp、Bilinear，`MaxTextureSize=256`；1254 方形原图在 UE 当前缩放设置下驻留为 156 方形，未把该值伪报为实际 256 方形。屏幕图标由控件缩放，边框、键位和等级独立绘制。

源表为 `data/Excel/GuLiStrikeIcons.xlsx / Icons`。稳定行名之外的业务字段为 `Id / Note / ResourcePath`。卡牌的 `IconId` 与 `ArtworkId` 分离；技能、Buff、HUD 引用同一表。`UGuLiIconCatalog` 在创建 HUD 时准备并持有纹理，显示更新只读缓存，专服不加载。

## HUDuiPRO 循环

来源为 `Circle_Elements_04`、`Circle_Elements_Bars_02`，第 24～119 帧每隔一帧取样，实际为 24、26……118，共 48 帧。每帧 96×96，加 2 像素边缘形成 4×12、400×1200 图集；完整来源与逐帧哈希见 [loop_manifest.json](loop_manifest.json)。

正式纹理为 `/Game/GuLiStrike/GroundMech/UI/Motion/Textures/T_GroundHUD_AmbientHealth` 和 `T_GroundHUD_AmbientMap`；材质为同级 `Materials/M_GroundHUD_Ambient`。只呈现局部刻度弧，周期 4 秒，帧插值、0.25 秒首尾淡化、默认透明度 0.18。参数来自原 GroundHUD 表。

材质时间驱动循环，控制端使用与Slate材质一致的AppDilatedTime，仅在暂停/恢复时更新材质参数；入场完毕后开始，地图、选牌和 HUD 隐藏期间暂停。旧图集和新增图集实际共驻留 60 MiB。没有启动原包媒体播放器，原包不变。

复现：`Scripts/GroundMech/prepare_ground_hud_revision.py` 制作表和图集，标准 Excel 导出后由 `Scripts/GroundMech/author_ground_hud_revision.py` 导入纹理与材质。图标生成源不被脚本重绘。

运行结果及玩家入口见 [交付记录](../../../../Artifacts/GroundHUDRevision_20261103/DELIVERY.md)。
