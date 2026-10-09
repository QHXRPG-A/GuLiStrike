---
schema: guli-progress/v1
id: DEV-20260917-001
work_id: WORK-20260917-001
kind: development
role: root
title: GuLiStrike 美术规范 — 维护与验收台账
areas:
- art
- rendering
- assets
- vfx
status: done
verification: passed
created: '2026-09-17'
updated: '2026-10-09'
summary: 保留全项目22色色库和原审核历史；v1.11记录扫荡者B_v1放行、正式UV遮罩且原Alpha保持、双方实际召唤自动改色通过，两类占位开关关闭。
next_action: 后续视觉资源按统一色库制作，持续维护功能色／资产例外和具体版本的审核证据。
relations:
  requirement: REQ-20260917-001
status_note: 规范维护与成品最终观感分开；v1.9色库与skill校验、v1.10待审交付历史保留。最新用户放行扫荡者B_v1，正式UE及实际双客户端改色已核对；台账done/passed不扩称所有资产最终验收或性能通过。
categories:
- art
---

# GuLiStrike 美术规范 — 维护与验收台账

当前规则唯一来源：[《GuLiStrike 美术规范》v1.11](../RequirementDocument/GuLiStrike美术规范.md)。本文件记录建立过程与审核证据，不复制另一份造型、爆炸或配色色表。

## 本次落实

- [x] 建立独立规范，收录已审八张兵种设计、后续重防号细化参考、Ship风格锚点、既有建筑几何实例和爆炸目标。
- [x] 明确新模型默认线稿+三渲二、扫荡者当前无线稿例外和重防号保留线稿。
- [x] 写入“出参考图→用户审核A→Blender一比一还原→用户审核B→UE导入”流程，区分批准与技术验证，已有批准无需重复。
- [x] 定义爆炸的形态、颜色、时序、烟火转变、冲击环和性能约束，保留三类既有指定资源的专属配置。
- [x] 三个制作技能增加制作前必读入口；修正模型旧PBR经验和地编通用人眼视角与项目规范之间的歧义。
- [x] `gulistrike-progress` 增加规范版本、变更、资产例外与用户审核证据维护职责。
- [x] 原样归档临时目录中的重防号/爆炸参考；两张非三视图效果图保存为长期透明PNG UI原图。
- [x] 清理可明确归属的本会话临时文件，保留生产输入、回退、参考和必要验收证据。
- [x] 完成已有技能校验、文档索引和链接/引用检查，并新增增量记录。

## 初始参考与审核事实

| 对象 | 参考审核 A | Blender/可播放成品审核 B | UE技术事实与边界 |
|---|---|---|---|
| 两台兵种八张设计图 | 用户明确“八张效果图是通过验收的方案，就原样照着设计”；八图哈希已核对 | 不能据此自动标记模型通过 | 设计清单为 `ArtSource/TacticalStyle_20260916/Production_Handbuilt/approved_design.json` |
| 重防号细化参考及连接v6 | 用户指定所附五图；45°平行双背舱及后续支架/节点修订沿用 | 用户指定当前v6“导出至UE成为正式资产”，据此放行当前成品正式导入；不倒填旧候选终验 | 2026-09-29正式SM/SK/贴图原位更新并保存回读；UE外观与实战效果仍待用户核验 |
| 扫荡者当前无线稿版 | 已审原设计与“删除扫荡者线稿、保留重防号线稿”的明确新指示并存 | 最新无线稿版没有单独的Blender最终美术通过记录；不倒填通过 | 用户已明确要求修改并导入，技术保存/独立读回通过；新规范不追溯撤销已有导入授权 |
| Ship | 用户指定与Ship一致的风格语言，要求保留船体及内线遮罩/外轮廓 | 没有将技术完成升级为整项美术终验 | 已接入玩家蓝图并独立读回，见原工作项 |
| 工业V3模型 | 既有实例记录用户“符合预期” | 仅沿用该版本已有验收范围 | 作为几何/装配经验，不自动算新Cel风格通过 |
| 三类指定爆炸 | 用户提供两张视觉目标图，并明确选择NS03/05/01 | 本轮没有新增用户对完整播放的终验意见 | 已接入、编译与阶段截图是技术/视觉检查，实战和性能仍按原工作项记录 |

原实施状态见[Ship与兵种工作项](20260916-Ship导入与扫荡者重防号风格重制.md)和[爆炸试作工作项](20260916-僚机对地轰炸动漫爆炸样板.md)。规范不会把它们批量改成done或passed。

## 后续审核记录格式

2026-09-29补充：用户“重防号模型导出至UE成为正式资产”明确放行 `WarMachine_LevelNodes_v6`，源Review SHA256 `0d2568d45953e0e53526438dac5c9bf06b940a18e86c525dbd432fa3f5bf4df6`。正式 `/Game/Commander/Units/Tactical/Cel/WarMachine` 的网格与贴图更新、原引用与材质保留、挂点适配和独立回读完成。遵从“只管制作、不需要查看过程图片”，助手未读图，留存真实UE预览供用户核验；没有PIE或性能实测。见[记录](../Archive/20260929-重防号v6正式模型替换.md)。仅更新本模型版本/授权台账，全局规则仍为v1.2，卡牌美术状态独立。

在具体资产的需求/开发文档记录详细证据，本台账只维护入口及跨资产的规则变更。每条至少包含：

| 字段 | 要求 |
|---|---|
| 资产与用途 | 兵种/建筑/特效名称、目标目录、UI或游戏用途 |
| 规范与参考版本 | art_revision、参考文件及哈希；不得只写“最新图” |
| 成品版本 | Blender/纹理/特效候选版本，截图与视频路径 |
| 审核A | 待审核/通过/修改后再审/明确免审/不适用，用户决定原话与日期 |
| 审核B | 同上，必须指向具体成品；不能从审核A、编译或导入成功推断 |
| UE与性能 | 未运行/部分/通过/失败，实际证据及未覆盖内容 |
| 例外与变更 | 适用资产、旧值→新值、用户指示；规范全局变化另增版本 |

确认新规范或跨资产规则后，更新规范当前正文和版本，在 `Progress/Archive` 追加不可覆盖的增量记录。现有用户批准持续有效；只请求尚缺失或受到新设计修改影响的审核。

## 原图与清理约定

Ship组件分批制作与审核入口：[首批双联炮/CIWS/Thor](20260917-Ship三组件风格样板制作.md)、[第二批自动炮/三联炮/单管炮](20260917-Ship第二批三组件贴图与框线制作.md)、[第三批机库/干扰/护盾](20260917-Ship第三批支援组件贴图与框线制作.md)。各批具体A/B、源文件和UE验证以对应工作项为准，开始下一批不改变上一批决定。第三批A由“开始实施”放行：无人机v2、深蓝白黄干扰v3、绿色护盾v3；实际原模型材质v3随后获用户“三件均通过 B，导出 FBX”批准。无人机标识在薄壁外侧、方向保持，CIWS青蓝不变。第三批三套静态FBX、四份Blender、九张2K贴图与预览已交付并回读通过；UE正式验证未运行，第二批B状态独立，规范仍为1.0。这些配色与标识要求仅属于具体资产。

- [参考清单](../../ArtSource/ArtDirection/reference_manifest.json)登记八张已审图及归档后的重防号/爆炸参考，明确第三张外撇后视不作为当前背舱装配目标，仍保留历史图片。
- [UI来源清单](../../ArtSource/UI/UnitPortraits/source_manifest.json)记录扫荡者、重防号的非三视图效果图。保留原尺寸、RGBA与透明通道，未裁切、缩放或重绘，未擅自接入游戏UI。
- 清理只处理本会话制作目录下能确认归属、无继续使用且非必要验收依据的临时探测脚本、传输回包和闲置日志；不搜索或删除用户系统临时目录、Content资产、生产模型/FBX/贴图、回退版本、设计参考或长期UI原图。
- 活跃编辑器/Blender仍在写入的文件不能删除。清理前逐项列明绝对路径和大小，核对文档引用，清理后保存清单与保留资源核验结果。

第四批覆盖[剩余四组件](20260917-Ship剩余四组件贴图与框线制作.md)：底置双联炮v1、高射速炮v2、燃烧弹舱v1、导弹舱v1已提交参考A。燃烧弹舱按用户要求加入平面火焰标志，候选放在两端外侧短板；原几何保持。四件A待审核、实际B未开始，第二批B独立待审，规范仍为1.0。

## 本次验证与归档

- 四个技能通过现有 `quick_validate.py` 校验；规范、技能与参考入口的本地链接检查无缺失。
- 参考清单15项、原八张已审设计和两张UI原图的SHA256均相符；UI原图尺寸、RGBA及透明通道保持。
- 按[清理计划](../../ArtSource/ArtDirection/session_cleanup_plan.json)核对路径、大小与哈希后，逐文件删除58个临时产物，共4,476,259字节（约4.27MiB），失败0。未递归删除目录；[清理结果](../../ArtSource/ArtDirection/session_cleanup_result.json)保留每个原路径及哈希。
- 现有文档工具 `build` / `check` 通过，零错误；12条提示均属于原有文档的篇幅或任务列表长度，不涉及本规范。检查摘要见[validation.json](../../ArtSource/ArtDirection/validation.json)。
- 本轮未修改UE资产、模型、C++或自动化测试文件，未声称新增运行时/性能验证。后续成品视觉验收仍按各资产工作项执行。

