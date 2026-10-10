# 服务器 StateTree 性能瓶颈诊断

主要热点集中在条件检查。源码中最值得优先处理的是据点推进所属组的线性查找，以及同一单位、同一轮条件重复读取行为事实／阶段／结果。稳定状态的集中全量检查进一步放大了这些成本并形成周期峰值。

## 实測与范围

复用 [Client1 实战诊断](../20261010-client1-live-frame-analysis/report.md) 的 30 秒捕获。源码版 UE5.7，一进程专服＋双客户端＋编辑器；采样地图 `/Game/Maps/LVL_CommanderMassPrototype`。保留当时实际战局、镜头、视口与质量；没有额外启动 600 单位／500 弹丸压力入口。CSV 905 帧、QPC 时间窗内 902 完整引擎帧。本轮只读探测时 PIE 已结束，未重启。本轮新读回的是编辑器资产，不是新的一轮运行采样。

| Mass StateTree Processor 指标 | 值 |
|---|---:|
| 每引擎帧平均（含未执行帧） | 1.558 ms |
| 实际执行帧平均／P95 | 5.447／7.034 ms |
| 单次调用平均／P95／最大 | 4.533／6.920／7.866 ms |
| 调用数／实际执行帧数 | 310／258 |
| 首次调用平均（每个执行帧） | 5.437 ms |
| 同帧额外调用总量／总成本占比 | 52 次／0.181% |

258 个执行帧中，214 帧调用一次、36 帧两次、8 帧三次，没有四次帧。52 次额外调用合计 2.539 ms、平均 0.049 ms。调用没有记录 DeltaTime 或调用者，额外调用可能包含即时轮次或移动接纳唤醒，不能全部断言为某一种路径。计时代码的名义周期为 0.1 秒；累加后清零会产生超出 0.1 秒的实际周期，本窗执行帧约 8.6 次／秒。

服务器 World Tick 平均 12.607 ms，Mass StateTree 摊销占约 12.36%；整个进程 GT 平均 33.176 ms，其中摊销占约 4.70%。因此它是明确的服务器热点，但这部分优化不能单独解释全部帧率问题，也不能将其成本全算到 Client1。

## 内部阶段：条件检查占最大份额

以下来自原始 CSV `Exclusive/GameThread/StateTree_*`。字段为独占时间，包含本进程全部 GT StateTree 实例；缺少 World／单位标签，不能把每个字段精确拆成 Mass、采矿、建造各自的成本。列中的 P95 为全部 905 引擎帧的 P95，不是某一次条件调用的 P95。不可把各项 P95 相加。

| 已记录内部阶段 | 全帧均值 ms | 全帧 P95 ms | 最大帧 ms | 内部独占总量占比 |
|---|---:|---:|---:|---:|
| `StateTree_TestConditions` | 0.7301 | 2.8665 | 4.1756 | 61.64% |
| `StateTree_TriggerTransition` | 0.1666 | 0.6245 | 1.1212 | 14.07% |
| `StateTree_Tick` | 0.1035 | 0.3872 | 0.6739 | 8.74% |
| `StateTree_TickTasks` | 0.0928 | 0.3350 | 0.7120 | 7.83% |
| `StateTree_Task_Tick` | 0.0504 | 0.1897 | 0.5278 | 4.26% |
| `StateTree_SelectState` | 0.0187 | 0.0802 | 0.3525 | 1.58% |
| `StateTree_EnterState` | 0.0099 | 0.0361 | 0.2662 | 0.84% |
| `StateTree_Task_EnterState` | 0.0049 | 0.0185 | 0.0907 | 0.42% |
| `StateTree_ExitState` | 0.0045 | 0.0152 | 0.1921 | 0.38% |
| `StateTree_Task_ExitState` | 0.0016 | 0.0058 | 0.0274 | 0.13% |
| `StateTree_StateCompleted` | 0.0009 | 0.0048 | 0.0421 | 0.07% |
| `StateTree_Start` | 0.0003 | 0.0000 | 0.1174 | 0.02% |
| `StateTree_Stop` | 0.0002 | 0.0024 | 0.0067 | 0.02% |
| `StateTree_StartEvaluators` | 0.0000 | 0.0000 | 0.0013 | 0.00% |

内部字段合计平均 1.184 ms／引擎帧；其中条件检查 0.730 ms，占 61.64%，最高一帧 4.176 ms。引擎的 `TestAllConditionsInternal` Scope 同时覆盖表达式／绑定处理和项目 `TestCondition` 回调，因此不能把这 62% 全部归到某个项目函数。

CSV 内部字段与 Mass Processor 是不同覆盖范围，不能用二者相减推算“上下文构造”成本。当前 CPU Trace 里 Processor 内没有项目细分子 Scope，Processor 的 exclusive 值仍包含这些未细分工作。

## 主要原因与优化顺序

### 1. 据点推进所属组查询缺少直接索引

