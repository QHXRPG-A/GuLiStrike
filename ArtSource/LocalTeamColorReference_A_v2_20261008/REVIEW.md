# 13 个模型配色参考 A_v2 审核

共 26 张独立蓝红图板，每张包含三分之四效果、正视、左侧视和后视。新增防空炮、哨戒炮、矿厂已纳入。

[可放大审核画廊](index.html) · [全套总览](overview.html) · [完整配置与来源清单](review-manifest.json) · [审核状态](approval_A.json)

## 色卡与分区

本轮只从用户四张色卡选取：奶油白 #FEE4D9 作主装甲，深灰 #2C3735 作机构，蓝灰 #6AA4BE／莓红 #A34053 作队色。仅已有功能位置允许少量 #EE9D58 或 #0D9099；护盾顶部青灯两队固定一致。色阶是体积表达，基础涂装色以配置 HEX 为准。

参考图用于审核大色块、队色区及线条整理。原模型尺寸、几何、装配、活动件、动画和 LOD 是施工依据；二维生成图不作尺寸测量图或实际 Blender 成品。

## 模型清单

| 模型 | 蓝方 | 红方 | 配置 | A 审核 |
|---|---|---|---|---|
| 先驱号 | [图板](Boards/DefaultSoldier_blue.png) | [图板](Boards/DefaultSoldier_red.png) | [分区](Configs/DefaultSoldier.json) | 待用户决定 |
| 彼之矛 | [图板](Boards/BiZhiMao_blue.png) | [图板](Boards/BiZhiMao_red.png) | [分区](Configs/BiZhiMao.json) | 待用户决定 |
| 护盾发生器 | [图板](Boards/ShieldGenerator_blue.png) | [图板](Boards/ShieldGenerator_red.png) | [分区](Configs/ShieldGenerator.json) | 待用户决定 |
| 前哨建筑 | [图板](Boards/ManualOutpost_blue.png) | [图板](Boards/ManualOutpost_red.png) | [分区](Configs/ManualOutpost.json) | 待用户决定 |
| 防空炮 | [图板](Boards/MissileTurret_blue.png) | [图板](Boards/MissileTurret_red.png) | [分区](Configs/MissileTurret.json) | 待用户决定 |
| 哨戒炮 | [图板](Boards/SentryTurret_blue.png) | [图板](Boards/SentryTurret_red.png) | [分区](Configs/SentryTurret.json) | 待用户决定 |
| 矿厂 | [图板](Boards/ResourceFactory_blue.png) | [图板](Boards/ResourceFactory_red.png) | [分区](Configs/ResourceFactory.json) | 待用户决定 |
| SSF 空军基地 | [图板](Boards/SSF_AirBase_blue.png) | [图板](Boards/SSF_AirBase_red.png) | [分区](Configs/SSF_AirBase.json) | 待用户决定 |
| SSF 克隆中心 | [图板](Boards/SSF_CloningCenter_blue.png) | [图板](Boards/SSF_CloningCenter_red.png) | [分区](Configs/SSF_CloningCenter.json) | 待用户决定 |
| SSF 指挥中心 | [图板](Boards/SSF_CommandCenter_blue.png) | [图板](Boards/SSF_CommandCenter_red.png) | [分区](Configs/SSF_CommandCenter.json) | 待用户决定 |
| SSF 军工厂 | [图板](Boards/SSF_MilitaryFactory_blue.png) | [图板](Boards/SSF_MilitaryFactory_red.png) | [分区](Configs/SSF_MilitaryFactory.json) | 待用户决定 |
| SSF 反应堆 | [图板](Boards/SSF_Reactor_blue.png) | [图板](Boards/SSF_Reactor_red.png) | [分区](Configs/SSF_Reactor.json) | 待用户决定 |
| SSF 战略中心 | [图板](Boards/SSF_StrategyCenter_blue.png) | [图板](Boards/SSF_StrategyCenter_red.png) | [分区](Configs/SSF_StrategyCenter.json) | 待用户决定 |

彼之矛施工体共用彼之矛 A_v2，不额外计作第十四个模型。SSF 本轮继续作为已入库建筑美术资源，不新增玩法类型。

## 源图与修订

两种兵种沿用 CommanderLOD 的实际原模型四视图；SSF 六座沿用已入库 B_v2 的实际 Blender 四视图。其余五座玩法建筑从现有 UE 网格或完整 Blueprint 提取原生视图，未保存或修改正式资产。两炮塔沿 +Y 炮口方向重新核对正视：Source Right→Front、Source Back→Left、Source Left→Back。

原色卡保存在 References，[原附件路径与复制映射](palette-source-map.json)逐张记录；精确生成提示词保存在 Prompts；源视图和来源哈希在清单及各模型配置中。未选首稿保留供追溯，不进入主画廊。旧版 [红蓝实际 UE 对照](../LocalTeamColorReview_20261008/REVIEW.md)继续保留。

[助手视觉记录](visual_qa.json) · [保存回读与静态核对](delivery-check.json) · [冻结交付清单](frozen_delivery_manifest.json)。自动浏览器工具因本地 file 协议策略未能复核画廊交互；图板、配置、脚本语法和静态链接已核对，浏览器显示与交互未记为通过。

## 当前审核阶段

本轮停在参考设计 A。可以按“模型名 + A_v2 + 通过／需要修改的位置”逐项给出决定。没有明确通过的模型继续改参考，尚未进行 Blender 施工或正式 UE 更新。

项目流程见 [模型制作技能](../../.agents/skills/guli-model-production/SKILL.md) 与 [美术规范](../../Progress/RequirementDocument/GuLiStrike美术规范.md)：参考图 → 用户审核 A → Blender 成品 → 用户审核 B → 正式 UE 资源。
