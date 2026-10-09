# 14 个模型配色参考 A_v3 审核

共 28 张独立蓝红图板，每张包含三分之四效果、正视、左侧视和后视。原十三模型全部重新设计，包含指挥中心和战略中心；制作中按用户明确决定补入重防号 WM01（UnitTypeId=2）。

[可放大审核画廊](index.html) · [全套总览](overview.html) · [完整配置与来源清单](review-manifest.json) · [审核状态](approval_A.json)

## 色卡与分区

所有基础涂装只选用户四张色卡：乳白 #FEE4D9、固定米砂 #D5C09C、机构深灰 #2C3735；蓝方浅/深队色 #6AA4BE/#274E61，红方浅/深队色 #A34053/#662249。乳白目标降至可见模型表面的20–30%，参考图不作精确覆盖率测量。护盾顶部三个色点属于队色灯，蓝方蓝、红方红；其余原有固定功能色保留。两档队色与三档明暗分别管理。

参考图用于审核大色块、队色区及线条整理。原模型尺寸、几何、装配、活动件、动画和 LOD 是施工依据；二维生成图不作尺寸测量图或实际 Blender 成品。

## 模型清单

| 模型 | 蓝方 | 红方 | 配置 | A 审核 |
|---|---|---|---|---|
| 先驱号 | [图板](Boards/DefaultSoldier_blue.png) | [图板](Boards/DefaultSoldier_red.png) | [分区](Configs/DefaultSoldier.json) | 待用户决定 |
| 重防号 | [图板](Boards/WM01_blue.png) | [图板](Boards/WM01_red.png) | [分区](Configs/WM01.json) | 待用户决定 |
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

彼之矛施工体共用彼之矛 A_v3，不额外计作独立模型。SSF 本轮继续作为已入库建筑美术资源，不新增玩法类型。重防号沿 WM01 稳定实现定位，非玩家 Ground 席位。

## 源图与修订

三种兵种沿用 CommanderLOD 的实际原模型四视图；SSF 六座沿用已入库 B_v2 的实际 Blender 四视图。其余五座玩法建筑复用 A_v2 已从 UE 网格或完整 Blueprint 提取的二十二张原生视图，三个原捕获报告原样保存；本轮没有重新操作编辑器或保存正式资产。两炮塔按 +Y 炮口方向：Source Right→Front、Source Back→Left、Source Left→Back。

前哨按用户最新要求改为整件配色：中央最高宽矩形整件浅队色，相邻矩形整件米砂，外围窄矩形整件乳白，外部单个半圆环整件深队色；不按高度切成色带。被否定的按高度稿保存在 height-draft 文件，仅供追溯。

原色卡与护盾反馈截图保存在 References；精确生成提示词保存在 Prompts，输入及输出路径见 [生成记录](generation-log.json)、[色卡来源](palette-source-map.json)。源视图和来源哈希在清单及各模型配置中。未选首稿保留供追溯，不进入主画廊。旧版 [A_v2](../LocalTeamColorReference_A_v2_20261008/REVIEW.md)及[红蓝实际 UE 对照](../LocalTeamColorReview_20261008/REVIEW.md)继续保留。

## 核对记录

[助手逐张读图](visual_qa.json) · [文件、来源和静态链接回读](delivery-check.json) · [冻结交付清单](frozen_delivery_manifest.json)。蓝红分区以配置中的基础 HEX 为准，二维光影不是新增涂装色，也不作为精确覆盖率或尺寸测量。

本地浏览器交互未自动验证：前轮浏览器工具拒绝 file 协议，本轮沿用该限制，不替换协议或入口绕过。画廊提供本地文件与静态核对结果。

## 当前审核阶段

本轮停在参考设计 A。可以按“模型名 + A_v3 + 通过／需要修改的位置”逐项给出决定。没有明确通过的模型继续改参考，尚未进行 Blender 施工或正式 UE 更新。

项目流程见 [模型制作技能](../../.agents/skills/guli-model-production/SKILL.md) 与 [美术规范](../../Progress/RequirementDocument/GuLiStrike美术规范.md)：参考图 → 用户审核 A → Blender 成品 → 用户审核 B → 正式 UE 资源。