[`FindGroup`](../../../Source/GuLiStrike/Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.cpp#L50) 用 `Groups.FindByPredicate` 遍历组，并用 `Group.Soldiers.Contains` 遍历成员。`GetBehaviorPhase` 与 `GetBehaviorResult` 每次重新执行它。每个组最多 25 人只限制了单组大小，没有限制全组扫描量。

[`GetWorkPhase/GetWorkResult`](../../../Source/GuLiStrike/Commander/Orders/GuLiUnitTaskSubsystem.cpp#L469) 从条件路径反复调用它。对 N 个自动推进成员做全量检查时，每次所属组查询最坏要扫描 O(N) 成员，多次条件读取使整轮最坏可趋近 O(k×N²)。这是源码复杂度结论，实际个别调用耗时和本窗 N／组数没有被计时记录。

优先建立稳定的“SoldierId→GroupId”索引，覆盖新成员、取消／死亡、空组删除和组数组移动。不能缓存会随 `TArray` 增长／删除失效的裸组指针。

### 2. 相同事实被多条条件重复计算

[`Matches`](../../../Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeNodes.cpp#L25) 每次先 `HasState`，再完整 `GetBehaviorFacts`，然后按条件获取 Phase／Result。即使 Required／Forbidden 都为零，仍计算全部 Facts。`MassUnit` 每次还从 EntityManager 重新获取身份；Facts 内有额外状态表查询、策略解析和操作 Token 验证。

最新资产读回确认 `ST_CommanderMass` 为 21 个状态、16 个持久任务，编译数据与编辑图一致。“AdvanceToStronghold”叶子及祖先的活动链共 11 个 OnTick 条件候选。无转移的普通推进状态中，这些候选会重复读同一事实；其中 3 个 Result 与 4 个 Phase 检查会分别走所属组查询。11 是静态候选数，不是本窗测得的调用次数，发生转移时可能提前停止。

优先为每个单位、每轮提供 Facts／Phase／Result 决策快照，复用状态表与组索引结果。业务提交后发生版本／控制／阶段／结果变化时必须失效，再进入同一固定步的后续即时轮次，保持取消→选择、结束→选择与持久任务 Token 语义。

### 3. 稳定单位也参与同一时刻的全量检查

[`Tick`](../../../Source/GuLiStrike/Commander/Orders/GuLiUnitTaskSubsystem.cpp#L767) 每 0.1 秒刷新所有 States，并将所有存活单位 `bBehaviorStepDone=false`；随后在游戏线程执行整个匹配 Mass 查询。空闲、停止与正在稳定移动的单位均继续进入周期检查，因此行为决策成本集中于少数引擎帧。

样本中 StateTree 执行帧的整帧平均 37.655 ms，未执行帧平均 31.418 ms；这是相关性，差值还包含同周期业务提交及其他工作，不能全部算成 StateTree 或优化收益。最慢 60.545 ms 帧没有 Mass StateTree，其 21.348 ms 渲染任务等待另见前一份诊断。

在前两项完成后，评估稳定身份错峰、实际经过时间、按修订／期限唤醒，减少无变化单位全树轮询。必须保留命令、取消、死亡、外部控制、自动重试和组阶段改变的及时处理。已有 `PumpMoveAdmissions` 使用显式唤醒 Entity Collection，可作为局部机制参考。不可只关闭游戏线程限制：当前任务修改 World 子系统及请求队列，且多处检查 `IsInGameThread()`。

### 4. 业务提交路径的附加成本需要单独计时

[`CommitBehaviorRequests`](../../../Source/GuLiStrike/Commander/Orders/GuLiUnitTaskSubsystem.cpp#L720) 位于 Mass Processor 返回之后。每条请求读取提交前后 Phase／Result，执行实际业务动作，并决定是否立即再评估。因此至少四次 Phase／Result 读取还能重复查组，索引也有助于此处。该段没有独立 Scope，本轮无法量化其总成本。

目标选择／下达移动／采矿／建造业务大多在这个提交阶段执行，不能把 Processor 的 4～8 ms 直接描述成寻路耗时。请求数组容量复用可列为次要候选，但尚无分配成本证据支持其优先级。

## 已排查的低优先级因素

- 实例数据在首次 Start 时分配，后续复用；当前为持久任务图，未发现“每轮重建整个实例”的路径。
- CPU 捕获中引擎默认 `MassStateTreeProcessor_0` 每帧摊销约 0.002 ms；自定义 Fragment 与手动 Processor 路径独立，数据不支持默认／自定义同时全量 Tick 的推断。
- 同帧额外调用只占 0.18%；本窗收益重点在首次全量检查，不能按“四轮全量执行”估算倍数收益。
- Start／Stop、任务进入／退出及状态重选的已记录 CSV 成本远低于条件检查。

## 验证边界与证据

本轮完成旧 Trace 的事件导出、905 帧 CSV 阶段归纳、当前源码调用链审查以及只读资产核对。当前三棵 Commander 树 ready，compiled_matches_editor=true，dirty=false；读回 UTC 为 `2026-10-10T02:50:41.433Z`。PIE 数量为 0；无新运行采样、代码改动、构建、资源保存、地图编辑或性能收益结论。

后续细分 Scope 建议覆盖 Facts、Group Lookup、持久 Task Tick、上下文准备、请求提交和业务执行，并同时记录检查单位数、组数、条件调用数、状态转换数与各轮待处理数。优化后保持同战局／镜头／人口／命令入口，验证取消、改令、推进阶段、死亡、空组、组重排、自动重试和 Actor 采矿／建造任务。

证据：[机器分析](analysis.json)、[Mass Processor 事件](statetree-events.csv)、[逐帧分组](processor-by-frame.json)、[导出命令](export-commands.txt)、[最新行为树读回](hierarchy-readback.json)、[编辑器只读查询](editor-readback.json)、[原始 CSV](../20261010-client1-live-frame-analysis/frames.csv)、[原始 Trace](../20261010-client1-live-frame-analysis/session.utrace)。
