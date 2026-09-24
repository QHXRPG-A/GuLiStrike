---
schema: guli-progress/v1
id: DEV-20260921-005
work_id: WORK-20260921-005
kind: development
role: root
title: 指挥官部队StateTree接入与特殊任务退役 — 技术方案
areas: [commander, resources, building]
categories: [gameplay]
status: verification
verification: partial
created: '2026-09-21'
updated: '2026-09-22'
summary: 原10Hz任务入口统一驱动Actor和Mass StateTree；工程车已接通矿位/建造位、预算路径和四点卸货，32辆配置完成返厂专项，完整功能与长期性能仍待验。
next_action: 在LVL_CommanderMassPrototype补齐矿位、施工、改令及通道异常矩阵，再完成新逻辑10/30台负载与30分钟性能验收，并收集玩家效果反馈。
relations:
  requirement: REQ-20260921-005
status_note: 2026-09-22经用户明确授权完成源码Editor构建并加载，三棵现有树及原型地图保存回读通过；32辆配置的约156秒返厂专项中16矿车共34次上传，6厂24点完整路径。未覆盖施工、完整异常矩阵、网络及30分钟性能，待玩家验证效果。下文9月21日未运行PIE、旧状态数和旧车辆数均属历史阶段。
---

# 指挥官部队StateTree接入与特殊任务退役

## 2026-09-22当前交付：工程车新逻辑与四点卸货

继续在现有StateTree机制上拆分选择目标、等位、预占、等待路径、移动、作业及失败恢复；矿位归资源系统、建造位归建筑系统，路径服务只提供计算结果。矿车/建造车共用预算查询和工作路线，保留任务版本与序列化枚举编号，不新增平行业务状态机。

本次返厂简化取消入口距离门槛、厂内进出路线和停靠预约；矿车直接走向厂内四个无占用卸货点，失败换点、再换厂，上传后完成一轮。工程车相互忽略物理碰撞及Crowd避让，完工厂代理碰撞正确关闭，包括赠品清场恢复场景。具体实现与构建证据见[本次归档](../Archive/20260922-工程车四点返厂简化与32辆专项验证.md)，当前行为与参数见[资源经济](../Gameplay/资源经济.md)。

源码版`D:/UnrealEngine-5.7`的`GuLiStrikeEditor Win64 Development`最终构建退出0，引擎与项目BuildId一致，新启动编辑器已加载项目模块。三棵原资产编译保存后分别为Mass18/Miner57/Builder18个状态，Soldiers 1/2/3/4引用保持；不再生成EnterFactory/ExitFactory，保留运输安全退出分支。[树结果](../../outputs/engineering-navigation/factory-unloading-20260922/archive-tree-assets.json)。

`/Game/Maps/LVL_CommanderMassPrototype`正常Commander入口已保存并核对实体，仍用`GuLiStrike/Review/CommanderStateTree`四个说明点，未改变原45个无关Actor。原型配置为500个Mass及每方8矿车＋8建造车，预计532个树实例；运行明确核对工程车32辆。地图保存与布局/绑定证据见[场景结果](../../outputs/engineering-navigation/factory-unloading-20260922/archive-scene-delivery.json)。

经用户“现在编译、加载并验证”授权，最终PIE专项持续约156秒：两队同时返厂峰值各8辆，16矿车各2–3次共34次上传，6厂24点均有完整非局部路径。该场景没有待建建筑，未验证施工产出；查询计数不替代主线程分位或30分钟验收。[最终专项结果](../../outputs/engineering-navigation/factory-unloading-20260922/verification-result.json)。

- [x] 完成四点返厂代码静态核对、授权构建及新模块加载。
- [x] 更新现有StateTree与原型地图说明并保存，回读绑定及相关实体数据。
- [x] 完成四点可达性、双方各8矿车返厂卸货及互相通过的专项验证。
- [ ] 补齐矿位/建造位、四车施工、手动/取消/销毁、据点围合与通道失效、不可达等功能矩阵及玩家反馈。
- [ ] 在新逻辑上完成10/30台负载和至少30分钟持续运行，核对主线程、排队响应、缓存/内存趋势及有效产出。
- [ ] 按[独立内存需求](../RequirementDocument/20260922-导航内存优化与对局容量预算.md)推进导航容量优化与专服测量，不将编辑器加PIE内存当作单局服务端占用。

