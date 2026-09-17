# GuLiStrike PIE 启动耗时分析

分析日期：2026-09-15。平台：Windows，UE 5.7，Development Editor。
地图：`/Game/Maps/LVL_CommanderMassPrototype`。

## 结论

**主要等待发生在资源系统的全局导航就绪门槛：最近一次等待 122.120 秒，占主机玩法启动耗时的 91.92%。** 点击播放到主机 Commander bootstrap ready 为 132.860 秒，到第二个客户端 ready 为 134.780 秒。

PIE 窗口创建、地图载入和玩法可操作是不同节点。UE 显示的“PIE 总开始时间 10.62 秒”尚未包含后续导航等待。

另一个编辑器实例的记录同样等待 117.939 秒。该次只有 40 个士兵，最近一次为 500 个；导航 ready 到士兵生成完成分别为 0.059 / 0.110 秒。因此，两分钟等待的优先调查对象是导航初始化及其就绪条件。

## 1. 最近一次时间线

以下为北京时间（原始 UE 日志为 UTC，已加 8 小时）。来源是 PID 8752 的日志快照。

| 节点 | 时间 | 距点击播放 |
|---|---|---:|
| 点击播放，出现 Play 命令标记 | 19:36:15.826 | 0.000 秒 |
| 资源世界初始化完成，开始等待动态导航 | 19:36:26.440 | 10.614 秒 |
| UE 报告 PIE 总开始时间 10.62 秒 | 19:36:26.468 | 10.642 秒 |
| 第二客户端地图加载完成 | 19:36:34.407 | 18.581 秒 |
| Resource world and navigation are Ready | 19:38:28.560 | 132.734 秒 |
| 500 个 Mass 士兵生成完成 | 19:38:28.670 | 132.844 秒 |
| 主机 Commander bootstrap ready | 19:38:28.686 | 132.860 秒 |
| 第二客户端 Commander bootstrap ready | 19:38:30.606 | 134.780 秒 |

证据：[播放命令](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/current-editor.log:1438)、[资源初始化](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/current-editor.log:1531)、[导航就绪](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/current-editor.log:1889)、[士兵生成](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/current-editor.log:1893)、[客户端就绪](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/current-editor.log:1898)。

注意：122.120 秒是“导航未就绪条件解除前”的墙钟等待窗口，不是独占 CPU 耗时。其中包含客户端加载和部分资产编译，不能把这些耗时再叠加到总时间上。当前证据未细分每个导航工作线程的 CPU 时间，也未直接计数本次重建瓦片。

### 第二份记录交叉验证

| 指标 | 19:15 左右的启动（PID 16424） | 19:36 的启动（PID 8752） |
|---|---:|---:|
| 士兵数 | 40 | 500 |
| 资源初始化后等待导航 | 117.939 秒 | 122.120 秒 |
| 导航 ready 到士兵生成完成 | 0.059 秒 | 0.110 秒 |
| 主机总启动时间 | 128.337 秒 | 132.860 秒 |
| 导航等待占主机总启动 | 91.90% | 91.92% |
| 资产编译内存预算提示 | 0 条 | 256 条 |

较早记录的起点由 UE 报告的 PIE 总开始时间反推；最近一次有直接的播放命令时间戳。两份记录发生在不同时间、不同实例，属于交叉证据，不是严格控制变量的性能对照试验。

## 2. 等待链路及成因

### A. 所有导航数据都要就绪，资源和士兵才放行（已确认）

1. `UGuLiResourceWorldSubsystem::OnWorldBeginPlay` 加载资源数据并创建权威资源世界。
2. `UpdateNavigationReadiness` 每帧查询 `IsNavigationBeingBuiltOrLocked(World)`，返回 false 后才开放经济交易、设置 AuthorityReady。
3. `UGuLiBattleAuthoritySubsystem::TrySpawnAuthorityPopulation` 同时检查资源 Ready 和这个全局导航条件，通过后才生成士兵。
4. UE 的查询涵盖导航锁、待处理脏区以及 `NavDataSet` 中任意导航生成器的未完成工作。两套 NavMesh 都在约束范围内。

代码：[资源就绪门槛](D:/UE5.7/test1/Source/GuLiStrike/Gameplay/Resources/GuLiResourceWorldSubsystem.cpp:439)、[士兵生成门槛](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp:1603)、[引擎查询范围](D:/UnrealEngine-5.7/Engine/Source/Runtime/NavigationSystem/Private/NavigationSystem.cpp:5431)、[遍历导航生成器](D:/UnrealEngine-5.7/Engine/Source/Runtime/NavigationSystem/Private/NavigationSystem.cpp:4885)。

