# Ground 三渲二图标正式接入

**会话状态：已完成（2026-11-03，按用户收尾指令）。** 正式资源与最终结构化证据保留；旧过程日志、截图和一次性回执已按清单清理，正文中的历史链接只记录当时验证。未执行的视觉或完整性能验收未改记为通过。


2026-11-03。按用户“通过数据管线实装在游戏里”执行，使用 `SingleIcons_A_v4` 原图。七张正式 Texture2D 对应八个 HUD 用途；喷气容量与喷气跳跃共用图标 ID 1。保持四种弹体的铜金/深灰配色，闪现使用淡蓝机甲、闪电和残影。

## 同日补正：Ground 席位启动

首次导入跳过了 Mech 标准后处理，遗漏网络清单同步。虽然 IconId 只负责显示，当前 Ground 配置指纹包含完整技能表，因此 RocketJump.IconId 从 7 改为 1 后，旧清单会触发配置不一致并在 120 秒后结束 PIE。这是本次接入遗漏。

已移除制作脚本的跳过标记，并检查标准后处理确实输出 200/600 两份清单；本次重新导入 Skills，标准同步完成且无需重烘动作。两份清单的 Ground 指纹均为 `86fdae12c2b21c458c80844f33a1ad9f`，引擎清单校验通过。没有修改原生代码，无需再次构建。

正常 Ground 新局结构化回读通过：会话就绪、无网络错误、控制正式机甲、唯一 Ground HUD 在视口且归属正确、7 张图标已加载，喷气技能使用容量图标。启动约 196 秒后仍正常，未再触发原 120 秒超时。保留 PIE 供玩家继续；席位选择提示已恢复。未做截图/读图或本次性能测量，界面几何与动画观感未验收。证据：[同步](SeatFix/sync_readback.json)、[运行回读](SeatFix/runtime_readback.json)、[原失败日志](SeatFix/original-local-pie-failure.log)。

## 正式数据与资源

`GuLiStrikeIcons.xlsx / Icons → Data/Json → DT_GuLiStrikeIcons_Icons → GuLiIconCatalog → Ground HUD`。Cards、Skills、Buffs、HUD 保持通过 IconId 查询；卡牌插画继续由 ArtworkId 管理。

| 图标 ID | 用途 | 正式资源 `/Game/GuLiStrike/GroundMech/UI/Icons/` |
|---|---|---|
| 1 | 喷气容量、喷气跳跃 | `T_GroundIcon_JetCapacity` |
| 2 | 折射强化 | `T_GroundIcon_Ricochet` |
| 3 | 灼烧强化及 Buff | `T_GroundIcon_Burning` |
| 4 | 爆燃强化及 Buff | `T_GroundIcon_BurningBurst` |
| 5 | 射击 | `T_GroundIcon_Fire` |
| 6 | 瞄准 | `T_GroundIcon_Aim` |
| 8 | 闪现 | `T_GroundIcon_Blink` |

`RocketJump.IconId` 从 7 改为 1；7 号 ID 退役，不重新分配。旧 `T_GroundIcon_Jump` 确认无资产引用后删除。历史生成原图完整保留。

当前制作入口读取 [正式图标资源清单](../../ArtSource/UI/GroundMech/Icons/catalog.json)，不会重导入旧极简图。源 PNG 不裁剪、不重绘；UE 构建时等比缩至最长边 256，UI/RGBA、sRGB、无 mip、Clamp、非流送。最终五张 256×256、灼烧 256×192、射击 256×171。首次仅用 MaxTextureSize 时尺寸较小，已改为显式构建尺寸；初次导入证据保留为 `asset_import_initial.json`。

HUD 的 `SetDataIconTexture` 在缓存纹理引用变化时设置居中的等比缩放，覆盖技能、强化和 Buff。原画面的比例不会被方形槽拉伸；没有增加逐帧资源解析或轮询。5Hz 普通刷新、事件同步、短动画、循环动画和真实键位维持原流程。

## 验证

- [静态检查](static_checks.json)：42 表导出通过；10 处 IconId 外键有效；行结构头文件未改变；Python 语法检查通过。
- [Editor 构建](build-editor.log)：源码版 `D:/UnrealEngine-5.7`，`GuLiStrikeEditor Win64 Development` 成功，15 步，29.24 秒。沿用同一 HUD 任务已有的编译与重载授权。[BuildId](build-verification.json) 一致。
- [资源保存回读](asset_import.json)：7 张纹理路径、源文件 SHA-256、构建尺寸、UI 压缩及透明设置；四组业务表的图标引用全部有效。
- [初次 DataTable 导入](data_import.json)：更新 Icons、Mech Skills 两表，逐行核对通过，但遗漏了标准机动后处理和网络清单同步；已由上述补正修复。IconId 变化也必须同步完整技能表所参与的配置指纹。
- [原地图入口](scene_readback.json)：保存并回读 4 个已有实体，Ground 角色仍为 ModelId 4001 / A4B2；地图保存后无脏包。
- 初次交付未启动 PIE；随后针对席位故障已完成上述新局启动回读。未做运行时性能测量或助手截图/读图。资源与实体检查不代表玩家视觉通过；此前 5Hz 和动效运行证据仍属于先前交付。

## 玩家入口

地图 `/Game/Maps/LVL_CommanderMassPrototype`。`GroundHUD_v1_PlayerEntry` 位于 `(-2800,54000,1229.04)`；`GroundRogue_Entry` 位于 `(-2800,54000,1149.04)`。

1. 启动 PIE，选择 Ground；底部射击、瞄准、喷气跳跃、闪现应显示新图，喷气跳跃与右下容量图案一致。
2. F4 取得折射、灼烧及其前置满足后的爆燃，查看左侧强化图标与服务器确认的等级角标。
3. Space 喷气、Shift 闪现，检查图标清晰度、CD/就绪效果与键位；在常用窗口比例下确认横向弹体和机甲残影的可读性。

原图与完整提示词：[A_v4](../../ArtSource/UI/GroundMech/IconStyle_Toon3D_20261103/SingleIcons_A_v4/prompts.json)。本轮没有修改伤害、卡池、成长或技能数值。