增量记录：[2026-09-17 美术规范建立与制作技能接入](../Archive/20260917-美术规范建立与制作技能接入.md)。

## 2026-09-17 全组件正式替换

用户后续明确“开始实施，然后把这些组件导入至ue替换正式资源”。本批纳入全13件正式替换，保留原网格路径、蓝图引用、源几何、原骨骼/挂点和碰撞。新增三档材质、2K纹理及独立轮廓，保存后独立回读通过。下文之前的“UE未运行/另行安排”是原交付阶段状态；当前实施以本节及[总开发记录](20260917-Ship剩余四组件贴图与框线制作.md)为准。

首批v4、第三批v3沿用已有B通过；第二批v2和第四批v1按[直接发布授权](../../ArtSource/Ships/ShipComponentStyle_20260917/UE_Integration/release_authorization_20260917.json)导出，不补写额外B视觉通过。7门炮637姿态核对通过；原舰体bottom_mid_0缺失与六个组件无兼容槽位仍按旧配置保留，完整实战/性能未覆盖。

[实际总览](../../ArtSource/Ships/ShipComponentStyle_20260917/UE_Integration/Review_Formal_v1.html)、[保存回读](../../ArtSource/Ships/ShipComponentStyle_20260917/UE_Integration/saved_asset_readback.json)、[交付总清单](../../ArtSource/Ships/ShipComponentStyle_20260917/UE_Integration/delivery_manifest.json)。UE表面沿用舰体可调美术光方向三档材质及场景投影，不等同Blender随灯光变化的自阴影。

规范仍为1.0；火焰、无人机外侧标识、深蓝白黄干扰和绿色护盾均为具体资产要求，CIWS青蓝不变。

## 2026-09-17 自然资源包例外（规范v1.1）

用户在自然资源包规划中明确选择“植被无线稿”“整体放大十倍”“包含轻微风摆”，并要求实施保留A/B双审的完整计划。规范升为1.1：仅NaturePack_20260917的N01–N32植被不制作内部线稿和外轮廓；N33–N34岩石保留轻描边与少量主折线；三档明暗均保留。

与旧规则相比，本包植被从新模型默认双线稿改为无线稿；不改变Ship、兵种或其他资产的既有记录。范围34款/33独立网格，具体来源、尺寸、候选版本和审核状态见[自然地编资源包制作记录](20260917-自然地编资源包.md)。当前[v3审核入口](../../ArtSource/Environment/NaturePack_20260917/Review_A_v3.html)包含完整外形参考与图集/面片工艺；A待审核、B未开始、UE未运行，不能把规则或制作方法确认写作产物视觉通过。

用户进一步明确“这些都是植被，用UE5植被常用的制作方法制作”，因此以Masked双面图集卡片替代逐叶/逐瓣几何；保留ArtLightDirection三档路线，不直接改为Two Sided Foliage受光模型。本次是本包制作工艺修订，不改变全局美术规范版本。阶段交付见[自然包参考与植被工艺增量](../Archive/20260917-自然资源包参考与植被贴片工艺.md)。

## 2026-09-18 松树林v1通过与原地图实施（规范v1.2）

用户对[实际UE v1](../../ArtSource/Environment/PineStyleAdaptation_20260918/Review_v1.html)明确“审核通过”，并要求“全面重构这些资源”“直接把它复制过来，在原处直接替换成我们的资源”。该记录放行已审无线稿植被、大色块三档材质及草簇矮宽方向扩展；下载源保持不动，项目原包路径可原位修改，验收为直接复制的原Demo_Map，不重摆场景。

规范v1.1仅登记NaturePack无线稿例外，v1.2新增松树林项目适配植被的已批准例外；Ship、两台兵种及其他资产记录不变。此为已存在UE资源的材质/LOD适配，不补写不存在的Blender通过，也不将v1批准当成随后整图通过。实施、布局读回及最终审核入口见[原地图实施记录](20260918-松树林原地图全资源风格重构.md)。

## 2026-09-18 玩法地图光照复核

用户反馈两张玩法地图局内偏暗，并指定参照Demo_Map。已按真实参考视口EV100=0校准局内曝光与后处理、清除空战旧压暗参数，经PIE截图和定向保存读回；模型材质、Demo及关卡布局保持。候选画面见[对照入口](../../TestResults/GameplayLighting_20260918/Review.html)，技术范围与待用户视觉确认见[实施记录](20260918-玩法地图对齐Demo光照.md)。这是两张关卡实例修正，不改变美术规范v1.2或其他资产审核状态。重防号拥挤另按实际占地不匹配诊断，未算作修复通过。

## 2026-09-19 重防号导弹指定2A爆炸

用户明确要求“导弹和尾焰特效大小以及爆炸特效设置为原来的2倍”，并指定`/Game/Stylized_Explosion_Pack_Vol1/VFX/N_Stylized_Explosion_2A_Classic_Explosion`。该指令授权本项直接实施；项目副本`NS_WM01_Explosion_2A`保留原动画/材质，增加统一缩放参数，原商城源资产保持原样。

资源选择沿用用户明确决定，Blender模型审核不适用；当前完整播放尚无用户新增终验意见。Niagara编译、比例播放与双客户端Q接入通过，未做性能压测。具体版本、截图、运行参数及授权边界见[导弹调整记录](20260919-重防号导弹范围与特效调整.md)。这是具体资产与玩法尺寸的调整，不修改全局美术规范v1.2。

同日用户评价爆炸偏小，明确要求“设置为现在的4倍”。据此将爆炸有效缩放0.4→1.6，单独调整爆炸配置，保留导弹/尾焰比例与伤害范围。该反馈属于修改指令，不能记录为前一版本或新版本的视觉终验通过；新对照证据见同一开发记录末节。

## 2026-09-19 两组机甲参考设计

用户明确要求 `/Game/Assets/Mech_Project` 与 `/Game/Assets/MechaController` 风格统一，“不改变色系和着色逻辑”，先出效果参考和三视图。本项保留原 Default Lit 主体与原 Unlit 驾驶舱屏幕，以造型分件、干净色块和结构线对齐项目；这是具体资产的任务约束，不修改全局规范 v1.2。

参考交付为四机体、三套武器与独立导弹，共六张效果参考和三视图。用户后续明确不显示驾驶员、SpiderMech 需要低模，以及驾驶舱可以封起来；SpiderMech 采用低模 v2 方向，Mech_Lightest v4 用连续装甲封闭驾驶区，独立驾驶舱退出展示。SpiderMech 原 LOD0 为 839,778 三角面，15,000–25,000 为制作预算。

随后用户对 Mecha_01 与武器板反馈“红蓝饱和度过高”，当前 Mecha_01 v2 和武器 v4 保留红蓝色系并降低饱和度，分别采用灰蓝与灰红；其余四张参考沿用前次版本。[参考总览 v2](../../ArtSource/Mechs/StyleUnification_20260919/Review_A_v2.html)、来源、版本和具体边界见[制作记录](20260919-两组机甲资源风格统一参考.md)。

用户最新明确“blender已开，开始制作，根据源模型制作，SpiderMech 也是，在原模型和参考图基础上减面并制作”，据此放行 A 进入实际制作。当前已从 10 个实际源网格制作八个成品候选，SpiderMech 为 19,656 三角面（减少 97.66%），保留 299 根骨骼和 10 槽；轻型机甲封舱、无驾驶员，红蓝降饱和。FireWeapon_01 原网格实为三管，参考图少画一管，实际制作保持源结构。当前[真实模型与三视图 B](../../ArtSource/Mechs/StyleUnification_20260919/Production_v1/Review_B_v1.html)待用户审阅；技术读回不等于 B 通过，UE 正式替换未开始。此为具体资产制作记录，规范仍为 v1.2。

## 2026-09-19 轻型装甲指定接入

用户随后明确选择当前“轻型装甲”成品作为地面玩家模型，并要求“PLEASE IMPLEMENT THIS PLAN”，完整批准模型导入、重防号高度适配、动画、增强输入和Demo_Map副本方案。本项据此直接导入当前`Mech_Lightest`，源blend SHA256为`97fe0d3954dbea27ce36ade02a1efbb19887fdd0a8b09fbde187619c7aa430aa`；不补写独立B视觉终验，不扩展其他七项候选的审核状态。具体接入和UE证据见[轻型装甲接入记录](20260919-轻型装甲地面玩家接入.md)，全局规范仍为v1.2。

