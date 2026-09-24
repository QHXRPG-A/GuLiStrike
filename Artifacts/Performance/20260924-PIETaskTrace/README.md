# 当前 PIE 客户端 Tick 任务追踪（2026-09-24 15:28）

## 采样

- UE 5.7 编辑器进程 PID 75840，`LVL_CommanderMassPrototype` 的单客户端嵌入式 PIE；编辑器里仍有进程内服务端。
- 通过本地 UE Trace 控制接口录制约 12 秒，通道包含 CPU、Frame、Task、Bookmark、Log、Counter。没有操作编辑器窗口或修改关卡设置。
- 原始数据：`client-task-12s.utrace`。分析只使用末尾 12 秒，共 590 个客户端 `UWorld_Tick` 和 4,130 次客户端 `ProcessUntilTasksComplete`，每次客户端 Tick 恰好 7 次。
- 此次为编辑器重启后的新 PIE 会话。负载状态与此前 14:05 的采样不同，不能把两个采样的绝对耗时混算。

## `ProcessUntilTasksComplete` 属于哪个 Tick 组

依据 `UWorld::Tick` 的 `RunTickGroup` 顺序、`TG_DuringPhysics` 的非阻塞执行及下一次 `TG_EndPhysics` 调用中的补等逻辑，将每次客户端 World Tick 中的 7 个计时区间映射如下：

| Tick 组 | 平均包含耗时 | P95 | 占 7 组总包含耗时 | 主要内层独占耗时（每次客户端 Tick） |
| --- | ---: | ---: | ---: | --- |
| `TG_PrePhysics` | 1.455 ms | 1.821 ms | 62.3% | 角色移动 0.255 ms、蒙皮组件 Tick 0.252 ms、并行动画结果收取 0.178 ms、单节点动画 0.104 ms |
| `TG_StartPhysics` | 0.046 ms | 0.055 ms | 2.0% | 启动物理模拟 |
| `TG_DuringPhysics`（在 EndPhysics 中补等） | 0.008 ms | 0.008 ms | 0.3% | 很短的任务收尾 |
| `TG_EndPhysics` | 0.258 ms | 0.358 ms | 11.1% | `WaitForTasks` 0.170 ms、GameThread `ExecuteTask` 0.081 ms |
| `TG_PostPhysics` | 0.026 ms | 0.032 ms | 1.1% | 相机、SpringArm 等 |
| `TG_PostUpdateWork` | 0.527 ms | 0.672 ms | 22.6% | 士兵显示插值 0.401 ms、实例变换批量更新 0.056 ms |
| `TG_LastDemotable` | 0.014 ms | 0.044 ms | 0.6% | 显示实例批次执行 |

7 组平均包含耗时合计约 2.334 ms。占比仅在本表内部计算，不代表整个客户端 World Tick 或进程帧间隔的占比。

## 真正等待的任务

- `TaskTrace.WaitingStarted/Finished` 在 `TG_EndPhysics` 的 590 次调用中匹配到 587 次，等待区间平均 0.257 ms，最长 0.737 ms。这个 TaskTrace 区间既包含实际挂起，也包含期间 GameThread 执行的完成任务。
- 同一组的 CPU 子事件里，`WaitForTasks`（任务队列事件等待）平均 0.170 ms，`ExecuteTask`（GameThread 上的完成工作）平均 0.081 ms。因此 0.257 ms 不能全称为纯空等。
- 30 个最长的等待区间，其上游任务链均有工作线程任务，且对应 CPU 事件含 `PhysicsParallelForWithContext` 或 `Chaos_PhysicsParallelFor`。
- 最长的 0.737 ms 样本：等待 `TaskId=218446381` 完成；它依赖 GameThread 任务 `218446827`（执行 0.053 ms），后者依赖工作线程任务 `218446826`（执行 0.406 ms）及其上游 `218446825`（执行 0.099 ms）。该链与引擎 `FEndPhysicsTickFunction::ExecuteTick` 中等待 `PhysScene->GetCompletionEvents()`、再调度 `FinishPhysicsSim` 的流程一致。
- TaskTrace 给这些任务的 `DebugName` 仅为 `GraphTask`，不能从 TaskTrace 自身进一步命名 Chaos 内部的具体子算法；CPU 内层标记与引擎调用链支持“Chaos 物理完成链”这一归因。

## 含义

本次 `ProcessUntilTasksComplete` 最大部分是 `TG_PrePhysics` 上主线程实际执行角色移动和骨骼动画，并非等待。实际的任务等待位于 `TG_EndPhysics`，与 Chaos 物理模拟完成相关。`TG_PostUpdateWork` 的主要工作是客户端士兵显示插值。

## 文件与实现依据

- `task-waits-summary.json`：逐 Tick 组统计、最长的调用与等待区间。
- `task-critical-chains.json`：30 个最长等待的 TaskTrace 前置依赖链。
- `task-cpu-scopes.json`：这些任务执行期间的 CPU scope。
- `analyze_task_waits.py`、`analyze_critical_tasks.py`、`attribute_task_cpu.py`：分析方法。
- 引擎源码：`Engine/Source/Runtime/Engine/Private/LevelTick.cpp` 的 `UWorld::Tick`、`Engine/Source/Runtime/Engine/Private/TickTaskManager.cpp` 的 `ReleaseTickGroup`、`Engine/Source/Runtime/Engine/Private/PhysicsEngine/PhysLevel.cpp` 的 `FEndPhysicsTickFunction::ExecuteTick`。
