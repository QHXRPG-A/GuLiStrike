# 两组机甲资源风格统一 · 参考与实际制作

本批已制作的 **8项成品已全部同步源码版UE5.7**，包括四台机甲、三套武器与独立导弹，共10个独立网格。用户最新明确“把已经制作了的资源都同步至UE，然后做一波总结”，据此补齐五项；轻型、Spider及随身机枪复用v9。查看[整批UE画面](UE_AllAssets_v10/Review_UE_All_v10.html)、[完整总结与用途](UE_AllAssets_v10/README.md)及[资源与哈希清单](UE_AllAssets_v10/delivery_manifest.json)。完整展示地图：`/Game/GuLiStrike/Mechs/StyleShowcase/LVL_MechAsset_Showcase`。

源交付为[五项效果与三视图 v9](Production_v9_VentMount/Review_B_v9.html#vent_mount)、[完整 Blender 文件](Production_v9_VentMount/Mechs_VentMount_v9.blend)、[保存回读](Production_v9_VentMount/saved_readback.json)。轻型头部双排气口已对称内收，并补齐沿原壳体贴合的封闭底座。赭金弧面头甲、灰蓝腿甲及Spider整件暗红保持；五项保留线稿与三档明暗。Mecha_01、Mecha_02与独立导弹沿用已制作的连续受光版本。

用户要求：`/Game/Assets/Mech_Project`、`/Game/Assets/MechaController`；“不改变色系和着色逻辑，将美术风格换成项目的美术风格，先出效果参考图+三视图”。

用户后续明确：“不要显示驾驶员”“SpiderMech 需要减面成低模”“也不要显示驾驶舱，这一块可以封起来”。最新参考中，轻型机甲驾驶区改为连续封闭装甲，各视图不显示驾驶员、头盔、座椅、护栏或开放舱；SpiderMech 采用低模分件方向。此修改指令不是整批参考审核通过。

SpiderMech“不再减面，只改色块和美术风格”撤回旧减面候选；[原网格风格版v2](Production_v2_Spider/Review_Spider_v2.html)是现行几何基线，后续线稿和整件配色见上方 v7 总览。Mecha_01、Mecha_02 和独立导弹沿用[实际 Blender 模型与三视图 B v1](Production_v1/Review_B_v1.html)，[原批次源文件](Production_v1/Mechs_Style_SourceBased_v1.blend)保留。此前的[六张参考板总览 v2](Review_A_v2.html)及[参考清单](delivery_manifest.json)保留历史，旧 Spider 低模设计不再作为减面依据。

此前色彩反馈：“红蓝饱和度过高”。Mecha_01 更新为 v2，武器板更新为 v4；蓝色机体和枪管统一降低饱和度为灰蓝，红色武器降为灰红，保留原色相类别、明暗关系和结构。修改仅针对用户附图的红蓝区域，其他四张参考沿用前次版本。原[总览 v1](Review_A_v1.html)及[交付快照](delivery_manifest_v1.json)作为历史保留。

二维参考采用内置 ImageGen，实际 UE 资源截图为形体与配色依据，项目 Ship 为分件、色块与结构线的风格依据。用户随后明确“开始制作，根据源模型制作”，已放行参考进入实际 Blender 制作。v1/v2 曾按原受光类别连续着色；之后用户明确五项缺少线稿、看不出三渲二，故 v3 起按最新反馈改用三档明暗并保留功能发光。实际成品不展示驾驶员与驾驶舱。本轮原 UE 材质网络未改动。

## 设计范围

| 对象 | 原色系与识别特征 | 源尺寸 / 说明 |
|---|---|---|
| SpiderMech | 深蓝灰装甲、赭黄机构、黑骨架，四片上腿装甲盖整件暗红；保留完整原网格 | 网格 XYZ 约5.73×4.76×3.67m；现行整件配色源于v5 |
| Mecha_01 | 原蓝色降低饱和度为灰蓝，搭配黑灰；前上方风挡、两侧舱盖、背面竖格栅、双趾足 | 网格 XYZ 约 2.06 × 2.05 × 4.67 m；当前 v2 |
| Mecha_02 | 金黄/黑灰、银灰窗沿；大八边形前窗、背面四喷口、双腿 | 网格 XYZ 约 1.91 × 2.18 × 4.53 m |
| Mech_Lightest | 赭金色弧面封舱甲、深青绿内凹排气孔及灰蓝上腿甲；排气口闭合底座贴合原壳体，原单侧武器和琥珀灯 | 以原 Mech_Lightest_Blueprint 装配为依据；当前实际v9；原骨架和挂点保留 |
| FireWeapon_01 | 灰蓝/黑；原三管、蓝色炮口套 | 参考图误画为两管，实际制作保留源网格三管和活动接口 |
| MissileWeapon_01 | 灰红主体、灰结构、橙红导弹 | 武器板 v4 降低红色饱和度；保留两列四排弹架 |
| Weapons_Machinegun_lvl1 | 赭黄/深青绿、琥珀灯；单根长炮管 | 已在轻型机甲主图中呈现，另列零件参考 |
| Cockpit_01 | 原棕灰围板、灰仪表台、屏幕与操纵杆 | 按最新要求退出当前参考展示；源证据与旧版只留档 |
| lowPoly_missile_01 | 灰白弹体、深灰分界、尾部翼片 | 保留原比例与轮廓 |

轻型机甲的 Cockpit_Jet、HalfShoulder_Box、Mech_Legs_Lt 与机枪按原蓝图合装展示。两目录里的碰撞代理、默认 UE4 模板资产和演示场景基本形体属于功能/演示辅助，不作为新机体造型。蓝图、动画、骨骼、物理与发射逻辑未在本阶段改动。

## 风格处理与边界

- 清理写实磨损、模糊污渍与随机微小噪点，保留各自原配色分布。
- SpiderMech当前保留原LOD0的 **839,778 三角面**、533,878顶点、10个材质槽；撤销旧15,000–25,000预算和19,656面候选。原位置、拓扑、UV、权重与导入法线保存读回一致，见[v2检查](Production_v2_Spider/source_style_report.json)。
- 统一规则装甲、窄倒角、规整关节与少量有依据的结构线；保留原曲面、尾部、腿长和装配特征。
- 原本简单的两台步行机甲在原外形体积内补充面板与可理解的机械分件，不增添手臂或替换兵种身份。
- 五项材质依据最新明确线稿/三渲二反馈，采用结构线、独立轮廓壳和三档明暗，保留色系及功能发光；不声称仍与原连续受光逻辑相同。实际 Blender 渲染不能替代引擎显示验证。
- 不把三视图当作已经制作好的网格或尺寸工程图。二维图之间仍需在后续还原阶段核对小结构，所有骨骼/挂点以原资源为约束。

## 来源与记录

- [原资产注册表、材质和尺寸快照](Source/source_inventory.json)：通过 UE Python/MCP 读取，未遍历 `Content/Assets` 文件夹或读取二进制资产文本。
- [原机体截图报告](Source/Rendered/capture_report.json)：独立源码版 UE5.7 预览进程，26 张实际截图。
- [零件补充截图报告](Source/Accessories/capture_report.json)：20 张实际多视角截图。Cockpit 原截图保留为历史证据，不在当前设计总览展示。
- 原图位于 `Source/Rendered`；补充零件多视角位于 `Source/Accessories`。正/侧/背源截图采用 4° 长焦近正交相机；设计板上的三视图是生成式正交表达。
- 第一次实时截图被另一项导航/PIE任务打断，已撤销本任务全部临时演员，转用独立进程。`Source/capture_report.json` 如实记录中断；完整独立截图为有效依据。
- 参考输出在 `Concepts`，完整生成提示词在 `Prompts`；交付清单保存 SHA256、来源和审核状态。

审核A沿用用户制作授权及最新原网格约束；当前八项已获明确导入授权并完成同步，用户对最终UE外观的评价单独记录。轻型玩家保留v9模型及已有增强输入，见[操控说明](../../../Scripts/GroundMech/README.md)；其他两套独立武器未新增开火等玩法。面数、近景描边余项、性能与验证器提示均见整批总结。项目规范保持v1.2。

依据：[模型制作技能](../../../.agents/skills/guli-model-production/SKILL.md)及[美术规范](../../../Progress/RequirementDocument/GuLiStrike美术规范.md)。参考设计需按“参考图 → 用户审核 A → Blender 一比一还原”推进。
