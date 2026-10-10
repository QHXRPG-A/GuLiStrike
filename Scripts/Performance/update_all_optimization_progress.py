"""Reconcile current four plans; preserve prior text and append an immutable archive."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'outputs/performance/20261009-all-optimizations'
assert json.loads((OUT/'final-saved-scene-readback.json').read_text())['saved']
assert json.loads((OUT/'static-source-build-check.json').read_text())['success']
evidence = json.loads((OUT/'whole-build-comparison.json').read_text())
assert len(evidence['samples']['before']) == len(evidence['samples']['final']) == 3
prior = OUT/'prior-plans'
prior.mkdir(exist_ok=True)
archive_name = '20261009-四项PIE全部优化应用与整版对照.md'
common = '''
## 构建、验证和保存交付

用户在完整计划中授权源码编译、专服双客户端、三轮性能及指定功能回归；后续明确“直接应用所有优化，并补齐整版对照”。原玩家会话已结束后执行。源码引擎 `D:/UnrealEngine-5.7`、目标 `GuLiStrikeEditor Win64 Development`，最后构建退出码0；引擎/项目BuildId均为 `dd3ee083-a0fd-45c8-814e-67fe5ef95e31`，当前加载DLL与最后源码构建一致，之后只修改脚本/资产/文档。没有恢复旧代码或重编引擎目标。

[静态与构建检查](../../outputs/performance/20261009-all-optimizations/static-source-build-check.json)覆盖World/完整身份/Generation/Epoch、GC资产引用、缓存失效、稳定候选/结算顺序、数组退出清理、源文件接口及脚本语法。相关现有回归优化路径21/21、Actor回退1/1、Ship查询回退3/3通过；仅扩充既有数学/Ship用例断言，没有新增测试文件或框架。

真实采矿暂停/最新目标恢复/停止，施工Visible→Suspended→Visible保留ScanStartedAt并完成后Inactive，HUD空提交/重挂载/F+2，Ship四节点两面板的失去/恢复控制和重生，中途加入补建/来源独立均有运行回读。技术检查与玩家外观/交互确认分别记录。

最后保存 `/Game/Maps/LVL_CommanderMassPrototype` 并回读24个默认停用预览Actor、3个指南、1个观察相机、四来源标记及6个正式特效引用，共28个相关实体。视觉区采矿/建造行Y=-20500/-18500、Z=1500，新All列X=-4800；枪口/命中All位于(-11200/-9600,-16500,1500)。客户端 `gs.Perf.Review start PR_Mining_All__Actor`、`PR_Construction_All__Actor`、`PR_Flash_AllMuzzle__Actor`、`PR_Flash_AllImpact__Actor`，用 `gs.Perf.Review stop`停止当前World预览。原Opaque/Muzzle/Impact入口供对照；预览默认不自动启动。

真实建造区(15000,72000,902)复用菜单、工厂定义6、红方建造兵和资源规则；四来源区(0,65000,902)在专服使用 `gs.Flights.Load 500 45`，每来源125，`gs.Flights.Stop`停止补充。指挥官使用现有选择/移动/停止/改令入口；Ship席位使用现有控制/重生入口。预期是原玩法判定正常、离屏恢复最新表现、UI正确跟随且无旧Pawn或过期HUD残留。

## 整版帧率与统计边界

普通双客户端启动三段CSV平均Frame为23.35/27.76/31.59ms，即 **42.84/36.02/31.66 FPS**。保持正常生产、默认角色/镜头，不注入600移动单位、500弹丸或特效预览；两视口1280×720、质量3，采集时关闭后台/Slate限速以获得完整采样。人口随正常生产增加，帧率逐步下降。用户此前双客户端15–20FPS是粗略历史参考，当前可粗略看作接近两倍，不能换算成精确累计百分比。

相同服务器压力入口下，追加前整版三轮平均 **14.54→15.53 FPS，约+6.8%**；Frame/GT **68.79/68.80→64.41/64.40ms**。该追加前版本已经包含上一轮四项主要优化；没有恢复计划实施前版本。压力为600移动单位、四来源共500服务器弹丸、每客户端32持续光束/32闪光，10秒热身、30秒有效采样、三轮，服务器人口/视口/镜头/质量固定。

客户端实际接纳/显示量与可靠事件积压不同，整合版显示更多弹丸和命中特效；结果是整个交付版在相同服务器入口下的表现，不能冒充相同客户端工作量的纯算法倍率。GPU平均 **10.24→16.62ms**，Frame P95均值 **86.60→89.36ms**；没有证实P95改善，主要仍受GT限制。整进程物理工作集 **4814→9148MB**、虚拟使用 **18275→16810MB**，同时受进程驻留、资产缓存与接纳表现量影响，保留原始值，不直接归因于单项或推算池内存节约。

诊断轨迹按原始QPC窗口/完整引擎帧归一化，移除导出器混入的GPU专用行，区分GT独占、含子项与所有线程工作量；等待单列，P95按World/LocalPlayer记录。无恢复代码；未完成窗口/被诊断工具干扰的中间样本不参与最终三轮结果。

## 当前证据与历史入口

- [整版完整结果](../../outputs/performance/20261009-all-optimizations/whole-build-results.md)、[三轮帧率/内存/World计数](../../outputs/performance/20261009-all-optimizations/whole-build-comparison.json)。
- [新旧可播放对照](../../outputs/performance/20261009-all-optimizations/visual-review/index.html)、[正式引用与消费者](../../outputs/performance/20261009-all-optimizations/formal-reference-final-readback.json)、[保存地图实体/入口](../../outputs/performance/20261009-all-optimizations/final-saved-scene-readback.json)。
- [运行功能](../../outputs/performance/20261009-all-optimizations/runtime-functional-review.json)、[施工/Ship/中途加入](../../outputs/performance/20261009-all-optimizations/runtime-lifecycle-final-review.json)、[已有回归](../../outputs/performance/20261009-all-optimizations/final-tests/results.json)。
- [本次增量与节点勘误](../Archive/20261009-四项PIE全部优化应用与整版对照.md)、[上一轮分项实施](../Archive/20261009-四项PIE性能优化实施与对照交付.md)、[上一轮视觉确认](../Archive/20261009-四项特效视觉确认与正式引用切换.md)。上一轮单项收益只用于其当时负载；旧16/8节点结论已作废，不能与当前整版收益相加。

代码、资产、构建、指定运行检查和保存交付已完成；玩家效果反馈与未达/未证实的性能目标单独保留，状态为 `verification / partial`，不表示追加代码尚未应用。
'''
plans = [
('20261009-PIE游戏线程耗时与避障候选查询优化.md',
 'A0–A4及稀疏格索引已生效；整版600单位查询平均1.25–1.32ms、批次P95 1.85–1.99ms达标。追加优化全部应用，普通双客户端约32–43FPS；待玩家避障效果确认。',
 '在LVL_CommanderMassPrototype确认交叉、停止、改令及环境边缘行为，记录玩家反馈；整版与压力结果见本次归档。',
 '''# PIE游戏线程耗时与避障候选查询优化 — 当前实施

## A0–A4已应用

| 阶段 | 当前实施 | 保持的合同 |
|---|---|---|
| A0 | World级CPU Scope、Grid/Query/Solve与候选/强制更新/访问计数 | 默认不额外采集、不刷逐兵日志 |
| A1 | 普通单位只入中心格，删除重复去重；环境跨格用可复用代次，溢出清零 | 稳定身份、参与和高度过滤保留 |
| A2 | 普通单位XY平方距离与平方重叠 | 环境仍使用表面净距，威胁排序不替换为单纯距离 |
| A3 | 单位前12、环境前2，环境优先，最终消费12 | 重叠→预测碰撞时间→距离→稳定键保持 |
| A4 | 同执行连续快照与工作数组复用；有序非空格索引跳过空格 | 原矩形覆盖和XY桶顺序保持，不缩小高速/大体型范围 |

源文件：[Policy](../../Source/GuLiStrike/Commander/Mass/Navigation/GuLiCommanderAvoidancePolicy.cpp)、[Processor](../../Source/GuLiStrike/Commander/Mass/GuLiCommanderPredictiveAvoidanceProcessor.cpp)。格索引在建格完成后建立，查询期间不改变桶；快照不跨执行冻结权威状态。

保留30Hz三相、SoldierId相位、低帧率最多追赶三步、同次每兵最多一次、改令版本强制更新、静止障碍、预测避障与软避障职责。2.5秒预测、迎面靠右、停止友军被动让行和0.5秒软避障尺度保持。

## 预算与行为核对

当前整版三轮600移动单位查询平均 **1.245/1.286/1.316ms**，活动批次P95 **1.853/1.950/1.987ms**，满足平均≤1.6ms/P95≤3ms。按服务器World和引擎帧统计，不是单兵P95。短时完整消费前缀核对0差异；VerifyPrefix的核对开销排除在性能采样之外。相关现有避障回归通过。

上一轮同负载局部查询回退约7.89–8.41ms、优化约1.26–1.52ms，是独立避障比较；当前整版追加前后都已开启避障优化，因此整版+6.8%不是避障全阶段累计降幅。整个压力帧仍包含Ship物理弹、命中Niagara、复制、Slate、StateTree与等待。

局部回退 `gs.Avoidance.QueryOptimizations 0`、`gs.Avoidance.SparseCells 0`；核对 `gs.Avoidance.VerifyPrefix 1`默认关闭，正式性能窗口保持0。没有通过减少人口、关闭预测层或降低单兵调度制造收益。

## 任务状态

- [x] A0聚合Scope/计数和默认静默。
- [x] A1普通重复去重移除、环境代次复用。
- [x] A2普通XY平方距离与环境净距合同。
- [x] A3前12/前2/最终12及稳定威胁排序。
- [x] A4快照/数组/有序非空格查询复用。
- [x] 保留覆盖、预测/软避障、三相与改令强制行为。
- [x] 前缀核对、源码构建、相关回归及整版三轮测量。
- [x] 最后保存现有Map并回读实体/入口，更新本次归档和索引。
- [ ] 玩家确认交叉、停止、改令和环境边缘的实际行为。
'''),
('20261009-采矿激光与机枪闪光性能优化.md',
 '运行时暂停/裁剪已生效；7.5/12cm Opaque、真实8节点、Beam GPU、无表现碰撞/求解及独立闪光全部正式应用。ID36/45/5/52和池ID4/38已回读；旧16/8节点测量作废，待新组合玩家反馈。',
 '在LVL_CommanderMassPrototype与可播放对照确认新8节点GPU/去表现碰撞组合的扫描、端点和机枪反馈，记录视觉与实战反馈。',
 '''# 采矿激光与机枪闪光性能优化 — 当前实施

## 正式资源与明确视觉变化

用户先前认可的是80节点CPU、不透明加宽且保留Spark碰撞的版本；本轮“直接应用所有优化，并补齐整版对照”授权继续接入全部组合。当前配置如下，原先视觉确认不冒充新组合已由玩家逐帧确认。

| 用途 | 当前正式路径 | 宽度 / 原基础比例 |
|---|---|---|
| 采矿ID36 | `/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized_GPU_All` | Beam 5→7.5cm；禁用Beam001 1→1.5cm；Scale1 |
| 建造ID45 | `/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized_GPU_All` | Beam 8→12cm；禁用Beam001 2.5→3.75cm；Scale1 |
| 命中ID5 | `/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_AllOptimizations` | 原基础Scale2 |
| 地面/机械枪口ID52 | `/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_AllOptimizations` | 原基础Scale1；重防号动态×2保持只应用一次 |
| 飞行激光池ID4/38 | `/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool_SmallBatches` | 运行每批256；ID4 Scale1，ID38 Scale(0.3,1,1)保持 |

采矿与建造分别处理，建造不共用ID36。沿用绿色/紫色与功能色，未重新配色。重防号是WM01/UnitTypeId2，地面机甲是独立Ground席位。原商城资源、上一交付副本保持供对照和局部回退。

主体为Opaque+Unlit，颜色接Emissive、深度遮挡和Ribbon结构保持；Alpha转换为 `max(0, RibbonWidth * saturate(Color.a))`宽度收束。制作写最终绝对1.5W，不叠乘；历史采矿25cm冲突已统一为真实基准5cm。蓝图使用ID36/45、空直接Asset、相对Scale1，正式数据比例不重复放大。

## P0–P4当前生效内容

共用LOD `ShouldRenderWorldEffectBounds`复用每帧视图；采矿/建造完整枪口—端点范围、扫描、弹体和历史尾迹分别计算。任一合格本地视图可见即准入。持续光束Inactive/Visible/Suspended，0.05秒独立检查、全不可见0.15秒宽限、恢复最新任务；建造保留扫描相位。瞬时闪光生成前裁剪，屏外枪口回池，去重、过期、墓碑及结束仍推进，回屏不补播旧事件。

两种启用主Beam现在均为 **实际8节点/GPU**，禁用Beam001结构与状态保持。Beam的SolveForcesAndVelocity关闭；Spark的表现Collision关闭。光束本来没有Collision，取消的是端点火花与场景的碰撞。服务器碰撞/伤害保持；火花现在不再弹碰场景，这是明确视觉行为变化。

枪口/命中独立保留Glow、Flash、RibbonCore、RIbbonTrailFollower、Sparks、Debris六层及生产/跟随事件，只取消Sparks/Debris表现Collision。4096cm未缩放可信发射包络涵盖速度、重力、生产/跟随寿命、尺寸曲线及相机偏移；消费者按基础比例只缩放一次，动态模拟Bounds保持。未知未认证模板仍保守放行，不以任意小范围误裁剪。

[组合制作脚本](../../Scripts/Vfx/apply_all_tool_optimizations.py)、[编译/配置回读](../../outputs/performance/20261009-all-optimizations/combined-vfx-build.json)、[小批次绑定](../../outputs/performance/20261009-all-optimizations/small-laser-batch-build.json)。正式Excel只改6个ResourcePath单元格，经既有27表导出与Vfx单表导入；52行、基础比例、双枪口绑定、Catalog ID5和Cook依赖均回读。

## 节点独立比较勘误与当前性能

重新读取Niagara全部RI存储发现，上一轮名为Nodes16/Nodes8的主Beam实际仍80，只有禁用Beam001的数值改动。**旧“16/8节点收益不稳定，故不采用”的独立测量不能证明真正16/8的成本，现已作废。** 原始采样和归档不改写，通过本次勘误保留历史。

制作脚本已同时更新拥有EmitterUpdate的RI与复制System RI并断言每个编译存储一致；四个CPU16/8候选已修复/编译/保存。组合GPU_All在本次三轮采样之前已确认所有Beam存储为8。修复后的CPU16/8独立收益未重新测量，不填写通过或把旧数字挪给新资产；正式采用的是真实8节点组合，已有三轮整版测量。

同QPC窗口诊断，采矿+建造GT系统独占约 **1.82→0.146ms/帧**；这是8节点/无Spark碰撞/GPU组合的成本变化，不能说成不透明材质单独节省92%。枪口/命中事件接纳量不同，整组VFX成本不能直接按旧活跃量对比。整版GPU上升和压力P95未改善如后文所列，收益边界保留。

## 可播放与视觉检查

新旧八段真实PIE对照共168帧，包含开始、持续/峰值与停止：[播放器](../../outputs/performance/20261009-all-optimizations/visual-review/index.html)。采矿绿色、建造紫色、不透明主体显示正常，停止后主体无残留；闪光峰值在最早帧，六层/事件和基础比例回读保持。随机火花未固定，单颗火花位置不作差异标准，画面瞬时FPS不作性能证据。

用户“直接应用所有优化”作为正式切换授权，与前次明确视觉认可分开记录。新组合的玩家视觉反馈尚未记录。技术、性能和用户视觉验收分别列账；不再次要求重复切换许可。

## 任务状态

- [x] P0实际ID/宽度/材质/蓝图/Excel回读，纠正建造ID45。
- [x] P0绝对1.5W、Opaque+Unlit、原宽度曲线与停止包络。
- [x] P1完整Bounds、瞬时生成前裁剪与回池。
- [x] P1持续暂停、独立唤醒、最新目标/扫描相位及完成停止。
- [x] P2独立枪口/命中，六层/事件/比例保持，应用去表现碰撞。
- [x] P3真实8节点、无Beam求解、无Spark表现碰撞正式应用。
- [x] P3修复80/16/8候选真实RI值，旧节点独立收益结论作废。
- [x] P4兼容Beam GPU正式应用，Spark保持CPU；三轮整版成本记录。
- [x] 经Excel正式接入ID36/45/5/52与池ID4/38，52行/消费者/Cook回读。
- [x] 编译/静态/指定生命周期验证与新旧八段播放对照。
- [x] 最后保存现有Map验证区并回读默认停止、比例和触发入口。
- [ ] 玩家确认新组合端点、扫描、遮挡和机枪反馈。
'''),
('20261009-场景UI来源注册与绘制缓存优化.md',
 'A–D全部生效，新增背景/World/HUD/面板四叶缓存和分区修订失效；来源稳态发现0，HUD/裁剪/Ship控制重生及加入回读通过。整版和保存交付已补齐，待玩家外观交互确认。',
 '在LVL_CommanderMassPrototype确认四叶缓存后的屏边/近面裁剪、建造预览、HUD期限及Ship面板外观交互，记录玩家反馈。',
 '''# 场景UI来源注册与绘制缓存优化 — 当前实施

## 四阶段全部应用

新增 [UGuLiSceneUISourceRegistry](../../Source/GuLiStrike/Commander/UI/GuLiSceneUISourceRegistry.h)，按客户端World维护弱来源、类别、代次及Membership/Content/Pose修订。每World一次初始化，生成/销毁、组件重建、晚到PlayerState、重生/席位切换接通；Pending只重试小范围未就绪来源。Widget重挂载读取快照，稳态全世界来源发现0。选择、本地阵营色/MID和投影仍属于LocalPlayer，不跨World共享。

选择、归属、血量、路线、资源、Mesh变化触发修订；最终插值与机械姿态发布Pose。活动血条、路线和面板继续跟随与期限处理。`EndHUDFrame()`与Begin组成待提交/已提交双缓冲，所有HUD返回由作用域提交；空提交清空，F内容有效至F+1、F+2主动过期，无Viewport也不遗留永久旧HUD。

固定Ring64/Halo48/Disc32与预览模板复用三角/索引；按完整来源身份/代次、视图及内容/姿态修订缓存投影批次。完整图元范围预裁剪；屏边、中心在屏外但图元仍相交、横跨屏幕和近裁剪面保持完整裁剪路径。索引60000上限、业务顺序、绘制层次保持。

Capture名单、捕获内容和Slate几何分离：名单只随成员/阵营/Mesh修订重建，RT动画捕获继续更新；建造预览单独更新。MID/RT引用仍由反射对象持有，缓存不使Render线程资源被GC释放。

## 新增四叶节点及分区缓存

[Widget](../../Source/GuLiStrike/Commander/UI/GuLiSceneUIWidget.cpp)已从单一绘制叶追加 **Background / World / HUD / Panels** 四个内部叶，分别维护投影批次/几何缓存与WorldRevision/HUDRevision/SurfaceRevision。世界内容只失效World/背景，HUD提交/到期只失效HUD，面板/MID只走自己的Surface修订；相机、Viewport矩形、尺寸/变换和拥有关系变化使相关全部失效。

覆盖面板使用SPanel的同基准Layer绘制子项，保留原0/1/2绘制顺序；不因为默认Overlay逐子增加Layer而改变UI遮挡。NativeDestruct和重挂载清理全部分区缓存，焦点、输入和捕获流程保持。

`gs.SceneUI.SplitLeaves 1`默认启用，0在Rebuild/重挂载时选择原单叶；运行中改值不迁移现存叶对象。`gs.SceneUI.ProjectionCache 0`回退投影批次。开关是局部实现回退，不代表恢复完整旧UI或关闭来源注册。

## 功能与成本

| 核对 | 当前结果 |
|---|---|
| 来源/晚身份/重挂载 | 动态来源加入/移除及身份恢复正常，发现0 |
| HUD空提交/F+2 | Rect1→空0、重挂载有效、未续提后到期0 |
| 横跨屏幕/近面 | 完整裁剪返回可见，保持图元范围语义 |
| Ship控制/重生 | 四节点/两面板→无Pawn零面板→重控恢复→新Pawn恢复 |
| 中途加入 | 新客户端World独立注册、本地拥有/颜色分离、发现0 |

本轮高运动整版每客户端Update约0.17–0.24ms，Paint约0.16–0.20ms；两客户端不能将P95相加，Capture/子项与Update不可重复累加。Slate整个本地操作在单窗口约4.56→2.52ms，但整版同时改动多项、Editor布局/处理量也影响，不能将该下降全部归给拆叶。没有独立拆叶三对加速倍率结论。

## 任务状态

- [x] A：World弱来源及一次初始化、完整生成/销毁/重建/晚身份生命周期。
- [x] A：Widget重挂载快照，World/LocalPlayer分项与默认静默。
- [x] B：选择/归属/血量/路线/资源/最终姿态修订。
- [x] B：活动逐帧跟随、HUD双缓冲/空提交/F+1/F+2。
- [x] C：模板/投影批次、完整范围裁剪、层次和索引合同。
- [x] C：Capture名单/内容/动画/几何分离与反射资源持有。
- [x] D：四叶节点/分区修订缓存和Paint失效正式应用。
- [x] D：重挂载/拥有/相机变化失效及原单叶局部回退。
- [x] 源码构建、相关回归、指定生命周期及整版三轮测量。
- [x] 最后保存现有Map/入口并回读，更新文档与归档。
- [ ] 玩家确认屏边、近面、建造预览、HUD和Ship面板实际外观/交互。
'''),
('20261009-非Mass飞行弹丸六项性能优化.md',
 'A/B/C及全部追加项已应用：曲线预计算、候选访问戳、256/32 Niagara批次、Ship类别快照。稳定槽/网络/权威合同保持；整版、21+1+3回归和生命周期通过，压力P95目标未证实，待玩家效果反馈。',
 '在LVL_CommanderMassPrototype用四来源入口确认反弹、双枪历史、尾迹/槽复用、加入及Epoch效果；保留压力P95未改善记录。',
 '''# 非Mass飞行弹丸六项性能优化 — 当前实施

## 阶段A/B/C与追加项全部应用

保持单一GuLiStrike运行模块，预测器/池句柄仅内部使用；原效果ID、可靠创建/结束、每批16条/1000字节、Generation/Epoch墓碑及权威命中不变，不添加周期位置同步。地面机甲Ground与重防号WM01兵种分路由。

| 项目 | 当前实现与合同 |
|---|---|
| 1 空Actor/共享预测 | 普通C++预测器支持直线、随机曲线、追踪、反弹/停止/插值；纯Niagara/激光没有空Actor逐帧变换，Ship网格保留Actor；每次推进的显示位置供渲染复用，屏外预测继续。 |
| 2 身份/配置缓存 | 每World/表现批次缓存成功和失败姿态；枪口另含武器槽、左右枪口、历史时间；激活解析固定配置，晚资源保留重试；Provider/Epoch/新批次失效，冷资产由反射对象持有。 |
| 3 数据池 | 稳定Index+Generation+Epoch、分类活跃索引/Free槽、交换删除修复移动项；结束淡出与旧Epoch清理，不把本地句柄送网络。 |
| 4 Niagara | 只准备活跃和上次槽位，清除退出槽与灯光；静态配置、动态修订和时间分离，同帧同修订去重复提交；存活心跳与上传修订分离，可见运动逐帧更新，尾迹/退役继续。 |
| 5 可见性 | 共用每帧本地视图完整Bounds，弹体/光束/当前及历史尾迹分别涵盖；瞬时事件去重/过期/结束继续，不重播屏外旧事件。 |
| 6 服务器 | Snapshot/Grid/Candidates/NarrowPhase/WorldSweep/Settle分项、域活跃索引及容量复用；Commander5Hz、其他30Hz，各域独立新鲜快照，士兵200ms运动历史与原结算顺序保持。 |

实现：[ClientFlightPool](../../Source/GuLiStrike/Gameplay/CombatEffects/GuLiClientFlightPool.cpp)、[Presentation](../../Source/GuLiStrike/Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.cpp)、[ProjectilePool](../../Source/GuLiStrike/Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.cpp)。Ship服务器仍用原ProjectileMovement和NotifyHit物理路径；没有把所有域合并成一份过期快照或删除权威碰撞。

## 追加1：曲线系数预计算

[FGuLiProjectileCurveCoefficients](../../Source/GuLiStrike/Gameplay/CombatEffects/GuLiCombatEffectTypes.h)在激活/服务器点准备时解析原FRandomStream序列的高度、侧偏、相位、频率及基向量，Actor适配器、数据池和服务器共享可选系数输入。保留随机取值顺序和数学运算序；空参数为原实现局部回退，`gs.Flights.PrecomputeCurves 1`默认启用。既有MathAndIdentity扩展负/零/正种子、移动目标、128步和退化方向等价断言，通过。

## 追加2：候选访问戳

服务器复用OrderedCandidates、CandidateVisits和uint32代次，溢出清零；保留原格子遍历、候选首次次序、等TOI稳定TargetIndex及结算顺序。`gs.Projectiles.VisitStamps 1`默认启用，0保留原TSet路径。Snapshot/访问戳只作用于当前域步骤，不让前域死亡/注册/Epoch变化被后域旧缓存覆盖。

## 追加3：更小Niagara批次

激光池每World冻结批次大小 **256**，复制项目系统的两处Burst绑定 `User.LaserSlotCount`，必须在Activate前写实际值；11字段数组维持同容量、空槽/灯光清理顺序保持。`gs.LaserPool.BatchSize 256`默认，32–1024，只对新空池生效，1024局部回退。正式ID4/38指向SmallBatches资源，原基础比例保持。

导弹每新批 **32**，`gs.MissileCluster.BatchSize 32`默认、范围8–64；保留全局64索引步幅和稳定句柄，批次固定自身Capacity，上传/粒子生成/尾迹历史前缀使用实际32。既有24采样、8通道、退役与LOD轨迹合同保持。减小数组不代表GPU只上传脏字节，仍按Niagara setter合同提交。

## 追加4：Ship物理弹的全目录漏点

[DamageLedger](../../Source/GuLiStrike/Battle/Combat/GuLiCombatDamageLedger.cpp)新增类别句柄索引，注册、注销、失效清理及World销毁同步维护。Ship Wingman Sweep只取得Wingman的新鲜快照并复用每Actor工作数组，不再每弹每帧读排序整个战斗目标目录。排序仍为原Kind/AuthorityId/Generation，命中TOI/稳定平局/伤害回调保持。

`gs.Projectiles.ShipWingmanSnapshotIndex 1`默认，0原全目录。新增GuLiShipProjectile_WingmanSnapshot Scope/World计数。既有Ship用例比较过滤前后快照/排序、实际新位置、注销重注册和命中结果，在开/关路径均通过。轨迹整帧归一化Ship Actor Tick约20.85→1.06ms，其中新快照Scope约0.50ms。

## 成本与验收边界

上一轮独立Actor→数据池三对均值合计中位下降16.36%，P95未达到10%；该结果仍属于旧独立场景，不能作为本轮全部追加项的累计收益。本轮相同服务器压力入口的客户端实际接纳量更多，当前每客户端表现均值约1.28–1.36ms、P95约2.64–2.87ms；纯算法平均/P95额外下降≥10%未得到恒定客户端工作量证实。

500入口为四来源各125、45秒运行时长。客户端网络接纳、预测结束与淡出不保证任意瞬间等于服务器500；真实各来源量、Actor容量128、数据容量768、槽复用/释放及可靠队列均存档。整进程内存值不能由容量直接推算，GPU/Frame/P95边界见整版数据。

## 任务状态

- [x] 阶段A：空Actor消除、姿态成功/失败与完整枪口身份缓存。
- [x] 阶段A：固定配置激活解析、同帧同修订提交合并。
- [x] 阶段B：普通预测器、显示计算复用和离屏推进。
- [x] 阶段B：稳定槽/Generation/Epoch与分类索引/反射冷资源。
- [x] 阶段B：曲线系数预计算及既有等价断言。
- [x] 阶段C：活跃/退出Niagara准备、静态/动态/时间/心跳分离。
- [x] 阶段C：激光256和导弹32小批次，原索引/灯光/尾迹合同。
- [x] 阶段C：服务器候选访问戳正式应用及TSet回退。
- [x] 阶段C：独立域快照/200ms历史/5–30Hz/结算顺序。
- [x] 补齐Ship类别快照漏点，保留移动组件及物理判定。
- [x] 源码构建、21/1/3相关回归、槽复用/加入/Ship生命周期核对。
- [x] 整版三轮、内存/GPU/GT与实际接纳量证据归档。
- [x] 最后保存现有Map及真实四来源/控制入口并回读。
- [ ] 玩家确认反弹、双枪历史、尾迹、槽复用、加入与Epoch实际效果。
''')]
manifest = []
for filename, summary, next_action, body in plans:
    path = ROOT/'Progress/DevelopmentDocumentation'/filename
    raw = path.read_bytes()
    original_hash = hashlib.sha256(raw).hexdigest()
    saved = prior/filename
    if not saved.exists():
        saved.write_bytes(raw)
    text = raw.decode('utf-8-sig')
    header = text.split('---',2)[1]
    note = '代码静态检查和源码构建通过，全部追加配置已正式应用，三轮整版/普通启动及指定运行回归已补齐；现有Map已保存并完成实体/引用核对。新组合玩家视觉与其他功能反馈待记录，压力P95改善未证实。'
    for key,value in [('summary',summary),('next_action',next_action),('status_note',note)]:
        header = re.sub(r'^'+key+r':.*$',key+': '+json.dumps(value,ensure_ascii=False),header,flags=re.M)
    header = re.sub(r'^status:.*$','status: verification',header,flags=re.M)
    header = re.sub(r'^verification:.*$','verification: partial',header,flags=re.M)
    final = '---'+header+'---\n\n'+body.strip()+'\n'+common
    assert path.read_bytes() == raw, 'Document changed while preparing update: '+filename
    path.write_text(final,encoding='utf-8')
    manifest.append({'path':str(path.relative_to(ROOT)),'before_sha256':original_hash,'after_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})

archive = ROOT/'Progress/Archive'/archive_name
assert not archive.exists(), 'Do not overwrite an existing archive'
archive.write_text('''---
schema: guli-progress/v1
id: ARC-20261009-009
work_id: ''
kind: archive
role: root
title: 四项PIE全部优化应用与整版对照
areas: [performance, commander, navigation, combat, resources, building, ui, ship, network, vfx]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-10-09'
updated: '2026-10-09'
summary: 全部追加优化正式应用，源码构建与21/1/3回归通过，普通双客户端约32–43FPS；同压力入口整版14.54→15.53FPS，接纳负载不同且P95未改善。保存28个场景实体，勘正旧16/8节点测量。
next_action: 在LVL_CommanderMassPrototype记录新组合视觉和实战功能反馈，继续关注重压力下的命中特效及可靠事件处理成本。
relations:
  work_items: [WORK-20261009-001, WORK-20261009-003, WORK-20261009-004, WORK-20261009-005]
  supersedes: ARC-20261009-007
status_note: supersedes仅勘正旧归档的16/8节点配置与独立收益结论；其他历史实施和采样事实保持。全部应用授权与旧视觉确认分别记录，玩家新组合效果反馈待完成。
---

# 2026-10-09：四项全部追加优化与整版交付

## 用户依据与当前结果

用户明确“直接应用所有优化，并补齐整版对照”，随后说明此前双客户端15–20FPS，不需要精确历史快照、不恢复代码。沿用完整计划对源码编译、专服双客户端及指定对照的授权；执行前的玩家会话已结束。

所有追加项已正式应用：UI四叶分区缓存、曲线预计算、访问戳、256/32 Niagara批次、真实8节点/GPU/无求解与表现碰撞，以及新发现的Ship类别快照漏点。普通双客户端当前三段 **42.84/36.02/31.66FPS**，人口正常增加；与历史15–20只作粗略参考。相同服务器压力入口整版 **14.54→15.53FPS（+6.8%）**，Frame/GT **68.79/68.80→64.41/64.40ms**。未恢复计划实施前版本，追加前已经有前轮主要优化。

## 变更与合同

| 文件/资源范围 | 本次变更 |
|---|---|
| GuLiSceneUIWidget.h/.cpp | Background/World/HUD/Panels四叶及分区缓存/失效；同基准Layer绘制、期限/重挂载/GC合同保持 |
| GuLiCombatEffectTypes、ClientFlightPool、FlightVisualActor、RuntimeSubsystem | 原随机顺序与数学合同的激活曲线系数缓存 |
| GuLiProjectilePoolSubsystem | 候选访问戳/有序数组、容量复用及原TSet回退 |
| LaserPresentation、MissileClusterPresentation | 256/32新批次、原64导弹索引步幅和11激光字段/灯光/尾迹合同 |
| DamageLedger、ShipProjectileLedgerBridge、StrikeProjectile | 类别目录维护，Ship每帧仅新鲜Wingman快照与Actor工作容量复用 |
| 项目Mining/Construction/CommanderWeapons/WingmanWeapons | 真实8节点/GPU/无求解/无表现碰撞组合，独立六层闪光、小批次绑定 |
| Excel→Json→DataTable | ID4/5/36/38/45/52六处ResourcePath，基础比例/效果ID/消费者/Cook依赖回读 |
| 既有Runtime/Ship测试 | 数学等价、类别快照/排序/注销重注册断言扩充；没有新增测试文件/框架 |
| Scripts/Performance 与 Scripts/Vfx | 整版冻结/采样、普通启动、组合制作、节点修复、实际帧播放及保存交付工具 |

仍为单一GuLiStrike运行模块；网络可靠创建/结束每批16条/1000字节不变，服务器碰撞/伤害保持，5/30Hz独立域和士兵200ms历史保持，Ship物理移动/NotifyHit保留，UI本地拥有/颜色/资源引用保持。原80节点CPU/原表现碰撞副本保留，未覆盖商城资源。

## 节点勘误：仅替代旧16/8结论

旧Nodes16/Nodes8经原生RI回读，主Beam实际仍80，只有禁用Beam001改变，原“16/8没有稳定收益”不是对实际16/8的实验。原归档/采样保留，通过本篇部分勘误说明；其余当时的运行时、避障、数据池、Opaque和正式视觉确认事实不作废。

制作修复同时写拥有EmitterUpdate与复制System RI，断言所有存储一致。四个CPU16/8副本已修复/编译/保存，独立收益未重测。当前GPU_All组合在整版采样前已确认主/次Beam全为8。本轮组合三轮可以描述组合成本，不能拆分成纯节点/Opaque/GPU单项收益。

## 性能和剩余限制

避障三轮平均1.245/1.286/1.316ms、活动批次P95 1.853/1.950/1.987ms，满足本场景预算。Ship Actor Tick诊断约20.85→1.06ms，新Wingman快照Scope约0.50ms。两种光束GT系统独占合计约1.82→0.146ms。

客户端实际接纳/显示量更多，可靠事件积压与可见命中特效量不同；当前诊断命中Niagara独占约7.96ms、Niagara组件约1.81ms，物理ProjectileMovement约3.15ms、Slate本地操作约2.52ms。局部大幅下降不自动相加为整帧下降；当前压力样本仍受GT限制。客户端纯表现平均/P95额外≥10%不在恒定渲染负载下被证实。

GPU平均10.24→16.62ms，Frame P95均值86.60→89.36ms；未证实P95改善。进程物理工作集4814→9148MB、虚拟18275→16810MB，驻留/缓存/实际接纳量不同，原始数据保留，不单凭这两个值认定数据池节约或泄漏。普通启动使用默认角色/镜头与生产、1280×720双视口、质量3，采集期间关闭后台/Slate限速；与用户历史范围只作大致参考。

每轮10秒热身、30秒窗口，固定压力三轮整版前/后三个有效样本；未完成和被诊断工具干扰的中间样本排除。CPU诊断按QPC完整引擎帧归一化、移除导出器GPU专用行，等待与业务工作分开。

## 构建、运行和视觉验证

源码引擎D:/UnrealEngine-5.7，GuLiStrikeEditor Win64 Development，最终退出码0。BuildId引擎/项目均dd3ee083-a0fd-45c8-814e-67fe5ef95e31，实际加载产物哈希保留；源码未在最终构建后再变更。脚本语法及源码差异检查通过。现有优化21/21、Actor回退1/1、Ship类别查询回退3/3通过。

真实采矿/施工暂停恢复停止、ScanStartedAt、HUD期限/空提交/重挂载、Ship四节点/两面板控制与重生、中途加入来源/补建均有运行回读。八段168帧可播放对照显示采矿绿色/建造紫色、开始/持续/停止主体与闪光；助手检查未见停止主体残留。新组合玩家视觉尚未确认。

原用户“认可，切换这四项引用”对应旧80节点CPU、保留火花碰撞；本轮直接应用授权对应真实8节点GPU、取消Spark/Sparks/Debris表现碰撞，原颜色/材质/Ribbon/闪光六层/比例保持。技术/性能/玩家视觉记录分开，没有以旧确认冒充新组合用户验收。正式表52行、6个资源、双枪口绑定、Catalog ID5与Cook依赖回读通过。

## 最后保存的Map与玩家步骤

Map `/Game/Maps/LVL_CommanderMassPrototype`，24个停用预览、3指南、1相机，共28相关实体，四来源标记另回读。All光束位于(-4800,-20500/-18500,1500)，枪口/命中位于(-11200/-9600,-16500,1500)。客户端使用 `gs.Perf.Review start PR_Mining_All__Actor`、`PR_Construction_All__Actor`、`PR_Flash_AllMuzzle__Actor`、`PR_Flash_AllImpact__Actor`，用 `gs.Perf.Review stop`停止。

施工区(15000,72000,902)使用现有建造菜单/定义6工厂与红方建造兵；转开镜头0.15秒后回屏应恢复最新光束且扫描相位保持，完成应停止。四来源区(0,65000,902)专服 `gs.Flights.Load 500 45`，观察反弹/尾迹/双枪历史与槽复用；Ship解除/恢复控制、重生后面板跟当前Pawn。指挥官交叉移动/停止/改令/环境边缘观察原避障；镜头越屏边/近面观察UI完整裁剪。玩家反馈待记录。

## 证据与资源定位

全部来自项目内既有资源副本，没有新增外部资源迁移。最终资源在 `/Game/GuLiStrike/FX/Mining`、`/Game/GuLiStrike/Buildings/Construction`、`/Game/GuLiStrike/FX/CommanderWeapons`、`/Game/GuLiStrike/FX/WingmanWeapons`，材质/事件生产跟随依赖保持，由正式Vfx表软引用纳入Cook。

- [整版结果](../../outputs/performance/20261009-all-optimizations/whole-build-results.md)、[全部样本/内存/World计数](../../outputs/performance/20261009-all-optimizations/whole-build-comparison.json)、[普通启动](../../outputs/performance/20261009-all-optimizations/normal-current/results.json)。
- [可播放对照](../../outputs/performance/20261009-all-optimizations/visual-review/index.html)、[组合编译](../../outputs/performance/20261009-all-optimizations/combined-vfx-build.json)、[节点修复](../../outputs/performance/20261009-all-optimizations/repaired-node-candidates.json)。
- [构建/静态](../../outputs/performance/20261009-all-optimizations/static-source-build-check.json)、[相关回归](../../outputs/performance/20261009-all-optimizations/final-tests/results.json)、[运行功能](../../outputs/performance/20261009-all-optimizations/runtime-functional-review.json)、[生命周期](../../outputs/performance/20261009-all-optimizations/runtime-lifecycle-final-review.json)。
- [正式引用](../../outputs/performance/20261009-all-optimizations/formal-reference-final-readback.json)、[保存Map实体](../../outputs/performance/20261009-all-optimizations/final-saved-scene-readback.json)。
- [避障开发](../DevelopmentDocumentation/20261009-PIE游戏线程耗时与避障候选查询优化.md)、[VFX开发](../DevelopmentDocumentation/20261009-采矿激光与机枪闪光性能优化.md)、[UI开发](../DevelopmentDocumentation/20261009-场景UI来源注册与绘制缓存优化.md)、[弹丸开发](../DevelopmentDocumentation/20261009-非Mass飞行弹丸六项性能优化.md)。
''',encoding='utf-8')
(OUT/'progress-update-manifest.json').write_text(json.dumps({'documents':manifest,'archive':str(archive.relative_to(ROOT))},ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'success':True,'documents':len(manifest),'archive':str(archive.relative_to(ROOT))},ensure_ascii=False))