本项已导入`/Game/GuLiStrike/GroundMech`，4,824三角面、封闭驾驶区，模型基准高度748.3796cm，与重防号对齐。骨架与挂点、动画、增强输入、双端同步及源码构建技术验证通过；用户反馈WASD跳变后，修复本项目动画蓝图根位移提取配置，未修改原动画。当前[实机画面](../../TestResults/GroundMech/PIE_final.png)与[交付归档](../Archive/20260919-轻型装甲地面玩家接入与根位移修复.md)记录技术结果，不补写用户对最终外观的审核决定。

## 2026-09-19 SpiderMech撤回减面

用户指出SpiderMech v1近景“几乎不能用，全是碎面”，随后明确“不再减面，只改色块和美术风格”。据此撤销本资产低模预算，恢复完整839,778面源网格，保留原位置、拓扑、UV、权重和导入法线，只整理连续蓝灰/赭黄/深灰色块及材质风格。旧逐面取色与平面着色叠加减面，构成碎片外观；不是已接入法线贴图损坏。

新[原网格风格版v2](../../ArtSource/Mechs/StyleUnification_20260919/Production_v2_Spider/Review_Spider_v2.html)已另存并打开Blender，源结构保存回读一致。Spider v1记为修改后再审，v2待用户B审核；本项未导入UE。轻型装甲与其他候选不受影响，全局规范仍为v1.2。具体版本与技术边界见[增量归档](../Archive/20260919-SpiderMech恢复原网格并整理色块.md)。

## 2026-09-19 五项机甲线稿与对称配色

用户指出轻型背部染色不对称，以及FireWeapon 01（三管）、MissileWeapon 01、Machinegun Lv1、SpiderMech和轻型机甲缺少线稿、看不出三渲二。本条最新反馈授权五项改为明确线稿和三档明暗，替代本批此前仅用连续受光表达的方案；保留原色系和功能发光。

后续用户追加Spider增加对称红色、轻型增加对称蓝白，形成v4镜像条纹；随后否定该条纹并明确整件染色。v5改为Spider四片完整暗红上腿甲、轻型蓝舱甲/白腿甲。用户再要求蓝白对调后形成v6，又评价不满意并要求白色改黑，故当前[实际v7总览](../../ArtSource/Mechs/StyleUnification_20260919/Production_v7_Black/Review_B_v7.html)为轻型炭黑舱甲/灰蓝腿甲，Spider继续沿用v5整件暗红。

五项保留v3内线/外轮廓/三档；Spider仍为839,778面完整原网格，轻型封舱、无驾驶员，原单侧武器装配保留。v7保存回读确认整件颜色、几何与绑定约束，视觉尚无用户通过意见；新材质未导入UE，原Ground资源未覆盖。此为指定资产修订，全局规范仍为v1.2，详见[记录](20260919-两组机甲资源风格统一参考.md)和[配色增量](../Archive/20260919-机甲整件分色与黑蓝配色修订.md)。

## 2026-09-19 轻型头部与散热结构细化

用户最新提供赭金色参考，要求恢复头部配色并做出弧度，随后指出前排气孔像梯子、肩侧外挑竖条不合规。已按明确修改指令重做实际头甲、双排气孔和肩侧散热舱，保留蓝腿甲、原骨架挂点与封舱约束。[v8实际近景和三视图](../../ArtSource/Mechs/StyleUnification_20260919/Production_v8_HeadRefine/Review_B_v8.html)及[新增归档](../Archive/20260919-轻型机甲弧面头甲与散热结构细化.md)为当前轻型候选；v7黑色头甲退出当前展示。

28个新网格闭合、无退化，头甲和前排气孔镜像误差0；轻型主体17,286面，轮廓8,573面。此为本资产指定几何细化，不将技术检查记为用户视觉通过，不修改全局规范v1.2，也未覆盖UE当前Ground资源。

## 2026-09-19 轻型排气口贴合修复

