# 归档时间线

> 自动生成，请勿手改。运行 `progress_docs.py build` 刷新。

| 日期 | 归档 | 模块 | 验证 | 摘要 |
|---|---|---|---|---|
| 2026-10-24 | [单颗矿石导航完成与清理](../Archive/20261024-单颗矿石导航完成与清理.md) | resource, navigation, commander, performance | partial | 按用户明确指令完成工作项收尾，保留编译与性能证据，清理20个临时文件约580.27MiB，并准备提交当前项目变更。 |
| 2026-10-24 | [单颗矿石导航编译与帧率实测](../Archive/20261024-单颗矿石导航编译与帧率实测.md) | resource, navigation, commander, performance | partial | 用户追加授权后源码版Editor构建通过；12个受控PIE窗口未见持续平均降帧，单颗解除0.14–0.16ms，234颗分4帧；渲染同步长帧单独记录。 |
| 2026-10-24 | [单颗矿石异步导航实现与场景交付](../Archive/20261024-单颗矿石异步导航实现与场景交付.md) | resource, navigation, commander, performance | partial | 单颗碰撞/避障/导航解除、200ms合批与共享静态矿石索引已实现；静态检查和原图33实体保存回读完成，编译与玩家效果未验证。 |
| 2026-10-24 | [地面机甲限位稳定与转向同步](../Archive/20261024-地面机甲限位稳定与转向同步.md) | art, combat | partial | 改为先限角再生成瞄准旋转，开放空中右键转向，W和右键共用转身曲线；源码构建、冷加载与场景回读完成。 |
| 2026-10-24 | [地面机甲双枪臂150度限位](../Archive/20261024-地面机甲双枪臂150度限位.md) | art, combat | partial | 修复只限制枪口而未限制枪臂水平朝向的遗漏，双臂最终姿态各限±75°；编译、冷加载和地图实体核对完成。 |
| 2026-10-24 | [地面机甲W跟随镜头、两倍转身与移动降速](../Archive/20261024-地面机甲W跟随镜头与移动降速.md) | combat, network, input | partial | 已按反馈接入W镜头朝向、转身及动画加倍、移动与加减速降至30%；编译、冷加载、数据和地图实体核对通过，效果待用户。 |
| 2026-10-24 | [GuLi 独立地图设计器与当前图工程交付](../Archive/20261024-GuLi独立地图设计器交付.md) | map-authoring, map, resources, navigation | partial | 交付编辑器外地图软件和当前完整工程，源码版构建与只读导入预检查通过，正式地图保持原样等待用户设计。 |
| 2026-10-24 | [地面机甲数据驱动机动、脚部IK与右键越肩控制](../Archive/20261024-地面机甲数据驱动机动IK与右键控制.md) | combat, network, input | partial | 已实施四张机甲配置表、双腿IK、速度匹配、根曲线转身和右键镜头；编译、冷加载资产及场景实体核对通过，待玩家验收。 |
| 2026-10-24 | [地面机甲动画冷加载丢节点修复](../Archive/20261024-地面机甲动画冷加载丢节点修复.md) | combat, art | partial | 修复PIE全部动画缺失和枪口不跟随的冷加载故障，补正之前检查重建图掩盖问题的结论；完整重启后的直接资产/姿势检查通过，等待玩家复验。 |
| 2026-10-24 | [地面机甲动画修复与越肩视角](../Archive/20261024-地面机甲动画修复与越肩视角.md) | art, combat | partial | 修复混合空间空采样和单侧开火断线，实现越肩横移、独立双枪后坐和当前帧瞄准；原生及蓝图编译、姿势数据和场景读取通过，玩家效果待验收。 |
| 2026-10-24 | [地面机甲PIE观察者与试玩入口更正](../Archive/20261024-地面机甲PIE观察者与试玩入口更正.md) | art, combat | partial | FireReview旧入口未创建Hybrid会话，玩家成为Observer；改用受支持的CommanderMassPrototype和Ground席位后，实际占有新版机甲及新动画实例。 |
| 2026-10-23 | [地面机甲B2正式UE导入与资源核对](../Archive/20261023-地面机甲B2正式UE导入与资源核对.md) | art, combat | partial | 按用户明确授权将当前B2导入并接入正式Ground，14段动作适配新骨架，29新资源硬引用与蓝图编译核对通过，FireReview地图保存完成。 |
| 2026-10-23 | [地面机甲 A_v4 定稿与 Blender 成品候选](../Archive/20261023-地面机甲A_v4定稿与Blender成品候选.md) | art, combat | partial | 用户批准两张 A_v4 为建模定稿；制作可编辑 Blender B_v1、红蓝材质、刚性骨架和待机适配，保存真实网格三视图供用户审核。 |
| 2026-10-23 | [地面机甲原包接入准备与折腿参考 A_v4](../Archive/20261023-地面机甲原包接入准备与折腿参考A_v4.md) | art, combat | partial | 保存 MechaDestroyer 原包、整机及双枪适配候选和原生构建证据；参考迭代至仅改腿折角的 A_v4，尚未正式切换 Ground 或开始风格化网格制作。 |
| 2026-10-23 | [行进响应计时与导航收尾](../Archive/20261023-行进响应计时与导航收尾.md) | commander, navigation, performance, network, ui | passed | 新增全部选中单位起步延迟显示，源码版Editor构建成功并重开原地图；清理本会话临时文件，按用户明确指令将相关工作项登记完成/通过。 |
| 2026-10-23 | [工程车同类互通源码版编译](../Archive/20261023-工程车同类互通源码版编译.md) | commander, navigation, performance, network | partial | 用户后续明确要求编译，工程车同类互通源码版Editor构建成功，BuildId一致；已重开原地图并更新保存验证说明，未启动新版PIE。 |
| 2026-10-23 | [工程车同类取消让路避障与碰撞](../Archive/20261023-工程车同类取消让路避障与碰撞.md) | commander, navigation, performance, network | partial | 按共用工程车类别取消彼此让路、预测与软避障及移动碰撞；保留与其他单位和环境的交互。静态检查及原图场景交付完成，新源码尚未编译运行。 |
| 2026-10-23 | [矿车作业衔接与重防号转向修复](../Archive/20261023-矿车作业衔接与重防号转向修复.md) | commander, navigation, performance, network | partial | 根据旧构建PIE现场定位矿车绕矿、厂外卸货目标、工程路线衔接和任务结束问题，并修正重防号机械部件插值；源码版Editor编译成功，原图入口保存读回，新版本效果待玩家验证。 |
| 2026-10-23 | [工程车姿态平滑补齐与验证边界](../Archive/20261023-工程车姿态平滑补齐与验证边界.md) | commander, performance, network | partial | 矿车和建造车接入独立模型姿态平滑，实际提交节拍修正已编译成功；按用户要求停止继续运行验证，最终效果待玩家核验。 |
| 2026-10-23 | [满速让路、本机平滑与工程车血条及攻击目标](../Archive/20261023-满速让路与本机平滑及工程车血条目标.md) | commander, navigation, performance, network | partial | 触发即满速让路、本机位置及动画平滑、工程车选中血条和敌方攻击目标已编译并通过部分原图PIE实体采样；原图入口已保存，保留单位化、方格站位和既有性能网络优化。 |
| 2026-10-23 | [UE共享寻路恢复、工程车Mass与旧执行清理](../Archive/20261023-UE共享寻路恢复与工程车Mass及旧执行清理.md) | commander, navigation, performance, network | partial | 恢复UE共享NavMesh及旧连续移动，保留单位化与方格站位；工程车接入Mass，删除旧格子/Crowd执行，源码编译和原图PIE已运行。密集穿行仍慢，性能无回退及完整业务恢复未全部验收。 |
| 2026-10-22 | [PIE进入后据点状态误判修复](../Archive/20261022-PIE进入后据点状态误判修复.md) | commander, navigation, performance, network | partial | 修复据点赠送建筑从轻量对象视图读取能力而误判缺失的问题，保留单项读取优化；重新编译加载，实际PIE运行待玩家复核。 |
| 2026-10-22 | [3米格PIE启动修复](../Archive/20261022-3米格PIE启动修复.md) | commander, navigation, performance, network | partial | 修复出生围挡对格、旧NavMesh启动检查及普通600人清单/服务版本；编译与启动前数据检查通过，实际PIE待玩家确认。 |
| 2026-10-22 | [3米整数格地面导航编译与地图迁移](../Archive/20261022-3米整数格地面导航编译与地图迁移.md) | commander, navigation, performance, network | partial | 源码版Editor构建通过，原指挥官地图完成300厘米格迁移与10个玩家入口实体交付；未运行玩法或性能测试。 |
| 2026-10-22 | [8月归档清单](../Archive/8月归档清单.md) | project | not_applicable | 2026年8月24篇归档按原路径转存ZIP；本页保留原ID、文件、状态、字节数及SHA-256定位清单。 |
| 2026-10-21 | [9月归档清单](../Archive/9月归档清单.md) | project | not_applicable | 2026年9月250篇归档已按原路径转存ZIP；本页提供原ID、文件、状态、字节数和SHA-256定位清单。 |
| 2026-10-21 | [近期性能优化整合归档（10月18–21日）](../Archive/20261021-近期性能优化整合归档.md) | performance, network, commander, combat, navigation, ui, tools, presentation | partial | 将本轮7份需求、18份开发文档和25篇增量归档收束为三个入口，原文按字节保存于ZIP；响应目标与玩家验收仍未完成。 |
| 2026-10-16 | [CS-P2P整合归档](../Archive/20261016-CS-P2P整合归档.md) | network, commander, combat | partial | 本会话归档与交付收尾完成，保全93份来源及历次独有事实；清理实际范围、平台拒绝及GitHub交付另记。 |
| 2026-10-14 | [Artifacts 与 ArtSource 运行时安全清理](../Archive/20261014-Artifacts与ArtSource运行时安全清理.md) | tools | partial | 根据用户授权清理两个目录的非运行时文件，删除 18,868 个文件、1,573 个空目录，逻辑大小 64.597 GiB，仅保留 105 份被文档引用的 Markdown 说明。运行资源未改动，安全性依据静态引用核对，未运行游戏。 |
| 2026-10-11 | [Mass会话GitHub交付与临时文件清理受阻](../Archive/20261011-Mass会话GitHub交付与临时文件清理受阻.md) | commander, navigation, presentation, performance | partial | Mass优化、原地图与报告共104个文件已上传GitHub main，远端提交核对一致；87个临时文件约85.4 MB的删除被自动审批策略拦截，未删除任何文件。 |
| 2026-10-11 | [Mass优化固定应用与开关移除](../Archive/20261011-Mass优化固定应用与开关移除.md) | commander, navigation, presentation, performance | partial | 按用户最新指示将已采用的Mass优化固定应用，删除本轮运行时开关与旧预测回退；重新编译13.06秒成功、8份BuildId一致、8项自动化通过，地图入口同步移除旧切换操作。 |
| 2026-10-11 | [Mass避障简化、转向提速与网络固定对照](../Archive/20261011-Mass避障简化转向提速与网络对照.md) | commander, navigation, presentation, performance | partial | 完整组合在200及600+500同进程场景各三组配对通过，默认采用六邻居预测、共享索引、保守零缓存与三倍转向；编译、8项自动化、原地图入口及退出恢复完成，网络积压仍在。 |
| 2026-10-11 | [会话变更编译、地图收尾与临时文件清理](../Archive/20261011-会话变更编译地图收尾与临时文件清理.md) | commander, navigation, combat, presentation, performance | partial | 用户授权后源码Editor构建退出0，8份BuildId一致；原地图遗留对象与说明清理并保存读回，66个临时文件约268 MB已删除。未追加PIE或效果验收。 |
| 2026-10-11 | [地面避障容量耗尽导致专服退出的诊断与调整](../Archive/20261011-地面避障容量耗尽导致专服退出的诊断与调整.md) | commander, navigation, performance | partial | 专服新增Mass障碍时耗尽共享Crowd的1024个注册槽，触发致命断言；配置及原生默认提高至8192，未重跑验证。 |
| 2026-10-10 | [遗留发射路径清理与地图保存阻塞恢复](../Archive/20261010-遗留发射路径清理与地图保存阻塞恢复.md) | ship, combat, network, performance | partial | 遗留发射实现、绑定资产和文本已清理；关闭任务启动的额外编辑器后，原地图保存及客户端PIE入口恢复。 |
| 2026-10-10 | [未通过性能候选回退与CPU及GPU瓶颈核对](../Archive/20261010-未通过性能候选回退与CPU及GPU瓶颈核对.md) | presentation, combat, ui, performance | partial | 按用户决定移除四项未通过的新候选和共用重构，保留网络观测及此前已采用优化；源码构建成功、地图入口更新，原始CPU/GPU计时补成不重复的游戏线程账目。 |
| 2026-10-10 | [组合P95同局切换与车辆负载混杂定位](../Archive/20261010-组合P95同局切换与车辆负载混杂定位.md) | presentation, combat, ui, performance | partial | 新增18段同局ABA/BAB/AAA诊断，确认200单位之外的车辆模型变换阶段峰值在全关时也出现；目标查询仍变快，剩余Slate和线程调度原因未锁定，四项默认关闭。 |
| 2026-10-10 | [快照性能优化实施、网络采样与采用判定](../Archive/20261010-快照性能优化实施网络采样与采用判定.md) | presentation, combat, ui, performance | partial | 完成原生网络采样、四项候选与源码build4，保留211份完整捕获及198份有效正式窗口；四项新候选均未通过完整默认采用门槛，旧优化保持开启。原地图入口已保存读回，设置恢复且编辑器关闭。 |
| 2026-10-10 | [PIE性能基线、多线程方案与会话临时文件清理](../Archive/20261010-PIE性能基线多线程方案与会话清理.md) | presentation, combat, ui, performance | partial | 新增性能基线与快照多线程需求/开发文档，保存18份配对统计、屏外计数和技术/视觉证据；核验后清理2377个临时文件约21.466 GiB。并行实现与大规模屏外收益尚未验证。 |
| 2026-10-10 | [网络指标HUD仅编译交付](../Archive/20261010-网络指标HUD仅编译交付.md) | commander, ui, performance | partial | 用户授权仅编译；网络HUD源码Editor构建成功，8份模块BuildId一致，保存地图并关闭占用DLL的编辑器后未重新打开。 |
| 2026-10-10 | [客户端表现专属技能与枪口批量化交付](../Archive/20261010-客户端表现专属技能与枪口批量化交付.md) | presentation, combat, vfx, performance | partial | 新建项目客户端表现技能，ID52三档CPU批量资源及默认2正式接入。14项相关技术检查和18份CPU窗口完成，200单位26.4至34.0FPS；压力真实枪口因上游积压未接纳，收益未证实，玩家视觉待验。 |
| 2026-10-10 | [左上角网络指标与飞行事件积压显示](../Archive/20261010-左上角网络指标与飞行事件积压显示.md) | commander, ui, performance | partial | 现有HUD增加每秒客户端连接流量、可靠待确认和飞行事件队列数据；240行文本表及原地图说明入口已保存读回，用户决定稍后统一编译。 |
| 2026-10-10 | [约200单位移动交火与场景UI性能截帧](../Archive/20261010-约200单位移动交火与场景UI性能截帧.md) | performance, ui, combat, vfx, commander | partial | 约200单位移动交火补采30秒，整帧40.61ms、GPU16.31ms；Client1场景UI约1.6ms CPU、描边截帧2.22ms GPU。最差GT69.69ms中战斗表现集中更新26.32ms，枪口Niagara为主要长帧来源。 |
| 2026-10-10 | [Slate分窗Prepass布局与绘制补采](../Archive/20261010-Slate分窗Prepass布局与绘制补采.md) | performance, ui | partial | 源码版Live Coding加载编辑器诊断后完成双客户端30秒分窗补采。编辑器主窗口扣除Client1视口为2.141ms，Client1与Client2视口UI为1.053与1.138ms，Slate合计4.791ms；编辑器占Slate约45%，占本次GT约8%。 |
| 2026-10-10 | [车辆CharacterMovement与飞船僚机移动开销拆分](../Archive/20261010-车辆CharacterMovement与飞船僚机移动开销拆分.md) | performance, combat, wingman, presentation | partial | 剩余飞行物与表现管线记录；旧独立投射路径描述已清理。 |
| 2026-10-10 | [Slate物理窗口与客户端游戏UI耗时拆分](../Archive/20261010-Slate物理窗口与客户端游戏UI耗时拆分.md) | performance, ui | partial | 复用Client1实战30秒Trace并按902完整帧拆分绘制。编辑器主窗口扣除Client1视口为1.138ms，Client1/2游戏UI为0.965/0.690ms；1.962ms布局Prepass缺少分窗计时，不能认定大部分Slate耗时属于编辑器。 |
| 2026-10-10 | [服务器StateTree条件查询与集中调度瓶颈分析](../Archive/20261010-服务器StateTree条件查询与集中调度瓶颈分析.md) | performance, commander | partial | 复用实战30秒Trace与CSV，Mass StateTree摊销1.558ms/引擎帧，执行帧平均5.447ms、P95 7.034ms；条件检查占已记录内部成本61.64%，发现重复事实读取和推进组线性查找。 |
| 2026-10-10 | [Client1实战PIE截帧与游戏线程瓶颈分析](../Archive/20261010-Client1实战PIE截帧与游戏线程瓶颈分析.md) | performance, commander, combat, presentation, ui, vfx | partial | 解除用户现有PIE调试暂停并采集Client1所在进程30秒；平均30.11FPS，GT33.18ms、GPU12.97ms。主要成本为同进程服务器/双客户端Tick与Slate，慢帧包含StateTree峰值和渲染资源分配等待。 |
| 2026-10-10 | [三项客户端优化正式接入与PIE验收交付](../Archive/20261010-三项客户端优化正式接入与PIE验收交付.md) | presentation, vfx, performance, combat, commander, ui, wingman | partial | 全部新变更经用户直接应用授权正式接入，53行Effects及真实僚机Profile/预警保存读回；源码构建和相关双客户端技术检查通过，地图14实体已保存。待玩家PIE验收，FPS对照暂缓。 |
| 2026-10-10 | [三项客户端优化手动PIE审核入口交付](../Archive/20261010-三项客户端优化手动PIE审核入口交付.md) | presentation, vfx, performance, combat, commander, ui | partial | 现有地图新增14个审核实体并保存回读，提供单次手动PIE候选覆盖；双客户端批量命中及真实对地入口、退出还原已短启动核对。完整资源认证、功能、性能与用户视觉验收仍待完成。 |
| 2026-10-09 | [600移动单位500弹丸PIE截帧耗时分析](../Archive/20261009-600移动单位500弹丸PIE截帧耗时分析.md) | performance, commander, combat, ship, network, ui, vfx | partial | 正式新特效/逻辑已回读，600移动单位与500弹丸压力复现15.55FPS；真实普通/P95/最慢帧定位命中特效、可靠事件处理和Ship集中生命周期，GPU Ribbon单帧9.253ms。追加修复收益尚未验证。 |
| 2026-10-09 | [四项PIE全部优化应用与整版对照](../Archive/20261009-四项PIE全部优化应用与整版对照.md) | performance, commander, navigation, combat, resources, building, ui, ship, network, vfx | partial | 全部追加优化正式应用，源码构建与21/1/3回归通过，普通双客户端约32–43FPS；同压力入口整版14.54→15.53FPS，接纳负载不同且P95未改善。保存28个场景实体，勘正旧16/8节点测量。 |
| 2026-10-09 | [四项特效视觉确认与正式引用切换](../Archive/20261009-四项特效视觉确认与正式引用切换.md) | vfx, combat, resources, building, performance | passed | 用户认可采矿7.5cm、建造12cm不透明激光与原模块枪口/命中独立副本；ID36/45/5/52已经Excel管线正式切换，52行目录、比例、蓝图绑定及Cook依赖回读通过。 |
| 2026-10-09 | [四项PIE性能优化实施与对照交付](../Archive/20261009-四项PIE性能优化实施与对照交付.md) | performance, commander, navigation, ui, combat, resources, building, vfx, presentation, network, ship | partial | 四项全阶段确定性改造已实施并通过源码Editor构建，60段最终对照完成；避障达标、弹丸平均下降16.36%但P95未达标。激光1.5倍宽Opaque候选及Map已保存，用户视觉和正式引用切换待完成。 |
| 2026-10-09 | [非Mass飞行弹丸六项优化分析与开发文档交付](../Archive/20261009-非Mass飞行弹丸六项优化分析交付.md) | combat, presentation, vfx, network, performance | partial | 完成空Actor变换、查询缓存、活跃数组、Niagara提交、表现裁剪和服务器碰撞组织六项源码分析，交付分阶段开发方案及证据；未实施优化，未产生优化后性能结果。 |
| 2026-10-09 | [PIE耗时与避障候选查询分析](../Archive/20261009-PIE耗时与避障候选查询分析.md) | performance, commander, navigation, ui, combat | partial | 当前同进程专用服务器和两个客户端PIE共681帧基线确认游戏线程瓶颈；候选查询约2.62ms，占预测处理器87%。完成源码拆解与开发方案，优化和新代码效果尚未实施验证。 |
| 2026-10-09 | [会话文档同步、临时清理与GitHub推送](../Archive/20261009-会话文档同步临时清理与GitHub推送.md) | assets, art, data, rendering, ui, commander, ground_mech, ship | passed | 同步本会话当前文档与历史阶段，清理30个临时备份、缓存、日志及草稿，保留已放行源文件与审核证据；主变更已推送GitHub main，1151个LFS对象上传完成。 |
| 2026-10-09 | [扫荡者橙区正式导入与自动改色](../Archive/20261009-扫荡者橙区正式导入与自动改色.md) | assets, data, rendering, commander | partial | 用户放行扫荡者原橙区B_v1；正式UV遮罩和本地自动改色已接入，Excel与三表保存回读、双方真实Q召唤及原三LOD属性核对通过，两类占位改色关闭。 |
| 2026-10-09 | [扫荡者原橙区队色Blender候选交付](../Archive/20261009-扫荡者原橙区队色Blender候选交付.md) | assets, rendering, commander | partial | 按用户取消新参考图并选择原橙区的决定，交付扫荡者蓝红共享网格B候选和精确UV遮罩，三LOD保存回读及固定区对照通过，Blender已打开、B待审核。 |
| 2026-10-08 | [全项目统一色库与美术技能接入](../Archive/20261008-全项目统一色库与美术技能接入.md) | art, rendering, assets, vfx | passed | 将用户四张色卡确认为全项目22色色库并接入美术skill，允许跨图组合并保留功能色和已审资产例外；本轮仅修改skill和文档。 |
| 2026-10-08 | [十四模型配色 Blender 成品 B_v1 交付](../Archive/20261008-十四模型配色Blender成品B_v1交付.md) | art, assets, rendering | partial | 记录用户明确开始制作后的十四模型二十八蓝红实际候选、112张原生四视图、参考比对及源网格保持检查；全部 B 待审核。 |
| 2026-10-08 | [十四模型配色参考 A_v3 与三视图交付](../Archive/20261008-十四模型配色参考A_v3交付.md) | art, assets, rendering | partial | 原色卡的十四模型二十八张蓝红参考已交付，含重防号、整件配色前哨、护盾队色顶灯和空军基地顶甲；停在具体版本 A 审核。 |
| 2026-10-08 | [十三模型配色参考 A_v2 与三视图交付](../Archive/20261008-十三模型配色参考A_v2交付.md) | art, assets, rendering | partial | 从用户四张色卡制作十三模型二十六张蓝红参考图板，保存图板、分区、源图与画廊；停在具体模型 A 审核。 |
| 2026-10-08 | [场景UI源码与18组红蓝模型对照交付](../Archive/20261008-场景UI源码与红蓝模型对照交付.md) | commander, ui, rendering, art, assets, building | partial | 单容器Slate UI与本地配色源码交付，18组模型独立对照已保存预览，Mass地图17个相关实体重载回读通过；未编译及运行验收。 |
| 2026-10-07 | [Mass脚环20cm正式材质与环境影响核对](../Archive/20261007-Mass脚环20cm正式材质与环境影响核对.md) | commander, ui | partial | 正式脚环材质已保存为20cm世界径向宽度并核对四种半径的编辑器渲染，保留RGBA与外半径；实际材质仍启用场景雾化。 |
| 2026-10-07 | [SSF会话临时文件清理与全项目上传范围](../Archive/20261007-SSF会话临时文件清理与全项目上传范围.md) | art, assets, tooling | passed | 按用户要求清理SSF会话3140个临时文件约3.20GB，保留正式资源及995个源文件和证据路径，上传范围为所有项目变更。 |
| 2026-10-07 | [SSF建筑蓝红正式资源B_v2审核通过](../Archive/20261007-SSF建筑蓝红正式资源B_v2审核通过.md) | art, assets, rendering | passed | 用户明确通过当前SSF蓝红正式B_v2，48新增资源及三档LOD完成本轮资产验收，暂不接入游戏。 |
| 2026-10-07 | [SSF建筑蓝红正式资源B_v2入库](../Archive/20261007-SSF建筑蓝红正式资源B_v2入库.md) | art, assets, rendering | partial | 用户放行当前B_v2后将蓝红两队48资源正式入库，独立重载、36LOD回读、378动画姿态和84UE图完成，不接入游戏。 |
| 2026-10-07 | [SSF建筑蓝红阵营配色Blender审核B_v2](../Archive/20261007-SSF建筑蓝红阵营配色Blender审核B_v2.md) | art, assets, rendering | partial | 十二个蓝红建筑配色副本已在实际Blender打开，75张原生渲染和独立回读完成；B_v2待用户审核，UE保持B_v1。 |
| 2026-10-06 | [SSF建筑B_v1正式资源入库且暂不接入玩法](../Archive/20261006-SSF建筑正式资源入库且暂不接入玩法.md) | art, assets, rendering | partial | 当前可见B_v1获用户正式存储放行，105资源、三档LOD、22动画与实际UE预览交付；保留原包，不接入玩法。 |
| 2026-10-05 | [SSF建筑Blender成品B_v1与预算例外](../Archive/20261005-SSF建筑Blender成品B_v1与预算例外.md) | art, assets, rendering | partial | A_v7依用户制作指令放行；实际B_v1十类资产、三档LOD、22完整动作与回读交付，24档本体超额及用户B待审，正式UE未导入。 |
| 2026-10-05 | [SSF线稿与三档明暗可见性修正A_v7](../Archive/20261005-SSF线稿与三档明暗可见性修正A_v7.md) | art, assets, rendering | partial | 核对A_v6已接入线稿和三档但可见性偏弱，保持全部配色与结构修正为完整A_v7，附实际拆分和同机位前后对照，当前待审核A。 |
| 2026-10-05 | [SSF军工厂与战略中心局部提亮A_v6](../Archive/20261005-SSF军工厂与战略中心局部提亮A_v6.md) | art, assets, rendering | partial | 附图核对为A_v3，仅提亮军工厂与战略中心四项深色，其余八资产和两座浅色保持；交付A_v6完整参考，成品和正式导入未开始。 |
| 2026-10-05 | [SSF建筑仅深色提亮参考A_v5](../Archive/20261005-SSF建筑仅深色提亮参考A_v5.md) | art, assets, rendering | partial | 按深色调浅浅色别动的最新澄清，仅提亮原深色，原浅色和三档明暗保持；交付全套A_v5，成品建模和正式导入未开始。 |
| 2026-10-05 | [SSF建筑浅色化参考A_v4](../Archive/20261005-SSF建筑浅色化参考A_v4.md) | art, assets, rendering | partial | 保留六套独立色系，按用户反馈同步提亮深色、框架、线稿和三档阴影，交付完整A_v4参考；成品建模及正式导入未开始。 |
| 2026-10-05 | [SSF建筑六套独立配色参考A_v3](../Archive/20261005-SSF建筑六套独立配色参考A_v3.md) | art, assets, rendering | partial | 按每座建筑不同色系的最新反馈，从三张用户色卡组合六套配色并交付A_v3全套效果图和三视图；成品建模及正式导入未开始。 |
| 2026-10-05 | [SSF建筑参考A与结构基线交付](../Archive/20261005-SSF建筑参考A与结构基线交付.md) | art, assets, rendering | partial | 六座SSF建筑及四件配套的参考A_v2、原始几何与22动画基线已交付；A待用户审核，成品建模及正式导入尚未开始。 |
| 2026-10-05 | [指挥官三档LOD审核放行与正式资源切换](../Archive/20261005-指挥官三档LOD审核放行与正式资源切换.md) | commander, art, assets, rendering, building | partial | 六兵种具体成品B已通过，76个正式资源组和三档引用已切换；获准源码Editor构建重开及真实彼之矛审核Actor保存，运行验收未执行。 |
| 2026-10-05 | [指挥官三档LOD纠正与候选资源交付](../Archive/20261005-指挥官三档LOD纠正与候选资源交付.md) | commander, art, assets, rendering | partial | 六兵种三档候选、完整资源组、真实预览和审核Map已保存；规范v1.3及相关历史说明勘误，正式切换待本版本B放行。 |
| 2026-10-05 | [彼之矛三档LOD进一步减面](../Archive/20261005-彼之矛三档LOD进一步减面.md) | commander, art, assets | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-05 | [彼之矛顶点动画制作与首轮源码交付](../Archive/20261005-彼之矛顶点动画制作与首轮源码交付.md) | commander, building, art, assets | partial | 彼之矛固定朝向建造源码和源表已实现，无运行骨骼VAT资源及真实预览已交付，原型图静态审核区已保存；编译与原生数据安装待答复。 |
| 2026-10-04 | [ControlRig机甲B-v4正式UE导入](../Archive/20261004-ControlRig机甲B_v4正式UE导入.md) | art, assets, rendering | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-04 | [ControlRig 机甲 B-v4 减少线稿保留轮廓](../Archive/20261004-ControlRig机甲B_v4减少线稿保留轮廓.md) | art, assets, rendering | partial | 按用户要求减少内部线稿，保留基础轮廓和三档；B-v4已显示在当前Blender，四图/三动作及同机位比较留档，B待审核，原面数预算仍超标。 |
| 2026-10-04 | [ControlRig 机甲 B-v3 实际线稿与三档明暗](../Archive/20261004-ControlRig机甲B_v3实际线稿与三档明暗.md) | art, assets, rendering | partial | 按用户实时线稿反馈修订B-v3，实际材质接入结构线/轮廓与三档明暗，当前Blender双3D预览可旋转；四图、真实三动作和预算差额已交付，B待审核。 |
| 2026-10-04 | [ControlRig 机甲 A 放行与实际 Blender B-v2 候选](../Archive/20261004-ControlRig机甲A放行与BlenderB_v2候选.md) | art, assets, rendering | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-04 | [ControlRig 机甲按用户色板修订参考 A-v2](../Archive/20261004-ControlRig机甲四色参考A_v2.md) | art, assets, rendering | partial | 按用户四色色板修订 ControlRig 机甲参考，交付并冻结原生2K效果与三视图，保持结构、骨架、姿态及源基线，A-v2待审核。 |
| 2026-10-04 | [ControlRig 机甲实际源采集与参考 A-v1 交付](../Archive/20261004-ControlRig机甲参考A_v1交付.md) | art, assets, rendering | partial | 恢复源资产只读查询，确认 ControlRig 四足单炮机甲的152骨骼与高面数基线，交付并冻结一致的八张2K参考和灰模，停在用户审核A。 |
| 2026-10-04 | [先驱号会话临时文件重试清理完成](../Archive/20261004-先驱号会话临时文件重试清理完成.md) | commander, art, assets | passed | 用户明确要求重试后，完成既有清单55个临时文件及两个空缓存目录的删除，70个冻结审核文件哈希仍全部一致。 |
| 2026-10-04 | [先驱号初代开发通过与 GitHub 交付](../Archive/20261004-先驱号初代开发通过与GitHub交付.md) | commander, combat, art, assets, rendering, data, network, performance | passed | 用户通过先驱号初代开发并授权清理与 GitHub 上传；记录当前批准基线、临时文件排除和已编译共用依赖的交付范围。 |
| 2026-10-04 | [绿色引导线玩家验收通过](../Archive/20261004-绿色引导线玩家验收通过.md) | commander, ui | passed | 绿色引导线遮挡修复编译加载及PIE地图占用解除后，用户回复通过；据此完成本次玩家验收，开发状态置done/passed。 |
| 2026-10-04 | [原型Map双编辑器文件占用解除](../Archive/20261004-原型Map双编辑器文件占用解除.md) | commander, ui | partial | 玩家PIE被导航保存门禁取消，查明任务重开实例锁住Map；正常退出空闲干净的重复实例后文件锁释放，保留玩家编辑器及新DLL。 |
| 2026-10-04 | [绿色引导线地形遮挡修复编译加载](../Archive/20261004-绿色引导线地形遮挡修复编译加载.md) | commander, ui | partial | 用户回复编译后，源码Editor目标构建退出0，8份BuildId一致，新DLL及覆盖材质配置已加载；跨坡实体回读通过，实际显示待玩家确认。 |
| 2026-10-04 | [绿色引导线地形遮挡修复静态交付](../Archive/20261004-绿色引导线地形遮挡修复静态交付.md) | commander, ui | partial | 玩家截图确认坡地遮挡后改接禁用深度测试的独立材质；静态检查及原型Map跨坡说明、观察实体保存回读通过，新原生代码尚未编译。 |
| 2026-10-04 | [飞行物启停同步与实际时间预算实施验收](../Archive/20261004-飞行物启停同步与实际时间预算实施验收.md) | commander, combat, network, ship, wingman, performance | partial | 实际时间连接预算和历史混合来源飞行启停通道已编译重载，500枚峰值、补建、池回收及17项回归通过；完整性能目标保留待验证。 |
| 2026-10-03 | [当前PIE网络预算与全场单位延迟诊断](../Archive/20261003-当前PIE网络预算与全场单位延迟诊断.md) | commander, network, combat, performance | partial | 当前专服加双客户端PIE采样确认状态姿态发送积压，地面弹丸快照占53.4%下行；120Hz预算截断与35Hz实际运行高度吻合，未实施修复或单变量复测。 |
| 2026-10-03 | [绿色引导线源码Editor编译加载](../Archive/20261003-绿色引导线源码Editor编译加载.md) | commander, ui | partial | 用户授权后源码Editor目标构建退出0，8份BuildId一致；重开编辑器确认新GuLiStrike DLL及原型Map的5个验收实体，运行效果待玩家确认。 |
| 2026-10-03 | [绿色引导线模型中心实时连接交付](../Archive/20261003-绿色引导线模型中心实时连接交付.md) | commander, ui | partial | 活动绿线新增模型中心逐帧跟随与缺失表现恢复，静态检查及原型Map的5个实体保存回读通过；编译和玩家效果验收尚未执行。 |
| 2026-10-03 | [先驱号步态五倍减速与一米改令](../Archive/20261003-先驱号步态五倍减速与一米改令.md) | commander, art, rendering | partial | 按用户反馈将先驱号四向六足 VAT 降至原速 20%，移动目标复用半径从实际 25 米改为 1 米；正式资产与配置已保存，当前会话未停止，运行采样约 0.2 倍。 |
| 2026-10-03 | [先驱号委托 PIE 验收与运行修复](../Archive/20261003-先驱号委托PIE验收与运行修复.md) | commander, combat, art, assets, rendering, data, network | passed | 用户明确委托助手代验；本地权威 PIE 核心 13 项通过，地图启动、基础速度、初生待机和墙内生成四个问题修复后重新编译加载，场景保存与烘焙有效。 |
| 2026-10-03 | [先驱号 Editor 编译与正式接线场景交付](../Archive/20261003-先驱号Editor编译与正式接线场景交付.md) | commander, combat, art, assets, data, network | partial | 用户另行授权编译重载，Editor 构建与新类型加载成功；正式 VAT、六张表及技能头像引用完成磁盘回读，指定地图最终保存核对通过，Mass 运行没有骨骼依赖。 |
| 2026-10-03 | [先驱号验收区域准备与资源目录回读](../Archive/20261003-先驱号验收区域准备与资源目录回读.md) | commander, art, assets, combat | partial | 原型图七个部署点、四个阻挡物与说明已保存并实体回读；明确新玩法尚未加载，补齐实际 Skeleton 与 Physics 资源目录映射。 |
| 2026-10-03 | [先驱号 B-v1 批准与 UE 美术 VAT 副本导入](../Archive/20261003-先驱号B-v1批准与UE美术VAT副本导入.md) | art, assets, commander, rendering, data | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-03 | [RSG 六足机器人 Blender B-v1 成品交付](../Archive/20261003-RSG六足机器人BlenderB-v1成品交付.md) | art, assets, rendering | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-03 | [RSG 六足机器人 A-v3 背环与头球配色](../Archive/20261003-RSG六足机器人A-v3背环与头球配色.md) | art, assets, rendering | partial | 按用户指定将背环改天蓝、头球改浅黄，保留八个天蓝小分件，同步 A-v3 四张 2048px 参考；源结构与旧版本保持，待审核 A-v3。 |
| 2026-10-03 | [RSG 六足机器人 A-v2 天蓝点缀](../Archive/20261003-RSG六足机器人A-v2天蓝点缀.md) | art, assets, rendering | partial | 按用户“加一点天蓝色”加入八个成对分件的少量天蓝点缀，同步四张 2048px A-v2 参考；源结构/相机及 A-v1 保持，等待审核 A-v2。 |
| 2026-10-03 | [RSG 六足机器人审核 A-v1 参考交付](../Archive/20261003-RSG六足机器人审核A-v1参考交付.md) | art, assets, rendering | partial | 交付源模型约束的 A-v1 效果图与三视图（均 2048px）、灰模对照、配色/活动件说明和审图页；用户审核 A 待决定，未开始重制或正式 UE 导入。 |
| 2026-10-02 | [海岛据点导航会话验收与清理上传](../Archive/20261002-海岛据点导航会话验收与清理上传.md) | commander, navigation, resources, art, assets, network | passed | 用户确认本会话全部文档通过，关闭6个工作项并归档验证数据；清理临时输出和可重建导入缓冲，保留海岛工程与游戏资源。 |
| 2026-10-02 | [连通认证暂关与R2C2落地区修复](../Archive/20261002-连通认证暂关与R2C2落地区修复.md) | commander, navigation, network | passed | 按用户要求暂关全图连通认证，R2C2 移至同格合格平地、资源及导航重建保存；PIE 核对停止任务与37人旧路线保持通过。 |
| 2026-10-02 | [不可达移动拒绝编译加载与PIE门禁记录](../Archive/20261002-不可达移动拒绝编译加载与PIE门禁记录.md) | commander, navigation, network | passed | 用户授权的 Editor 构建、BuildId 核对和 UE 重启加载通过；普通 PIE 被原地图资源门禁拒绝，运行效果未执行。 |
| 2026-10-02 | [不可达新移动指令拒绝源码与场景交付](../Archive/20261002-不可达新移动指令拒绝源码与场景交付.md) | commander, navigation, network | passed | 增加新令完整可达预检，拒绝时保留原任务并撤掉本次目标线；静态检查和地图场景保存通过，未编译或运行游戏。 |
| 2026-10-02 | [坡面导航编译重建与残留断区](../Archive/20261002-坡面导航编译重建与残留断区.md) | commander, navigation | passed | 用户授权后完成源码版Editor构建并重建新坡面预设；参数回读通过，全图20个区域使严格认证失败，19个孤立区域与旧标记高度失效已留档。 |
| 2026-10-02 | [坡面导航预设接入与场景准备](../Archive/20261002-坡面导航预设接入与场景准备.md) | commander, navigation | passed | 已接入20厘米台阶与坡度过滤预设，更新Prepare/Validate及旧缩放入口，完成静态检查并保存原三处验证标记和Commander导航显示；本次编译与导航重建待授权。 |
| 2026-10-02 | [坡面细碎导航断带生成参数分析](../Archive/20261002-坡面细碎导航断带生成参数分析.md) | commander, navigation | passed | 只读核对发现导航网格分辨率、7厘米台阶限制和44度坡度许可不匹配；UE5.7已有坡度边缘过滤选项，建议配合网格精度或台阶高度调整。尚未重建或确认截图中每条断带的具体成因。 |
| 2026-10-02 | [导航连通性校验编译与模块加载](../Archive/20261002-导航连通性校验编译与模块加载.md) | commander, navigation | passed | 用户授权后完成源码版 GuLiStrikeEditor 编译与 BuildId 核对，重新打开指挥官地图并确认新接口、严格地图配置及三个验证点；尚未执行导航验收。 |
| 2026-10-02 | [导航连通性烘焙校验实现与场景交付](../Archive/20261002-导航连通性烘焙校验实现与场景交付.md) | commander, navigation | passed | 已实现 CommanderSoldier 两点有向可达查询与全图强连通烘焙校验，静态检查通过，真实指挥官地图保存三个验证点；本阶段未编译或运行新代码。 |
| 2026-10-02 | [山脚高台导航连通性确认](../Archive/20261002-山脚高台导航连通性确认.md) | commander, navigation | passed | 当前四个选中单位到高台及反向查询均为部分路径且原生搜索未耗尽，两端内部各自连通；确认当前CommanderSoldier缺少山脚到该高台的可走连接。 |
| 2026-10-01 | [Mass编译后高处部分路径诊断](../Archive/20261001-Mass编译后高处部分路径诊断.md) | commander, navigation | passed | 用户编译后自行开启PIE，新高处目标的10次CommanderSoldier查询均仅有部分路径；135停滞约168秒仍Normal，确认部分末端处理有遗漏。 |
| 2026-10-01 | [Mass陡坡绕行原生编译](../Archive/20261001-Mass陡坡绕行原生编译.md) | commander, navigation | passed | 用户授权后完成GuLiStrikeEditor Win64 Development构建，退出码0，项目与源码版引擎BuildId一致；编辑器加载及玩家效果未验证。 |
| 2026-10-01 | [Mass陡坡绕行源码与静态核对](../Archive/20261001-Mass陡坡绕行源码与静态核对.md) | commander, navigation | passed | 完成共享移动末段导航验证与受预算约束的停滞重寻路源码；静态核对通过，当前用户PIE和旧模块保持运行。 |
| 2026-10-01 | [指挥官1800米海岛与49据点交付](../Archive/20261001-指挥官1800米海岛与49据点交付.md) | commander, resources, art, assets | passed | 1800米扩岸海岛与49据点已保存重开；独立Gaea源、原生Editor编译、200蓝簇40红簇6240节点、500出生槽及104编辑器路径核对通过，浮动与联机效果待玩家验收。 |
| 2026-10-01 | [战斗海岛原生Landscape导入UE](../Archive/20261001-战斗海岛原生Landscape导入UE.md) | art, assets | passed | Gaea海岛已导入独立2300米UE关卡，保存重开及16位高度、六类权重逐像素核对通过，并留存实际UE预览。 |
| 2026-10-01 | [重防号战斗射程优化验收通过](../Archive/20261001-重防号战斗射程优化验收通过.md) | commander, combat | passed | 用户在重防号机枪120米、Q导弹240米交付后明确回复通过，工作项玩家整体验收完成。 |
| 2026-10-01 | [重防号战斗射程优化交付](../Archive/20261001-重防号战斗射程优化交付.md) | commander, combat | partial | 按用户要求将WM01机枪30→120米、Q导弹96→240米；源表、三张UE数据表、主动目录和11个场景实体已保存回读。 |
| 2026-10-01 | [机枪10度随机弹道审核通过](../Archive/20261001-机枪10度随机弹道审核通过.md) | commander, combat | passed | 用户在两兵种机枪10度随机弹道交付后明确回复审核通过，工作项整体玩家验收完成。 |
| 2026-10-01 | [指挥官关卡环境对齐地面机甲演示关卡](../Archive/20261001-指挥官关卡环境对齐地面机甲演示关卡.md) | art, rendering, assets | partial | 指挥官关卡的灯光、天空球云材质、雾与后期已按源图对齐，保存重载无参数差异，地形及玩法对象保持。 |
| 2026-10-01 | [地面机甲地表材质迁移至指挥官关卡](../Archive/20261001-地面机甲地表材质迁移至指挥官关卡.md) | art, rendering, assets | partial | 源地表材质通过关闭草簇的独立实例应用到指挥官地形，全范围自动层填充、保存重载及编辑器画面核对通过。 |