以下章节保留9月21日迁移、报错修复与交付的过程事实。当时64个矿车状态、14个建造状态、每队单车及“未启动PIE”不代表9月22日当前基线。

## 结构与边界

Actor使用独立StateTree Component。Mass使用Mass Schema、Task基类、ExecutionContext与原生实例存储，由项目Processor在原有任务节拍执行。独立实例fragment避免默认Mass信号处理器再次驱动同一实例；该fragment同时进入Even/Odd调参基础组成，迁移沿用句柄和树引用。

任务Subsystem保留队列、权限、版本、异步寻路批次和摘要。树负责选择与切换，操作经过版本校验后在Mass遍历外提交。完成或取消后的立即选择使用同一10Hz周期内的零时间转换，不新增仿真或网络节拍。

矿车、建造组件和据点推进Subsystem已拆出动作执行接口与结果；内部流程选择迁至树。寻路、移动、避让、施工结算、采矿结算、分组规模和查询预算保持。

## 实施清单

- [x] 添加Actor/Mass Schema、节点、运行入口和版本化任务桥接。
- [x] 迁移UI能力来源及命令语义校验，保留网络结构与编号。
- [x] Soldiers新增StateTreeAsset，删除特殊任务源表、专用脚本和文本导出产物。
- [x] 完成三种内部业务流程迁移及旧调度清理。
- [x] 完成接口、依赖、网络参数和底层差异静态检查。
- [x] 经用户授权编译原生模块，创建并保存三棵树、重新导入Soldiers、删除旧DataTable。
- [x] 最后准备并保存对应Map场景，读取相关实体数据，交付玩家验证。

## 验证和资产交付

Excel标准导出保持18张表，没有恢复旧特殊任务表。Soldiers原13列逐值不变；新列绑定四个兵种。GameTexts中5条仍使用的错误提示保留ID和显示文本，仅将来源说明从旧Executor改为能力桥接文件，并重新导入对应DataTable。Python源文件语法检查、git diff --check及本地UE5.7接口核对通过。