用户指出v8头部两侧排气口浮空。v9内收并后移框体，补齐沿原壳体贴合的对称闭合底座；[实际前后对比与三视图](../../ArtSource/Mechs/StyleUnification_20260919/Production_v9_VentMount/Review_B_v9.html#vent_mount)替代v8作为当前轻型候选。保存回读两侧各115个后沿点接合，镜像误差0。上一版网格闭合检查未涵盖装配接触，此次补查实际贴合。

当前轻型主体20,038面、独立轮廓11,325面，原挂点及其他资产保持。用户修改指令不等于视觉终验通过；全局规范仍v1.2，UE旧Ground资源未覆盖。事实见[增量归档](../Archive/20260919-轻型机甲排气口浮空修复.md)。


## 2026-09-19 两台成品明确放行同步UE

用户在v9贴壳修复后明确“同步至UE”，并补充“SpiderMech 也同步”。据此导入完整v9源文件中的当前轻型及Spider，源SHA256为`6caa1f5569d1c929b30f924c71f9b06144208ed340c437f3194a3399ec46912b`；这是两台指定版本的导入授权，不扩展为其他候选或最终UE外观的视觉评价。

当前轻型进入`/Game/GuLiStrike/GroundMech/Style_v9`并更新原玩家BP；Spider进入`/Game/GuLiStrike/Mechs/SpiderMech`，通过继承原蓝图的`BP_SpiderMech_Styled`在Demo副本展示。线稿、三档明暗、原功能灯、封舱及整件配色同步；Spider主体不减面。四张[UE实际图](../../ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9/Review_UE_v9.html)、[保存与实机验证](../../ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9/README.md)及[增量归档](../Archive/20260919-轻型机甲与Spider同步UE.md)已交付。全局规范仍v1.2，当前高面数及轮廓成本的性能压测未进行。

## 2026-09-19 整批八项放行同步UE

用户最新要求“把已经制作了的资源都同步至UE，然后做一波总结”，据此补齐Mecha_01、Mecha_02、三管FireWeapon_01、MissileWeapon_01和独立Missile_01；已同步的轻型、Spider与Lv1机枪复用有效版本。本批8项共10个网格全部进入项目目录，并新增完整展示关卡；当前原骨架/挂点、蓝图、保存引用及两台Mecha原动画姿态已验证。

五项指定资源维持线稿和三档明暗；Mecha_01、Mecha_02、独立导弹按已制作成品保留连续受光。导入授权不扩展为新武器玩法、再次减面或用户最终外观通过。完整面数、截图、描边余项与验证边界见[整批交付](../../ArtSource/Mechs/StyleUnification_20260919/UE_AllAssets_v10/README.md)及[新增归档](../Archive/20260919-机甲与武器整批同步UE及总结.md)。全局规范仍v1.2。

## 2026-09-20 地面机枪逐发特效候选

用户明确指定`/Game/Assets/VFX/WeaponBulletVFX/NS/VFX_Smg_Loop`和`VFX_FireGun_Loop`作为已确认参考A，并批准先制作可播放候选、正式接入前完成视觉审核。项目副本为`/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_Bullet`及`NS_GroundMech_Muzzle`，前者保留源中央弹道材质与配色、去双侧散射及枪口层，后者提取枪口焰与近口火星并改为逐发有限寿命。

当前B候选与炮管后坐已在`/Game/Maps/LVL_GroundMech_FireReview`实际播放，[62帧连续图、时间轴和采样](../../TestResults/GroundMech/Fire/Review.html)覆盖起始、回缩、复位和消散。Niagara及动画蓝图编译0错误/0警告，单发粒子、池回收和双端同步技术检查通过；没有用户对当前B的通过决定，正式玩家蓝图武器开关保持关闭。全局规范仍v1.2，不改变其他资产审核状态。具体源/目标、镜像枪口轴向修正及边界见[开发记录](20260920-玩家地面机甲开火与升级配置.md)。

## 2026-09-21 地面机甲双喷、飞行倾斜与下落确认

用户指定`VFX_FireGun_Loop`、双喷口与15°飞行腿部倾斜，并已明确批准实施动画蓝图及空中战斗计划。当前版本为`ABP_GroundMech`图表20260921.1、源喷火资源和`FallGravityMultiplier=2`，没有新建或改造Blender模型；模型A/B不适用，既有参考及直接接入授权继续有效。

本日用户针对“鼠标指向空处开火”和“最终连续画面：双喷、腿部倾斜和新版下落效果”明确“这两个我已经验证过了”。据此登记这两项当前UE实机结果通过，依据为[用户确认记录](../../TestResults/GroundMech/Animation/user-verification.json)；当前源码/配置摘要、技术采样与[实施记录](20260921-地面机甲动画蓝图与空中战斗.md)相互关联。旧[连续画面](../../TestResults/GroundMech/Animation/flight-before-fall-tuning.gif)仍明确标作下落调参前版本，不伪装成当前录像。

此决定不扩展为容量条、其他机枪特效或机甲整体外观的独立终验。技术验证、性能边界与用户确认分记；全局美术规范仍为v1.2，没有新增通用风格规则。

2026-09-30补充：用户针对重防号导弹当前`Candidate_v3`明确“赶紧切换啊”“代码层面彻底替换啊”，据此放行本版本正式接入。参考与32帧源预览沿用[Candidate_v3](../../ArtSource/FX/WM01MissileCluster/Candidate_v3/review.html)，规范保持v1.2。原生按WM01/MissileLauncher表记录默认使用v3，候选及资产开关删除；授权构建重开、三档GPU与保存后包重载回读通过。接入放行不等于完整实战视觉终验或性能通过，本轮未启动PIE或测量。详见[当前开发记录](20260929-重防号导弹解锁与肉鸽卡牌.md#2026-09-30-v3原生正式接入)。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，CommanderLOD_3Tier_v1实际版本B已通过；六组正式资源已切换保存，游戏运行与FPS未运行。

同日后续用户明确“需要应用正式资源”，据此放行已展示的5–15米渐长尾焰正式应用。原位更新VFX48的`NS_WarMachineHoverPool`及两个正式材质，保存与包重载后参数/编译就绪检查通过；此前候选待应用状态结束。已有后续授权构建包含本项15米包围盒，当前DLL及8份BuildId核验一致，本輪未重复构建或启动PIE。应用授权不扩大为实战、网络或性能终验；见[正式应用归档](../Archive/20260930-重防号5至15米渐长尾焰正式应用.md)，全局规范仍为v1.2。

## 2026-10-04 ControlRig 机甲参考 A-v1

用户明确要求实施 ControlRig 机甲美术统一计划，首轮停在参考审核 A。源实际为四足单主炮、152 骨骼、284,700 三角面、14 区段、1 LOD，CR_Mech 的 preview/骨架引用及部署/待机/行走已只读登记。暖白、珊瑚红、深暖灰、琥珀灯的[四张2K参考与四张灰模](../../ArtSource/Mechs/ControlRigMechStyle_20261004/README.md)共用姿态、尺度和配色，保持源结构与法线，未重拓扑或主动减面。

当前 A-v1 [冻结清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v1/reference_manifest.json) SHA256 为 `93324b32cb12fb9273bdb3d7ad5c60ce59798655f30bb657f53cca7f18db4fb4`；A 待用户针对该版本决定，B 和正式 UE 副本未开始。部署真实位移及 cannon_02 缩放已采样；默认 Blender FBX 导入将 root 表示为 Armature 对象的差异留给 B 兼容处理。预算和性能没有过审结论，未改变 Ground、重防号或战斗引用。详见[实施记录](20261004-ControlRig机甲美术统一.md)与[阶段归档](../Archive/20261004-ControlRig机甲参考A_v1交付.md)，全局规范保持 v1.2。

## 2026-10-04 ControlRig 机甲四色参考 A-v2

用户随后附色板并要求“改成这种配色”，据此用深青灰`#2C3735`、铁锈红`#8E3A2A`、灰青`#557B78`和沙米色`#D5C09C`替代本资产A-v1暖白/珊瑚红方向。当前[A-v2四张2K参考](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v2/README.md)保持源结构、同姿态/同尺度、分色区域、三档明暗和结构线；与A-v1相同的几何/权重、骨骼/姿态和法线已核对。用户色板原件已复制留档，旧27个冻结文件及清单哈希保持原样。

A-v1决定更新为修改后再审历史；A-v2清单SHA256为`ea47e95d2e5ee1f8d2bfe3ccd1fd9b249873e99fe31d93f9c9935b72af1ed9ac`，当前待用户对具体版本审核。B和正式UE副本未开始；改色授权不记为A/B通过，也不推广到其他资产。详见[四色增量归档](../Archive/20261004-ControlRig机甲四色参考A_v2.md)，全局规范仍为v1.2。

## 2026-10-04 ControlRig A 放行与 Blender B-v2 候选

用户通过A-v2后提交B-v2：152骨骼、三动作，预算超标；Freestyle主展示未代表实际内线完成。283帧视频回读通过，未导入UE，详见[当时归档](../Archive/20261004-ControlRig机甲A放行与BlenderB_v2候选.md)。

## 2026-10-04 ControlRig B-v4 / UE-v1


## 2026-10-05 彼之矛逐顶点动画 v1

用户批准彼之矛首轮开发方案，并勘误“定点动画”为“顶点动画，不需要骨骼”。本轮复用已审ControlRig B_v4，冻结源blend SHA256为 `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`；旧配色/稀疏线稿/三档明暗的放行继续有效，派生兵种战场尺度为2倍。


## 2026-10-05 彼之矛远景 LOD_v2


> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../ArtSource/CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。



## 2026-10-05：v1.3 指挥官模型三档勘误

用户明确统一Soldiers六种兵种为LOD0近景、LOD1中景、LOD2远景，保留近景、既有配色/线稿例外、ID和变形路线。重防号仍按WM01/ID2路由，玩家Ground不迁移。规则变更已确认；`CommanderLOD_3Tier_v1`实际成品B尚未通过，正式资源未切换。

[当前审核与哈希](../../ArtSource/CommanderLOD_20261005/review_manifest.json)、[候选实景](../../ArtSource/CommanderLOD_20261005/Review/index.html)、[迁移开发](20261005-指挥官三档LOD纠正与资源迁移.md)、[勘误归档](../Archive/20261005-指挥官三档LOD纠正与候选资源交付.md)。冻结源/原始回读继续保留，当前生成脚本使用三档入口。原生编译、PIE、联机和实战帧率未运行。

## 2026-10-05 指挥官三档成品放行

用户在打开审核Map后明确“审核通过”，对应`CommanderLOD_3Tier_v1`、SHA256 `761bc5edd06b08c598771943b46ebcbd7bc8285b42d345e234fea0907d02783d`；[B决定](../../ArtSource/CommanderLOD_20261005/approval_B.json)与批准时源文件哈希固定。随后用户明确“现在编译并重开 UE”，源码Editor构建、模块版本和新字段加载通过。六种兵种76个正式资源组已切换并保存回读，模型均为LOD0/1/2；[正式交付](../../ArtSource/CommanderLOD_20261005/formal_delivery.json)与[新增归档](../Archive/20261005-指挥官三档LOD审核放行与正式资源切换.md)为当前入口。

四足彼之矛保留已审四色、稀疏轮廓与三档明暗，正式运行资源无骨骼。车辆远景每台多10面、彼之矛近/远景原预算差额和重防号源远景剪影偏差继续登记；美术B不替代预算、玩法或帧率结果。规范仍为v1.3，未启动PIE/联机/性能测试。

## 2026-10-05 SSF 建筑六套色系参考 A-v3

用户先批准SSF六座建筑、平台、灯柱/灯片和无人机制作方案，随后明确“这些建筑色系是不是太单一了，希望每个建筑都是不同的色系，在参考色系中去选然后组合”。据此替代本批原奶油白/蓝绿统一主体方向，重新组合为：空军基地暖橙、克隆中心浅粉、指挥中心莓红、军工厂钢蓝、反应堆青绿、战略中心紫色；无人机跟随军工厂，平台蓝灰。所有主体、设备、点缀和框架色值来自三张用户色卡。

当前版本[SSF_Reference_A_v3](../../ArtSource/Buildings/SSFStyle_20261005/Review_A_v3.md)有42张2K效果/正交图、十套图板、两张4K原尺寸组合和纹理已打包的参考Blender文件；[冻结清单](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v3/reference_manifest.json)及[版本修改核对](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v3/revision_validation.json)记录图纸哈希、同机位结构和旧版本保持。10个原网格、7套骨架和22个动画基线留存，尚未重制成品或实际减面。

A-v1/A-v2为历史，原共同配色收到修改要求；A-v3待用户针对整套具体图纸决定，B未开始。正式UE目录尚未创建本批资产，原包保持。助手[视觉检查](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v3/visual_qa.json)与技术验证单独记录，均不代替A/B；相应[开发](20261005-SSF建筑美术统一与三档LOD.md)和[配色增量](../Archive/20261005-SSF建筑六套独立配色参考A_v3.md)为当前入口。此项资产方向不新增全局配色规则，规范仍v1.3。

## 2026-10-05 SSF 建筑浅色化参考 A-v4

用户随后明确“深色的颜色再浅一些，不要有太深的颜色”。据此保留六座独立色系与分件配色区域，将深紫、深蓝、深青、深莓色及机械框架提亮；内部线和外轮廓改为中亮蓝灰`#5F8A9E`，三档明暗系数由`0.40 / 0.72 / 1.00`改为`0.78 / 0.90 / 1.00`。主体/设备/点缀的基础色目标L*至少65，框架至少62；这是基础色值约束，不将其冒充最终渲染像素亮度。已较亮的橙色、奶油色、粉色保留，新增HEX与原卡对应关系见[40项派生色记录](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v4/palette_revision.json)。

当前[SSF_Reference_A_v4全套参考](../../ArtSource/Buildings/SSFStyle_20261005/Review_A_v4.md)已同步更新42张2K效果/正交图、十套图板、两张4K组合及打包纹理的参考Blender场景。[冻结清单](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v4/reference_manifest.json)SHA256为`6d3af05f70ea2aa2d6ff860c0b1d7b4af5547cd3ea5d23e3ac17bbbcd4e0d81b`，79个跟踪文件哈希核对一致；[历史与结构核对](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v4/revision_validation.json)确认A_v1/v2/v3各64个历史文件保持，以及10个源网格的几何、权重、尺寸、配色区域和相机相同。[助手实际读图记录](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v4/visual_qa.json)覆盖全套40核心视图、2补充顶视及2组合图，不代替用户决定。

A_v3按本次反馈修改后再审，A_v4待具体版本审核，B未开始；未重制、减面、生成生产LOD或正式导入UE。22动画兼容及项目镜头辨识留在成品验收阶段。此为本批配色修订，规范仍v1.3；版本边界与资源清单见[新增归档](../Archive/20261005-SSF建筑浅色化参考A_v4.md)及[当前开发记录](20261005-SSF建筑美术统一与三档LOD.md)。

## 2026-10-05 SSF 仅深色提亮参考 A-v5

用户最新明确“深色调浅，浅色别动”。当前[A_v5全套参考](../../ArtSource/Buildings/SSFStyle_20261005/Review_A_v5.md)以A_v3原色为基准，仅提亮原深色；原橙、奶油白、浅粉、浅蓝、蓝绿中间色的HEX保持，三档因子恢复并保持`0.40/0.72/1.0`，撤销A_v4全局暗部抬亮。共40项配色角色中18项提亮、22项保持，原深色描线与平台仍变浅；这不修改其他资产或全局默认规则，规范仍v1.3。

[真实Blender材质回读](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v5/palette_preservation_validation.json)检查90个材质，46个受保护浅色材质的基础色及90个三档色阶保持原方案；无全局曝光或gamma改变。修改深色线稿会影响边缘像素，不将材质原值保持误记为整图逐像素相同。[冻结清单](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v5/reference_manifest.json)SHA256为`f3a3d853bef64c1b4fba8132dd3e403a75b201d86401b513deb19769c11f48b9`，80个跟踪文件核对一致；[历史核对](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v5/revision_validation.json)确认A_v1/v2/v3/v4冻结文件和10资产结构/相机保持。

42张2K单图、十套图板及两张4K组合已同步更新，[助手读图记录](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v5/visual_qa.json)覆盖40核心视图、2补充顶视及2组合。A_v4按最新反馈修改后再审，A_v5待用户审核，B未开始；未进行成品重制、减面、生产LOD或正式UE导入。下一步见[当前开发记录](20261005-SSF建筑美术统一与三档LOD.md)及[浅色保持增量](../Archive/20261005-SSF建筑仅深色提亮参考A_v5.md)。

## 2026-10-05 SSF 附图基准与两座局部提亮 A-v6

用户最新附图明确“以你刚刚调的这一版为准，军工厂和战略中心的深色需要调浅，其他别动”。实际附图与归档A_v3总览像素一致，已原样留存，[基准确认](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v6/baseline_selection.json)记录附件SHA256和选择依据。当前[A_v6整套参考](../../ArtSource/Buildings/SSFStyle_20261005/Review_A_v6.md)读取实际A_v3参考场景，仅将军工厂深紫设备/框架提亮为`#9972CA / #756FB5`、战略中心深紫主体/框架提亮为`#AA6EB9 / #8A67B6`。两座浅色、其余四座及全部配套保持A_v3；无人机不随本轮军工厂改色，原描线`#1A182F`及三档因子`0.40/0.72/1.0`不改。此次限定范围覆盖此前A_v4/v5全面提亮方向，历史记录继续保留。

[实际限定修改核对](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v6/scope_preservation_validation.json)记录90个实际材质图中5个改变、85个相同；5个为四项配色角色及军工厂设备别名，其他八资产72个材质图相同，其34张原始视图与A_v3字节一致。全套仍有42张2K单图、十套图板、两张4K组合及10纹理已打包的参考blend；[保存后材质回读](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v6/palette_preservation_validation.json)检查实际RGB、色阶及图签名。[助手实际读图](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v6/visual_qa.json)覆盖40核心视图、2补充俯视和2组合，两座修订图板另行放大检查。

[冻结清单](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v6/reference_manifest.json)SHA256为`cd3cde73b94a6b2b034a306064be1e726a25075ba5910721b16d3dfad129341f`，84个跟踪文件核对一致；[历史与结构检查](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v6/revision_validation.json)确认五个旧版本冻结文件、10资产几何/权重、尺寸、机位和组合布局保持。用户选定修改基准不记为A_v6通过；当前A_v6待审核、B未开始，未制作生产LOD或正式导入UE。22动画兼容及实际项目镜头验收仍留在成品阶段，全局美术规范仍v1.3，见[新增局部修订归档](../Archive/20261005-SSF军工厂与战略中心局部提亮A_v6.md)。

## 2026-10-05 SSF 线稿与三档明暗可见性修正 A-v7

用户反馈“线稿和三档明暗好像没加？”。实际重新打开A_v6并渲染关闭/单独显示对照，确认线稿和三档均已接入，但军工厂该Hero机位暗部只占0.70%，总览内线及外轮廓偏细。[实际拆分图](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Style_Breakdown_A_v6.png)与[节点回读](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v7/A_v6_native_node_audit.json)分别记录可见结果和连接，不能只凭节点存在判断风格已表达充分。

当前[A_v7完整参考](../../ArtSource/Buildings/SSFStyle_20261005/Review_A_v7.md)保留A_v6全部40项HEX、90个材质基础RGB及三档RGB、光向、法线、几何和机位。色阶阈值改为0.38/0.68，三档因子仍为0.40/0.72/1.0；2048px资产图结构线/外轮廓为2.2/4.4px，内部遮罩强度0.65，组合图使用1.6/3.0px，避免小建筑成为黑团。[同机位前后对照](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Style_Comparison_A_v6_to_A_v7.png)可检查线条与大色阶变化；[实际分区记录](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v7/style_visibility_validation.json)中军工厂该机位亮/中/暗为48.11%/38.46%/13.43%，仅适用于此视图。

42张2K视图、十套图板和两张4K组合已同步重出，10纹理保持打包；最终场景保存后重开，[90材质RGB核对](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v7/palette_preservation_validation.json)无差值。[冻结清单](../../ArtSource/Buildings/SSFStyle_20261005/References_A_v7/reference_manifest.json)SHA256为`d1d3dcabdac432395edd1554da297de47e555a94c9e6ff77893043a7487c0173`，137跟踪文件核对一致，六个旧版本和10资产结构/机位保持。A_v6依反馈修正为A_v7，当前A_v7待审核、B未开始；外轮廓仍是参考Freestyle，生产规则线稿图集、描边壳、三档LOD、22动画兼容及正式UE材质留在后续阶段。此修正回应原定风格，不新增全局规则，规范仍v1.3，见[增量归档](../Archive/20261005-SSF线稿与三档明暗可见性修正A_v7.md)。

## 2026-10-05 SSF A_v7制作放行与实际成品 B_v1

用户明确“开始制作，严格一比一按照参考图和原模型制作”，作为已展示SSF_Reference_A_v7的制作放行，[A决定](../../ArtSource/Buildings/SSFStyle_20261005/approval_A.json)固定清单 `d1d3dcabdac432395edd1554da297de47e555a94c9e6ff77893043a7487c0173`，不记为尚未展示B的通过。七个参考版本原样保留。

当前[实际Blender B_v1](../../ArtSource/Buildings/SSFStyle_20261005/Review_B_v1.md)交付十类资产、792分件、355骨骼、三档LOD、独立线稿与真实描边壳，保持A_v7全部配色及三档因子。42张2K效果/正交图、参考同机位/三档LOD对比、两张4K组合、全部22原名完整动画预览与MP4回读完成；6003源姿态留存，实际变形抽样最大位置差5.888243316e-6m。

原结构保护下[24档本体差额](../../ArtSource/Buildings/SSFStyle_20261005/Production_B_v1/Budget_Exceptions.md)未达原上限，例外未批准；描边预算达标但部分覆盖/线宽弱于参考Freestyle，提交用户B决定。实际成品SHA256 `6f386c2e4782ce6ab6e81ae0f2380a6345fa05cdc6fc3dcf9d260c13ed3a806e`，冻结清单 `47fa0b49979828e331ca702d526f18d65b63ff639bd72404390288f6894adf29`；[当前核对](../../ArtSource/Buildings/SSFStyle_20261005/DeliveryValidation_B_v1.json)记录496个交付文件哈希保持。

B与预算例外待审核，正式UE导入、导出回读、引用/物理资产/无人机蓝图和项目指挥官镜头均未执行；资源预算和Blender检查不替代实战帧率。全局规范仍v1.3，增量见[归档](../Archive/20261005-SSF建筑Blender成品B_v1与预算例外.md)。

## 2026-10-06 当前 B_v1 正式资源入库

用户查看实际Blender成品后明确“传ue，作为住正式资源存GuLiStrike中，暂不接入游戏中”。[本次放行](../../ArtSource/Buildings/SSFStyle_20261005/approval_B_import_20261006.json)只针对当前`SSF_Production_B_v1`的正式资源存储；Blender SHA256为`6f386c2e4782ce6ab6e81ae0f2380a6345fa05cdc6fc3dcf9d260c13ed3a806e`，冻结清单为`47fa0b49979828e331ca702d526f18d65b63ff639bd72404390288f6894adf29`。这是具体版本B存储放行，包含已披露差额，不推导全局预算上调、玩法或性能验收。

正式目录`/Game/GuLiStrike/Buildings/SSFStylized`已保存105个资源：10网格各3LOD、7套骨架/355骨骼、6源物理资产副本、22原名动画、38贴图、4母材质、16材质实例、1无人机蓝图副本和1独立机械动画压缩设置。原商城包、A_v1–A_v7及496个冻结B交付文件保持。源无人机未指派物理资产，正式副本继续不伪造物理资产。

30档UE网格回读最大几何差9.53674316e-06m，权重差6.19888306e-06；22动画×3LOD×起/中/末共198组实际组件对照最大位置差0.107709347cm、缩放差3.81655967e-07、旋转差0.02797342°。对照基线为原可编辑动画按原运行采样率重采样后的局部四元数插值，原有1/2fps低采样动作保持；原压缩噪声差另行记录。 骨架参考姿态按源恢复，三档UV/法线/材质区段及屏幕阈值保持；独立ACL设置使用0.001cm误差阈值和100cm虚拟顶点距离，保护机械活动件，原压缩设置未改。原时长、采样率、帧数、4条可编辑变换曲线与`Destoy`/`Edle`名称保留。

保存后独立重载105资产，原商城包引用为0；依赖只保留新目录、Engine、ACL和既有Script插件。54张实际UE图覆盖10资产×3LOD、六座35m/25°、300–700m/55°和1500m/55°预览；资源预览在未保存Entry世界执行，未编辑战场地图或游戏引用。固定光向、三档着色、内部遮罩、近中档壳、远档淡出和Base Color/Team Color接口已建立并读图检查。

[正式交付入口](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/README.md)、[机器清单](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/formal_delivery.json)、[独立重载/姿态/截图](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/ue_validation.json)、[UE回读](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/ue_mesh_readback.json)、[视觉记录](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/visual_qa.json)。

24项本体预算差额和部分壳覆盖弱于参考的B_v1既有差异继续登记，原上限未变。当前版本已获存储放行；进一步减面、玩法接入、实战帧率和真实战场总览属于后续范围。本轮未运行PIE、联机或原生构建。上方参考/成品阶段的待审及未导入描述是历史事实，由本条最新放行与交付接续。


## 2026-10-07 蓝红阵营配色实际 Blender B_v2

用户最新附两张实际效果图，明确“蓝色方所有建筑改成图1配色，红色方把所有建筑改成图二配色，先放Blender给我审核”。此指令替代六座建筑各独立色系的旧方向：蓝方橙`#EE9D58`/蓝灰`#274E61`/奶油白`#FEE4D9`，红方莓红`#A34053`/浅粉`#E3B6B1`/橙`#EE9D58`；六座各制作两队副本，保持原功能分区。蓝方AirBase和红方CommandCenter直接沿用附图对应原调色，少量框架与线条保持原参考颜色；军工厂和战略中心的框架在新队色中使用蓝灰/莓红，不新增大面积近黑色块。Floor、Lamp、Light、Drone不在此次调色范围。

[实际审核B_v2](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/Review_B_v2.md)与已打开[Blender](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend)提供12个模型、36档本体、24档真实描边壳、12套兼容原骨架、12张打包2K基础色图集及75张原生渲染。默认场景`Review_Blue_Red_Buildings`左蓝右红；单模型场景与48张效果/三视图、36档同机位LOD样本可近看。原22个Action、线稿遮罩、三档明暗和原B_v1的全部可编辑分件保留。

[独立回读](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/native_validation.json)核对60个网格的几何/法线/UV/权重、36档配色和风格节点；[助手读图](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/visual_qa.json)覆盖12模型的48核心视图及36个LOD样本，两个原色锚点保持。来源B_v1 SHA256为`6f386c2e4782ce6ab6e81ae0f2380a6345fa05cdc6fc3dcf9d260c13ed3a806e`，当前B_v2 SHA256为`5cee0988de1a4201cd9eb60de9cbd30ccddd108090b99b3e86f50c4bf2d584b6`；[用户指令/附图](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/user_reference_instruction.json)保存原图和哈希。

当前B_v2待用户针对实际成品审核；B_v1的正式存储放行与105资源交付继续有效，不能推导B_v2已通过。**本轮未更新UE、不接入玩法**，原24项本体预算差额及真实壳局部覆盖差异继续登记，未重新减面或提高预算。此为资产队色修订，规范仍v1.3；[本轮增量归档](../Archive/20261007-SSF建筑蓝红阵营配色Blender审核B_v2.md)记录版本、验证和下一步。


## 2026-10-07 B_v2 蓝红阵营正式资源入库

用户看到实际Blender `SSF_TeamPalette_B_v2` 后明确“导入至ue作为正式资源”，已登记[本版本存储放行](../../ArtSource/Buildings/SSFStyle_20261005/approval_B_v2_import_20261007.json)，SHA256固定为 `5cee0988de1a4201cd9eb60de9cbd30ccddd108090b99b3e86f50c4bf2d584b6`。这是当前可见版本的正式资源放行，暂不接入游戏，不扩大为全局预算或性能验收。

[正式交付](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/README.md)新增48资源：12骨骼网格各3LOD、24材质实例、12张2K基础色图集，分入 `/Game/GuLiStrike/Buildings/SSFStylized/Blue` 与 `/Game/GuLiStrike/Buildings/SSFStylized/Red`。六座建筑均有两队配色，原B_v1保留；复用原正式6兼容骨架、6物理资产、21建筑动画、共用三档/描边/半透明母材质和6线稿遮罩。原无人机、平台、灯具及第22个无人机动画保持。

导出前147冻结文件哈希核对保持；36个B_v2 FBX独立回读后完成导入，36档实际UE存储网格再次导出回读。最大几何差 `4.86280396e-06 m`、权重差 `6.19888306e-06`，实际三档面数、法线和4UV配色/队色/远档淡出传输保持。只保存本轮Blue/Red归属包，未覆盖旧正式资源或商城包。

[独立UE重载](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/ue_validation.json)检查48资产、资源引用、骨架参考姿态、材质接口与21建筑动画的两队/三LOD/起中末共378组实际组件对照，最大位置/缩放/角度差 `[0.0, 0.0, 0.0]`（cm/无量纲/°）。84张实际2K UE图覆盖12模型×3档及35m/25°、300–700m/55°、1500m/55°独立资源镜头；[读图记录](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/visual_qa.json)与[原生图清单](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/preview_inventory.json)分别登记。预览使用未保存Entry世界，未接入游戏、改战场地图、运行PIE或做性能测试。

原24项本体差额（含平台/无人机）和真实描边局部覆盖差异继续记录；两队建筑各18档继承差额，不擅改主要结构或提高预算。上方B_v2提交时待审/未导入是历史事实，以本条具体版本放行与完成交付接续。规范仍v1.3，见[新增归档](../Archive/20261007-SSF建筑蓝红正式资源B_v2入库.md)。


## 2026-10-07 蓝红正式 B_v2 用户审核通过

用户在正式UE交付后明确“审核通过”，[本次最终审核记录](../../ArtSource/Buildings/SSFStyle_20261005/Acceptance_B_v2_20261007/README.md)固定版本为 `SSF_TeamPalette_B_v2`，来源Blender SHA256 `5cee0988de1a4201cd9eb60de9cbd30ccddd108090b99b3e86f50c4bf2d584b6`。蓝红各六座建筑、12网格各三档LOD、24材质实例、12图集共48新增正式资源已完成本轮美术与正式资源交付验收；规范v1.3保持。

本决定接续此前“导入至ue作为正式资源”的存储放行，确认当前实际成品的配色、结构、线稿、三档明暗与LOD。已披露的本体差额及局部描边差异随具体版本留档，原面数上限未提高。沿用已有36LOD回读、378动画姿态对照、84实际UE图及保存重载结果；本轮仅更新审核记录，没有重新导入、改模型或扩大验证。147个Blender冻结文件与196个UE交付证据文件哈希保持。

正式目录为 `/Game/GuLiStrike/Buildings/SSFStylized/Blue` 与 `/Game/GuLiStrike/Buildings/SSFStylized/Red`，继续复用B_v1正式共享依赖。原商城包、B_v1、平台、灯具和无人机保持，**暂不接入游戏**；实战性能、玩法和联机仍不在本次资产验收范围。前文待审和存储放行状态保留为历史，以本条最终通过决定为当前状态。


## 2026-10-08：v1.4 本地阵营配色与场景 UI 规则

用户明确要求“该ui不受任何环境影响”“玩家控制的永远是蓝色方，敌人永远是非蓝色方”，并批准实施完整计划；据此更新规范 v1.3→v1.4。新增本地敌我显示、脚环己方蓝/敌方红与世界20 cm环带、同色选择短标记、UI环境隔离、模型固定/可变分区与先审后接入规则。保留三档LOD、扫荡者无线稿和既有批准版本，不对已入库模型追溯重做。

`LocalTeamColorReview.20261008.v1` 提供四种Mass、八类玩法建筑和六座SSF共18组独立派生对照；[画廊与黄色候选分区](../../ArtSource/LocalTeamColorReview_20261008/REVIEW.md)共54张实际UE图，相机/材质/源引用逐模型记录。蓝红参考为 #2877DB / #D7534D，实际模型引用未切换。方向依据为本轮用户计划；这18个具体变色区与哨戒炮独立预览底色修复尚待用户核对，没有新成品“通过”决定。

该规则维护工作项的 done/passed 仅表示规范与台账维护完成，不代表候选配色通过。技术事实为素材保存与源码静态审查、对应地图17个实体保存重载回读；未执行原生编译、游戏运行、双客户端和性能验证。[本轮交付与边界](20261008-场景UI环境隔离与本地阵营配色.md)、[增量归档](../Archive/20261008-场景UI源码与红蓝模型对照交付.md)。

## 2026-10-08：v1.5 十三模型配色参考 A_v2

用户要求十模型重出参考图＋三视图，随后附四张色卡限定选配，再追加防空炮、哨戒炮、矿厂；据此新增资产专属参考方向并维护规范 v1.4→v1.5。选取奶油白主装甲、深灰机构、蓝灰／莓红连续队色区，整理碎线与小色块，原造型和活动关系保持。本次不扩大为其他资产的统一配色，不改 UI 功能色或旧审核事实。

[A_v2 参考交付](../../ArtSource/LocalTeamColorReference_A_v2_20261008/REVIEW.md)覆盖十三模型、二十六独立 1536×1024 图板；[分区及来源清单](../../ArtSource/LocalTeamColorReference_A_v2_20261008/review-manifest.json)记录固定／队色／功能区、实际源图和哈希。四张色卡原样归档，二十二张新增原生模型视图捕获未保存正式资源，蓝红配对由内置图像生成工具制作，助手逐模型读图后保留八张首稿修订历史。

[具体版本 A 状态](../../ArtSource/LocalTeamColorReference_A_v2_20261008/approval_A.json)全部待用户，尚无 A_v2“通过”决定；实际 Blender B 未开始、正式 UE 未更新。原 SSF B_v2 的 2026-10-07 入库与通过属于旧版本事实，不能推导新参考通过。[视觉记录](../../ArtSource/LocalTeamColorReference_A_v2_20261008/visual_qa.json)和[文件静态核对](../../ArtSource/LocalTeamColorReference_A_v2_20261008/delivery-check.json)分别留档；浏览器工具不允许 file 协议，交互未自动复核。二维设计不替代实际几何、成品或游戏效果验收。

本台账的 done/passed 仍只表示规则维护完成。当前制作阶段是 A 参考审核，后续按模型逐项取得用户具体版本决定再施工。见[正式需求](../RequirementDocument/20261008-十三模型配色参考与三视图A_v2.md)、[开发交付](20261008-十三模型配色参考与三视图A_v2.md)和[新增增量归档](../Archive/20261008-十三模型配色参考A_v2交付.md)。

## 2026-10-08：v1.6 十四模型配色参考 A_v3

用户批准在原四张色卡中加入固定米砂与深浅两档队色，减少乳白并重设计全部原十三模型（含指挥中心、战略中心）；制作中明确补入重防号，扩展为十四模型、二十八蓝红图板。当前方向取代上方 v1.5 的后续候选，不改已入库 SSF 的历史通过事实。规范 v1.5→v1.6，本批资产使用乳白 `#FEE4D9`、米砂 `#D5C09C`、机构深灰 `#2C3735`；蓝浅／深为 `#6AA4BE`／`#274E61`，红浅／深为 `#A34053`／`#662249`。两档队色与三档明暗分别管理，乳白20–30%是按原组件提出的设计目标，非精确测量结论。

护盾原顶部三点纳入队色灯，红版顶部不再保留蓝／青点；空军基地顶部现有扇板增加队色。用户后续纠正前哨“以组件为单位赋予色彩”，覆盖初始 A_v3 的按高度分段方案：中央最高宽矩形整件浅队色、相邻矩形整件米砂、外围窄矩形整件乳白、外部单个半圆环整件深队色。同一件从顶到底、侧面与背面保持基础色，被否定的高度稿仅作修订历史。

[A_v3 新参考交付](../../ArtSource/LocalTeamColorReference_A_v3_20261008/REVIEW.md)提供十四模型、二十八张1536×1024图板、十四份分区配置、放大画廊与总览。四十次成功图像生成及十二张未选稿留存；[助手读图](../../ArtSource/LocalTeamColorReference_A_v3_20261008/visual_qa.json)与[文件来源回读](../../ArtSource/LocalTeamColorReference_A_v3_20261008/delivery-check.json)分别记账，204个本地静态链接核对，A_v2冻结130文件保持。复用二十二原生视图和三份历史报告，本轮没有再次操作编辑器。

[具体 A_v3 状态](../../ArtSource/LocalTeamColorReference_A_v3_20261008/approval_A.json)全为 pending，实际 Blender B 为 not_started；来源模型几何、装配、动画与 LOD 是后续施工依据，二维生成图不作尺寸或像素等同保证。本地浏览器交互未自动验证，沿用此前工具拒绝file协议的限制，没有更换入口绕过。当前只交付参考；没有更新正式 UE、修改运行接口、编译、运行游戏或新增测试地图。本台账维护完成不代表美术审核通过。

范围和最新决定见[需求](../RequirementDocument/20261008-十四模型配色参考与三视图A_v3.md)、[开发交付](20261008-十四模型配色参考与三视图A_v3.md)及[新增归档](../Archive/20261008-十四模型配色参考A_v3交付.md)。重防号沿WM01/UnitTypeId=2定位，WarMachine引用保留，不混同玩家Ground。

## 2026-10-08：十四模型实际仅配色 B_v1 与参考对齐

规范继续沿 v1.6 四卡固定／队色方向。用户明确开始 Blender 实际制作，限制只改配色，并追加全模型三渲二、三档明暗和适量线稿、完成后自行对照参考与三视图的要求；已据新授权推进 B，不把旧 A_v3 pending 改为逐板通过。资产执行细则补入规范9.2，旧参考与增量归档原样保留。

[十四模型蓝红实际成品](../../ArtSource/LocalTeamColorProduction_B_v1_20261008/PaletteOnly_14Models_BlueRed_B_v1.blend)含29场景、28候选及实例总览；[实际原生四视图与 A/B 对照](../../ArtSource/LocalTeamColorProduction_B_v1_20261008/reference-comparison.html)共112张。助手实际逐模型读图并修正护盾顶部三点队色灯、上部护盖、空军基地顶甲、先驱号主壳、彼之矛踝甲、矿厂柜门深色范围、SSF机构和固定护甲分区。护盾150面顶灯两队分别使用 #6AA4BE／#A34053，红方不留蓝／青点；前哨保持按整件分区。

[单模型回读](../../ArtSource/LocalTeamColorProduction_B_v1_20261008/Reports/native-save-readback.json)与[总文件回读](../../ArtSource/LocalTeamColorProduction_B_v1_20261008/Reports/combined-save-readback.json)保留原网格、UV、法线、权重、变换、骨架、动画曲线和 LOD 数据；原 SSF B_v1 与当前入库 B_v2 的三十网格签名一致。只读导出原两炮塔与矿厂原坡道，不保存正式 UE。来源哈希及完整修订／剩余参考几何偏差见[逐模型记录](../../ArtSource/LocalTeamColorProduction_B_v1_20261008/visual_qa.json)。

全体 B 待用户审核。统一三档材质0.40／0.72／1.00，保留既有线稿；新增 Freestyle 基于原网格，只在 F12／原生交付图显示，材质预览不显示此补线，未新增可导出描边壳。没有 UE 替换、编译、游戏运行或自动化测试，动画播放与实战效果未验证。本地 HTML 静态核对成功不代替受限制的浏览器交互或用户美术决定。

见[新需求](../RequirementDocument/20261008-十四模型仅配色Blender成品B_v1.md)、[开发交付](20261008-十四模型仅配色Blender成品B_v1.md)、[本次交付归档](../Archive/20261008-十四模型配色Blender成品B_v1交付.md)。本台账维护完成不代表任何 B 成品通过。


## 2026-10-08：v1.7 模型 ID 与统一分区接口

用户批准[统一模型目录与本地阵营改色](20261008-统一模型目录与本地阵营改色.md)，规范1.6→1.7增加唯一Excel维护入口、稳定ModelId、区域Alpha与CPD8–17契约及参数实际值读取。当前84模型／103部件已完成源表迁移，原玩法数值与网络身份保持；14个B_v1和共用施工体的候选分区准备，旧SSF Team Color绑定原标记区。

[Alpha候选保存回读](../../ArtSource/ModelInterface_B_20261008/blender-saved-readback.json)证明14模型／34网格的原B几何、RGB、UV、法线、权重、变换和分区保持，护盾150顶灯面角色7。原交互Blender未重载；共享函数只在Review保存，原生候选接线尚未执行。扫荡者与两类占位建筑遮罩为明确待补项，没有登记成生效区域。

[资源与装配核对](../../Data/Models/editor-static-validation.json)、[源代码和源表记录](../../Data/Models/static-source-review.json)、[三图32实体保存回读](../../Data/Models/acceptance-scenes-readback.json)分别留档。新USTRUCT／ModelId字段未在当前模块加载，[导入预检](../../Data/Models/import-preflight.json)在写资产前停止。没有编译、运行游戏、自动化测试或正式新涂装导入；全部新B及接口候选仍待具体审核，不改原SSF B_v2与LOD已审事实。本规范台账的done/passed仅表示规则维护完成。

## 2026-10-08：v1.8 当前成品正式存储与黑块提亮

用户明确要求“编译，然后模型表需要加个‘描述’字段…导入新DataTable并验证运行效果”，随后指令“记得将刚刚开发的美术资源导入至UE的正式资源中，并将旧资源删除”。[本次存储授权](../../ArtSource/ModelInterface_B_20261008/formal-import-authorization.json)固定当前十四模型B_v1源SHA256 `c28e175b95f6393a51256392097dd8244e17fc8dee82b85ef78d2c03aa1067e0`，包含共用彼之矛施工体。该授权接续上方待审/未编译历史，不改写旧归档或把存储放行自动登记为最终美术通过。

原生构建退出0，84模型Description、四张Models及五张依赖DataTable已导入；[正式迁移](../../ArtSource/ModelInterface_B_20261008/formal-promotion.json)覆盖17网格和实际矿厂蓝图，70材质/函数移动到正式共享库。删除旧SSF重复红方资源18项及另外20项零引用旧涂装，继续保护被引用的描边、功能、动画和骨架依赖。14模型/34网格源几何、RGB、UV、法线、权重和原变换沿既有回读保留，UE只将颜色区编码至Alpha；原生显示包围盒修正不改变CPU/GPU几何或防空炮12倍导入设置。

用户最新“这些黑块太深了，再浅一些”引入v1.8本批暗部修订：22模型主体母材质保留固定色卡、三档与轮廓，降低过黑阴影和内部描线。运行残留黑簇通过只隐藏OreHISM确认是矿石，分别隐藏地面破坏/悬浮Niagara不消除它；此前仅凭截图将其当粒子的推测已纠正，未修改Niagara。矿石四个正式材质保留原顶点底色、原发光和亮面，岩壳使用#2C3735、晶体暗面使用#274E61/#662249补光；[实际前后图](../../Artifacts/ModelRegistryRuntime20261008/ore-rock-comparison.json)与[保存读回](../../Artifacts/ModelRegistryRuntime20261008/ore-rock-lighter-revision.json)留档。

[本轮画廊](../../ArtSource/LocalTeamColorUE_B_v2_20261008/index.html)含15入口120张实际UE蓝红四视图及4张矿石前后图，相机、gamma2.2和源网格相同；未对PNG重绘。助手逐模型检查护盾三点、前哨组件、空军基地顶甲及固定区，当前暗部幅度待用户反馈。本地浏览器交互未自动验证，不绕过文件访问限制。

运行核对覆盖双方指挥官、晚加入Ground、真实建筑复制、归属变化、实际MID/CPD和选择/清除；六种环境下指定不透明敌环ROI颜色相同。长时间三客户端/服务器及大量捕获发生Windows提交内存不足，恢复编辑器与保存资源后缩小预览范围；不能记为长期稳定或性能通过。三个旧模型遮罩仍待补，Ship只完成保存装配回读，详见[当前开发](20261008-统一模型目录与本地阵营改色.md)和[本次增量归档](../Archive/20261008-模型目录正式入库与暗部提亮.md)。本规范台账done/passed仅指规则维护完成。

## 2026-10-08：v1.9 全项目统一色库与制作入口

用户本轮要求整个游戏适用所附配色，确认全部22色组成统一色库、允许跨图组合，并保留现有功能色例外；随后限定“当前任务不动游戏项目，只改skill和相关文档”，并批准执行计划。执行前规范已从规划时的v1.4推进到v1.8，因此保留第9／10节及此前历史，递增为v1.9，不回写旧版v1.5。

- 已将准确sRGB色值、全项目范围、主辅点缀选择、同系明暗派生、功能色例外和已审版本兼容写入规范第11节；本台账不重复色表。
- 模型、特效、地编、UI与卡牌skill增加或更新规范入口；卡牌项目版和用户级副本及其美术prompt指导同步，冻结prompt保持。
- 项目AGENTS增加全项目美术入口，`gulistrike-progress`明确色表只由规范维护。已有模型Excel／ModelId／区域参数继续作为运行配置来源，本轮不修改其源表或资产。
- 验证：六个项目skill及用户级卡牌skill共7个通过现有quick_validate校验；22个HEX的数量、顺序和唯一性与用户四图标注一致，卡牌两份skill及prompt指导字节相同，115个相关Markdown目标无缺失。文档索引build成功，全量check为0错误、16条既有篇幅／任务列表提示；通过范围仅为文档维护，不涉及成品视觉、游戏运行或性能。

用户附件对应关系、改动清单和验证结果见[本次增量归档](../Archive/20261008-全项目统一色库与美术技能接入.md)。后续新建或明确重配色的资源按规范制作；既有批准及功能色例外保留，不自动重做现有资源。

## 2026-10-09：v1.10 扫荡者原橙区队色与占位范围收缩

用户要求不制作克隆兵营占位2004、领地据点占位2007，只补扫荡者；先提出参考图→Blender审核→UE，随后明确“不用出图了，就这橙色的区域作为team colour就好了”。A新图明确免除、原橙色分区直接确认，B仍待审核，未沿用之前十四模型存储许可提前导入本次扫荡者。

[实际候选及审核说明](../../ArtSource/SweeperTeamColor_v1_20261008/REVIEW.md)记录源文件及哈希；[保存回读](../../ArtSource/SweeperTeamColor_v1_20261008/blender-saved-readback.json)覆盖5456／1355／220三档源几何、法线、UV、原颜色、权重和持久变换，蓝红共享网格。原橙色UV遮罩包括721615个纹理像素，原固定黄灯、白甲和机构像素不在遮罩内；四视图固定内部RGB差为0，纹理边界和抗锯齿外延单列。

当前已在交互Blender打开原／蓝／红并排成品；切换前旧文件已保存且无脏修改。未知的UE LOD1差异仍记录为既有1355/1363导出边界，不借此次改色重建模型。没有新参考图生成、正式UE写入、编译或游戏运行；原生导出辅助尝试只保留未执行草稿，本轮功能修改撤回。新资产A区域决定、B待审、UE未运行与规范维护通过分别登记。见[本次增量归档](../Archive/20261009-扫荡者原橙区队色Blender候选交付.md)。

## 2026-10-09：v1.11 扫荡者B_v1正式导入与自动改色

用户在展示B成品后明确要求“导入 UE、接入自动改色。”，[版本授权](../../ArtSource/SweeperTeamColor_v1_20261008/formal-import-authorization.json)绑定 `Sweeper_OrangeTeamColor_B_v1.blend` SHA256 `6ff9ea43b21702b6a820bafbca0e69e5e715ec5302c28128086af431ba938144`，不重复请求B放行。原橙区可变、其他原配色固定；新增精确UV0灰度遮罩，保留正式网格Alpha，避免原低LOD跨色面整面替换。

[正式资源回读](../../ArtSource/SweeperTeamColor_v1_20261008/formal-ue-import.json)确认同一原生网格、5456／1363／220三档顶点属性、UV、法线、蒙皮和socket保持；原Body材质仅新增CPD8–11浅队色与16启用，替色后继续原三档明暗和刚性WPO。未重导入网格、没有阵营N套模型，原底色贴图及无线稿例外保持。源Excel和三张DataTable更新并保存回读；2004／2007按要求关闭改色，候选参数和区域移除，稳定模型ID和资源保留。

[双方实际Q召唤](../../ArtSource/SweeperTeamColor_v1_20261008/UE/runtime-local-team-readback.json)核对每真实阵营5个扫荡者，各客户端己方蓝、敌方红；登记归属刷新、未知阵营中性灰、外部CPD清零后自动恢复通过。Mass动画步长63和实际WPO保持；25张固定视图及4张真实召唤批次图见[UE画廊](../../ArtSource/SweeperTeamColor_v1_20261008/UE/index.html)，固定内部UE像素最大差1／255、边界单列，未把Blender零差结论套到UE输出。

Mass地图增加4个EditorOnly审核对象，保存并重新载入、默认CPD读回通过。当前接入无需新C++或编译，沿已有明确运行授权做短时双客户端核对；已结束本次PIE。没有新增自动化测试、重测全部UI环境、晚加入或长期性能；文件浏览器交互仍未验证、不绕过限制。规则维护、用户正式导入放行、具体运行核对和最终观感分别记账，见[本次增量归档](../Archive/20261009-扫荡者橙区正式导入与自动改色.md)。
