---
schema: guli-progress/v1
id: ARC-20261010-004
work_id: ''
kind: archive
role: root
title: 服务器StateTree条件查询与集中调度瓶颈分析
areas: [performance, commander]
categories: [gameplay, performance]
status: recorded
verification: partial
created: '2026-10-10'
updated: '2026-10-10'
summary: 复用实战30秒Trace与CSV，Mass StateTree摊销1.558ms/引擎帧，执行帧平均5.447ms、P95 7.034ms；条件检查占已记录内部成本61.64%，发现重复事实读取和推进组线性查找。
next_action: 后续优化优先建立士兵到推进组索引、每轮行为快照与稳定单位错峰，补齐子Scope及同场景复测，保持任务和即时转移合同。
relations:
  related: [ARC-20261010-003]
status_note: 用户本轮要求分析服务器StateTree，未修改玩法源码、资源或地图，未构建或重启PIE。本轮探测时PIE已结束；计时来自前一轮保存的实战捕获，当前资产通过只读检查。具体FindGroup函数占比尚未单独计时。
---

# 服务器 StateTree 条件查询与集中调度瓶颈分析

## 分析范围与证据

本轮响应“分析目前服务器StateTree性能瓶颈”，复用 [Client1 实战捕获归档](20261010-Client1实战PIE截帧与游戏线程瓶颈分析.md) 的 30 秒原始 Trace／CSV。该场景为源码版 UE5.7 的 `/Game/Maps/LVL_CommanderMassPrototype`，同进程专服＋双客户端＋编辑器，实际战局没有额外启动 600 单位／500 弹丸压力入口。CSV 905 帧，QPC 窗内 902 完整引擎帧。

本轮只读探测时 PIE 已结束，因此未进行新的运行捕获或启动游玩；编辑器资产检查仍完成，UTC 为 2026-10-10T02:50:41.433Z。

## 实测结果

| 指标 | 结果 |
|---|---:|
| Mass StateTree 全引擎帧摊销 | 1.558 ms |
| 发生执行的帧，平均／P95 | 5.447／7.034 ms |
| 单次调用平均／P95／最大 | 4.533／6.920／7.866 ms |
| 30 秒内调用／执行帧 | 310／258 |
| 同帧额外调用总成本占比 | 0.181% |
| StateTree 条件检查全引擎帧摊销 | 0.730 ms |
| 条件检查占已记录内部独占成本 | 61.64% |

条件检查数据来自聚合的 `Exclusive/GameThread/StateTree_TestConditions`，同时包含引擎表达式／绑定处理与项目条件回调；没有 World／单位标签，不能将其全部等同于 Mass 或某一个项目函数。CSV 内部字段与 CPU Mass Processor 覆盖范围不同，不能相减推算上下文准备成本。

258 个执行帧中，214 帧调用一次、36 帧两次、8 帧三次，无四次帧。52 次额外调用合计 2.539 ms，平均 0.049 ms；调用没有 DeltaTime／调用者标签，可能含即时轮次或移动接纳唤醒。数据表明本窗主成本在首次全量检查，不能按四轮全量工作估算优化收益。

服务器 World Tick 平均 12.607 ms，Mass StateTree 摊销占约 12.36%；整个进程 GT 平均 33.176 ms，摊销占约 4.70%。它是服务器热点，尚不能单独解释整帧全部瓶颈。

## 源码定位与优先级

1. `UGuLiArmyAdvanceSubsystem::FindGroup` 遍历全部 Groups 并对每组执行 Soldiers.Contains；Phase／Result 查询每次重新执行它。对 N 个自动推进成员的全量行为检查，反复成员查找最坏可趋近 O(k×N²)。优先维护 SoldierId→稳定 GroupId 索引，正确处理移除、死亡、取消和数组重排；避免缓存失效的裸组指针。
2. `Matches` 每次执行 HasState、完整 GetBehaviorFacts，再按条件读 Phase／Result；纯 Phase／Result 条件也完整计算 Facts。最新 Mass 树“AdvanceToStronghold”及祖先活动链有 11 个 OnTick 条件候选，普通无转移推进状态提供重复读取同一数据的路径。11 为静态候选数量，非运行时调用计数。优先使用同单位／同轮决策快照，业务提交后版本／阶段／结果变化时失效。
3. `UGuLiUnitTaskSubsystem::Tick` 每 0.1 秒将所有存活 States 的 bBehaviorStepDone 清零，并在游戏线程执行全量 Mass 查询；空闲、停止、稳定移动仍周期进入行为树。应在查询／缓存优化之后评估稳定身份错峰和按修订／期限唤醒，保留命令、取消、外部控制、死亡和自动重试的处理时机。
4. `CommitBehaviorRequests` 在 Mass Processor 返回后执行，至少读取提交前后的 Phase／Result，并运行实际业务动作。该段尚无独立 Scope；不能把 Processor 的 4～8 ms 全部解释成路径计算。请求数组容量复用为次要候选，尚无分配成本证据。

额外排查：实例数据只在首次 Start 分配，持久任务已存在；默认 MassStateTreeProcessor 摊销约 0.002 ms，数据不支持默认／自定义双重全量 Tick 的推断。当前任务写 World 子系统与请求队列，并包含 IsInGameThread 检查，不能仅切换并行标记。

## 资产核对与验证边界

只读命令 `gs.Commander.InspectStateTrees` 完成编辑器读回，未编译、保存或重建资产：

| 资产 | 状态数 | 持久任务数 | 编译与编辑图一致 | dirty |
|---|---:|---:|---|---|
| ST_CommanderMass | 21 | 16 | true | false |
| ST_CommanderMiner | 30 | 23 | true | false |
| ST_CommanderBuilder | 23 | 17 | true | false |

三棵树均 ready，检查 errors 为空。该结果证明当前编辑器资产配置，不将其伪装成本轮新游戏运行结果。

本轮只生成诊断报告和导出数据，没有生产逻辑修改。构建、功能测试和对应 Map 新场景交付不适用；没有优化前后收益或玩家验收结论。后续子Scope应分别记录条件、Facts、Group Lookup、上下文、请求提交和业务执行，并伴随单位／组／条件／转移／待处理数，验证改令、取消、死亡、空组、组重排、自动重试及 Actor 采矿／建造任务合同。

证据：[完整报告](../../outputs/performance/20261010-server-statetree-analysis/report.md)、[机器分析](../../outputs/performance/20261010-server-statetree-analysis/analysis.json)、[事件导出](../../outputs/performance/20261010-server-statetree-analysis/statetree-events.csv)、[逐帧分组](../../outputs/performance/20261010-server-statetree-analysis/processor-by-frame.json)、[最新资产读回](../../outputs/performance/20261010-server-statetree-analysis/hierarchy-readback.json)、[可复现分析脚本](../../outputs/performance/20261010-server-statetree-analysis/analyze.py)。