### B. 开局生成大量影响导航的对象，且两套 NavMesh 都为 Dynamic（已确认机制，单项耗时未拆分）

运行时创建 240 个矿簇障碍、25 个据点、2 座初始工厂及车辆。矿簇碰撞球开启 `CanEverAffectNavigation`，同时带有 NavArea_Null Modifier；初始化还会更新球半径和导航边界。据点模型也开启了导航影响。这些对象在开局进入导航系统并使相关区域需要更新。

6240 个矿石节点通过 24 个 HISM 显示，HISM 已禁用导航影响；数量本身不代表 6240 个导航障碍。

代码：[开局生成](D:/UE5.7/test1/Source/GuLiStrike/Gameplay/Resources/GuLiResourceWorldSubsystem.cpp:295)、[矿簇障碍配置](D:/UE5.7/test1/Source/GuLiStrike/Gameplay/Resources/GuLiResourceActors.cpp:283)、[据点导航影响](D:/UE5.7/test1/Source/GuLiStrike/Gameplay/Resources/GuLiResourceActors.cpp:338)、[HISM 导航设置](D:/UE5.7/test1/Source/GuLiStrike/Gameplay/Resources/GuLiResourceActors.cpp:48)。

实时读回的同地图实例参数：

| 参数 | 读回值 |
|---|---|
| 导航边界宽度 | 5.6 × 5.6 公里，面积 31.36 平方公里 |
| NavMesh 数量 | 2：Default、CommanderSoldier |
| 代理半径 | 34 cm、750 cm |
| 生成方式 | 两套均为 Dynamic |
| TileSizeUU | 5000 cm，即每块 50 m |
| 并发生成任务配置 | 每套最大 8，实际还受工作线程数量约束 |
| FullyAsyncNavDataGathering | True |
| ForceRebuildOnLoad | False |

按边界宽度 / 瓦片宽度粗算，每套覆盖约 112 × 112 个平面格位，两套合计约 25,088 个格位。这是覆盖规模估算，地形裁剪、边界扩展、分层以及脏区分布会改变实际瓦片数；不能据此声称本次重建了全部格位。

### C. 旧精度字段与真正生效参数存在落差（已在连接实例确认）

[DefaultEngine.ini](D:/UE5.7/test1/Config/DefaultEngine.ini:12) 写着 `CellSize=50`、`CellHeight=25`。但实时读取的 `NavMeshResolutionParams` 为：

| 分辨率档位 | CellSize | CellHeight |
|---|---:|---:|
| Low | 38 cm | 10 cm |
| Default | 19 cm | 10 cm |
| High | 19 cm | 10 cm |

UE 5.7 中旧字段已经标记 Deprecated；生成器直接使用 `NavMeshResolutionParams`，旧字段的迁移仅在特定旧 NavMesh 版本加载时执行。因此，现有 ini 中的 50/25 不能证明地图 Actor 正在使用 50/25。

按默认档位，50 m 瓦片每边约 263 个单元；按配置意图的 50 cm 单元则为每边 100 个。水平采样数量约有 6.9 倍差别，这只是规模计算，实际速度还受几何、代理膨胀边界、线程和脏区数量影响。

代码：[旧字段弃用与新参数](D:/UnrealEngine-5.7/Engine/Source/Runtime/NavigationSystem/Public/NavMesh/RecastNavMesh.h:697)、[版本迁移条件](D:/UnrealEngine-5.7/Engine/Source/Runtime/NavigationSystem/Private/NavMesh/RecastNavMesh.cpp:642)、[生成器使用实际分辨率](D:/UnrealEngine-5.7/Engine/Source/Runtime/NavigationSystem/Private/NavMesh/RecastNavMeshGenerator.cpp:5177)。

**实例边界：** Python 接口实际连接 PID 16424，19:36 启动日志来自 PID 8752。已连接实例的导航参数不能冒充另一实例的直接读回值。两份启动日志独立证明导航就绪等待；分辨率配置问题在 PID 16424 得到实测，修改前应在目标实例再读回一次。

## 3. 次要成本与排除项