源码版`GuLiStrikeEditor Win64 Development`构建成功，退出0。引擎、项目及本次构建的GuLiFlightNavigation插件BuildId一致：`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。[构建记录](../../Artifacts/CommanderStateTree/20260921/build.json)。构建过程中修正了Mass fragment的可平凡复制约束、Move名称歧义和未导出的StateTree编辑器方法调用。

三棵树均已编译并保存：Mass 18个状态、Miner 64个状态、Builder 14个状态；Schema分别为Commander Mass和Commander Actor。四行Soldiers引用回读一致；旧特殊任务DataTable无资产引用并已删除。[树资产](../../Artifacts/CommanderStateTree/tree-assets.json)、[Soldiers导入](../../Artifacts/CommanderStateTree/soldiers-import.json)、[旧表删除](../../Artifacts/CommanderStateTree/retired-table.json)。

Map：`/Game/Maps/LVL_CommanderMassPrototype`，已保存并读取相关实体和资产引用。未执行PIE、自动化或性能验收，资产编译成功不代表运行行为验收通过。

编译执行前按项目技能确认`GuLiStrikeEditor Win64 Development`及编辑器使用情况。现有测试只维护因移除旧类型造成的引用，不增加或自动运行测试。

[需求](../RequirementDocument/20260921-指挥官部队StateTree接入与特殊任务退役.md)

## 源码静态核对

- 3个共享树资产，每个存活单位独立实例；例如500名Mass士兵和4辆工程车对应504个实例。仅服务端运行，客户端加载配置供UI使用。
- Actor组件禁用自动Tick，由原10Hz任务入口手动驱动。Mass使用独立fragment和原生实例存储，死亡/移除释放句柄，调速迁移复制共同fragment；有安全退出要求的Actor注销先完成退出再释放树。
- 树观察业务阶段与结果，输出带任务版本的请求；查询结束后统一提交，重复进入同轮节点不会重复下令。旧Advance循环、矿车TickAutomatic、Pawn待执行命令分支已删除。
- 旧Actor命令入口继续做权限、序号和回执处理，动作改为进入统一任务队列。矿车兼容请求序号与树内部执行编号分开保存，避免内部推进消耗外部序号。
- 采矿Manager的节点分配、采集累计与资源结算、厂内MoveFactoryStep、建造PrepareBuilding与AddConstructionWork、Mass移动/避让/寻路实现保留。推进分组上限25、路径请求上限每世界帧4次、组观察间隔0.5秒保留。
- 网络合同和NetSync主文件逐字比对未变，任务状态/命令枚举及SpecialTaskId继续兼容；10Hz任务、5Hz摘要、重试与同步参数未改。没有树内部状态复制。
- [构建前静态数据核对](../../Artifacts/CommanderStateTree/20260921/static-review.json)与本轮原生构建均通过；运行时行为待玩家验证。

## 构建授权

用户明确回复“允许编译并完成资产和场景交付”。目标为源码版`D:/UnrealEngine-5.7`下`GuLiStrikeEditor Win64 Development`，构建开始时无运行中的编辑器；不包含PIE、自动化或运行效果验收。构建完成后打开现有原型地图完成交付。

## 场景交付与玩家入口

使用地图正常的指挥官入口。原有默认500名Mass士兵覆盖扫荡者与战争机器；经济配置的矿车兵种3、建造车兵种4、每队一辆建造车和原工厂生成逻辑提供4个Actor单位。按当前配置开局预计504个独立实例，尚未通过运行计数验证。

Outliner文件夹`GuLiStrike/Review/CommanderStateTree`包含4个仅编辑器说明点：

| 实体标签 | 位置与用途 |
| --- | --- |
| StateTreeReview_Entry | 红方集合区(0, 196000)，场景入口与验证边界 |
| StateTreeReview_MiningFactory | 红方工厂外(0, 250600)，观察自动采矿、回厂和安全退出 |
| StateTreeReview_Construction | 红方集合区(1800, 196000)，从正常B建造入口放置合法且可负担的工地 |
| StateTreeReview_StrongholdAdvance | Outpost_R2C3附近(0, 112000)，观察选目标、推进和占领；目标仍由拓扑选择 |

回读确认原有42个Actor变换不变，Commander GameMode、PlayerStart、两类RecastNavMesh与资源地图引用有效。资源布局仍为25个领地、240个矿簇、6240个矿节点，布局哈希保持`5710cc3994205e33848d7d4051a80c2d6cd3b458`。保存后4个说明点存在且均为EditorOnly，地图无脏包。[场景交付](../../Artifacts/CommanderStateTree/20260921/scene-delivery.json)、[独立回读](../../Artifacts/CommanderStateTree/20260921/scene-readback.json)。

该原型地图由项目既有`.gitignore`第66行排除，因此本轮保存的是本地实际地图文件；没有更改版本管理规则。[场景作者脚本](../../Scripts/author_commander_state_tree_scene.py)可在同一原型地图复现这4个说明点，脚本不会启动玩法。

玩家按需求清单确认自动业务、Shift追加、替换、S停止、安全退出、连续换路，以及增援、死亡、注销和调速迁移。当前只交付可验证场景与静态证据，不将这些运行结果标记为通过。

## 玩家报错修复：Mass Subsystem类型登记

玩家随后提供运行日志：`MassRequirements.cpp:75`触发`Failed to find type information for GuLiUnitTaskSubsystem`。首个异常链路为`AllocateInstanceData → CreateProcessorForStateTree → IsGameThreadOnlySubsystem`，发生于Mass树实例创建时；日志中的FlushAsyncLoading为Display信息。[原始日志](../../Artifacts/CommanderStateTree/20260921/subsystem-traits-fix/reported-ensure.log)。

UE5.7的模板`AddSubsystemRequirement<T>`只声明查询访问和线程要求，不会把普通项目`UWorldSubsystem`登记到每个World的Mass类型管理器。编译后的StateTree Schema按UClass动态解析依赖时会查询该类型表，因此静态构建和树资产编译均无法提前捕获此次缺失。

在`GuLiCommanderMassStateTreeProcessor::ConfigureQueries`中，通过原生`RegisterSubsystemType`补齐`GuLiUnitTaskSubsystem`、`GuLiArmyAdvanceSubsystem`、`GuLiUnitDataSubsystem`，覆盖条件与任务节点声明的全部项目Subsystem。登记发生在查询执行及首次实例分配前，并跳过已存在记录，避免UE5.7禁止运行时覆盖已登记类型的另一处ensure。三者沿用默认`GameThreadOnly=true / ThreadSafeWrite=false`。未改引擎源码、Subsystem业务实现、实体组成、树运行句柄、树资产、网络接口或调度参数。[诊断及静态核对](../../Artifacts/CommanderStateTree/20260921/subsystem-traits-fix/diagnosis.json)。

沿用用户此前批准的同一Commander StateTree构建范围。关闭前确认编辑器无游戏World、无地图或内容脏包；`D:/UnrealEngine-5.7`下`GuLiStrikeEditor Win64 Development`增量构建成功、退出0，仅重新编译该Processor并链接项目模块。引擎与项目BuildId均为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。[构建结果](../../Artifacts/CommanderStateTree/20260921/subsystem-traits-fix/build.json)。

复用已交付的`/Game/Maps/LVL_CommanderMassPrototype`和4个StateTreeReview说明点，不新增场景对象。玩家重新进入正常指挥官战局，预期首次Mass树实例创建不再出现上述TypeInfo ensure，随后两种Mass士兵开始原出生推进。助手未启动PIE或自动化；本次编译成功尚不等于运行报错已实测消除。

加载边界：本次增量构建后，编辑器两次正常D3D12启动均停在第4帧，日志记录GPU队列超时，停在Slate缓冲上传附近。这与用户提供的Mass TypeInfo ensure调用链不同；本轮因此无法完成重开后的场景数据回读。仅结束了本任务新启动且尚未进入玩法的无响应进程，没有改渲染配置或启动其他RHI。原地图文件仍是18:12保存的版本，未改动。新项目DLL已构建，但运行修复结论等待编辑器恢复后复查。[首次GPU超时](../../Artifacts/CommanderStateTree/20260921/subsystem-traits-fix/editor-startup-gpu-timeout.log)、[第二次启动日志](../../Artifacts/CommanderStateTree/20260921/subsystem-traits-fix/editor-restart.log)、[加载结果](../../Artifacts/CommanderStateTree/20260921/subsystem-traits-fix/editor-load-result.json)。

## 玩家报错修复：条件参数显示无效类型

玩家在Mass树的`RejectUnreachableStronghold`状态看到`Required`、`Forbidden`、`Phase`、`Result`均标为“上下文 / 无效类型”。原因是Actor和Mass条件结构体把这些整数、枚举属性声明为`Category="Context"`。UE5.7 StateTree将该分类解释为上下文绑定，只接受对象引用或结构体；这些属性应为普通条件参数。这与前述Subsystem类型登记错误是两个问题。[玩家截图](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/reported-invalid-types.png)、[诊断](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/diagnosis.json)。

两个条件结构体的8个属性统一改为`Category="Condition"`，保留名称、类型、默认值和判断逻辑。增加编辑器维护命令`gs.Commander.RefreshStateTrees`，从原生反射读取实际属性用途，编译并保存现有三棵树，不重新生成图；保存前检查编辑数据哈希未改变。该命令不参与运行时业务。

沿用同一构建授权，确认无游戏World及地图、内容脏包后正常关闭编辑器。源码版`D:/UnrealEngine-5.7`下`GuLiStrikeEditor Win64 Development`构建成功，退出0，项目与引擎BuildId一致：`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。本次编辑器正常启动并加载新模块，未修改渲染配置或RHI参数；这一事实不代表已查明前次GPU超时原因。[构建记录](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/build.json)、[启动记录](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/editor-launch.json)。

编辑器内回读确认8个属性均为普通Parameter，Mass、Miner、Builder三棵树均编译成功、可运行、已保存，编译前后编辑图哈希相同。原图中的条件值和转换保留。`/Game/Maps/LVL_CommanderMassPrototype`中4个说明点及Soldiers四行引用一致，无地图或内容脏包；已为玩家打开`ST_CommanderMass`资产编辑器。[资产刷新结果](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/asset-refresh.json)、[场景与引用回读](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/editor-readback.json)。

本次未修改业务算法、网络接口、调度与同步频率；保留此前Subsystem登记修复。未运行PIE或自动化，四兵种运行效果及首次实例分配的ensure消除仍交由玩家在既有场景确认。