- **前段约 10.6 秒：** 世界复制 2.210 秒，World Init 0.942 秒，另有资源加载、注册、创建音频和网络世界等。现有日志不足以将全部剩余时间逐函数归因。
- **客户端资产加载：** 19:36:27.686 出现 `FlushAsyncLoading` 等待 76 个异步包，19:36:30.570 才开始 LoadMap；LoadMap 本身 3.836 秒。这部分与导航等待重叠。
- **资产编译压力：** 最近一次启动窗口出现 252 条 TextureDerivedData、4 条 StaticMesh 内存预算不足提示，并明确等待矿石网格与工厂门蒙皮资产。较早记录无同类提示仍等待约 118 秒，因此它解释不了主要等待。
- **双编辑器实例：** 19:51 的采样仍有 PID 8752 与 PID 16424。两者私有提交内存合计约 37.7 GiB，物理驻留合计约 10.1 GiB；系统剩余提交额度约 3.8 GiB。额外实例会增加资源竞争，但这不是启动时分页或其耗时的直接测量。
- **蓝图编译：** 两次均明确记录 `No blueprints needed recompiling`。
- **缓存服务：** 最近日志中 ZenLocal 状态 OK；本地缓存速度测试读约 591.63 MB/s，延迟 0.01 ms。没有证据指向 DDC 服务不可达或清空缓存能解决此次等待。
- **编辑器启动约 29.81 秒** 属于打开 UE 的初始化，发生在此次点击 PIE 前，不计入上述启动时间。

## 4. 建议处理顺序

1. **先校正真正生效的导航分辨率。** 核对目标地图的两套 `NavMeshResolutionParams` 与 CDO/ini 的关系，使用 UE 5.7 支持的新字段配置，并同步已有 Actor。先以原配置意图 50/25 cm 做候选方案，检查坡面、台阶、窄路和士兵半径，测量耗时后决定。
2. **减少开局脏区和重复导航生成。** 测量两套生成器的待处理/运行瓦片数；检查矿簇、据点、工厂的碰撞导出与 Modifier 是否产生多余更新；集中完成开局对象注册后再让导航处理脏区。保留资源和移动所需的正确就绪约束。
3. **评估预生成地面导航 + 动态障碍修改。** 只有在矿簇消失、建筑建造/摧毁、车辆移动和两种代理均能正确通行时，才采用 Dynamic Modifiers Only 或等效方案。当前有实体碰撞参与导航，切换模式需要配套处理。
4. **再处理资产预加载与多实例竞争。** 优先按稳定资产路径预加载矿石阶段网格/工厂资源；单编辑器重测以量化内存竞争。减少客户端数量只能作为区分客户端附加成本的实验，不能据此宣称导航问题解决。

Epic 的[导航生成优化说明](https://dev.epicgames.com/documentation/unreal-engine/optimizing-navigation-mesh-generation-speed-in-unreal-engine)建议使用能满足通行精度的较大单元、控制瓦片单元数量、简化导航碰撞并减少重复脏区。此次优先级根据本项目日志与源码确定。

## 5. 复现与验收

### 本次已完成

- 读取两个确定路径的文本日志，保存快照、时间线及 SHA-256。
- 通过 `Scripts/commander_editor_python.py` 只读访问运行编辑器，记录实际 PID、地图和导航参数。
- 对照项目启动源码和本机 UE 5.7 导航实现，验证 Ready 依赖链。
- 未重新触发 PIE，未进行功能或配置优化；本次仅新增本目录诊断材料。

### 后续同场景对照

1. 记录目标 PID、同一地图、Listen Server + 1 Client、同进程设置和 500 个士兵配置。
2. 记录按下播放 → 资源创建 → 导航队列清空 → 主机 ready → 客户端 ready，各阶段统一使用墙钟时间。
3. 收集首次运行一次、资源已加载后的重复运行三次，分开报告；每次仅改变一个候选因素。
4. 同步采集两套 NavMesh 的待处理/已完成瓦片和 CPU 事件，以拆开全量初始化、资源脏区更新、后台调度等待。
5. 回归：全部出生点可投影、500 个士兵可寻路、矿簇耗尽后可通行、工厂/据点/新建建筑能阻挡、采矿车与工程车可移动、客户端同步就绪。
6. 原始基线为主机 132.860 秒、导航门槛 122.120 秒。优化验收必须有相同场景的前后数据；尚未测出可承诺的优化幅度或最终秒数。

## 材料

- [结构化耗时](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/timings.json)
- [编辑器导航快照](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/connected-editor.json)
- [进程与内存快照](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/processes.json)
- [日志分析脚本](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/analyze_logs.py)
- [只读编辑器采样脚本](D:/UE5.7/test1/TestResults/PIEStartupAnalysis-20260915/inspect_connected_editor.py)

置信度：**导航就绪门槛是主要启动等待，高。** 具体导航生成子阶段、当前目标实例精度与每项优化收益仍需按上述对照方案量化。
