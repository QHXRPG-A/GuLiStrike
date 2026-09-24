# ST_CommanderBuilder｜建造｜18 状态

## 资产配置
### 单位绑定：建造车
> 资产路径：/Game/GuLiStrike/Commander/Behavior/ST_CommanderBuilder.ST_CommanderBuilder
### Schema：GuLiCommanderActorStateTreeSchema
### 生命周期：Persistent；自动启用

## CommanderOrders｜根状态：按子状态顺序选择
> UE 原图中以下状态全部是 CommanderOrders 的直接子状态；导图分组仅供审核，不表示额外的 UE 状态层级。
> 每个子状态含一个 EnterCondition 和一个 BehaviorTask；成功时 GotoState(CommanderOrders)，重新按优先级选子状态。
### 01–08 入口与人工控制｜审核分组
> 同组编号是 C++ AddChildState 的原始优先顺序。
#### 01 WaitingSafeExit｜等待安全退出
> 必需 Flags：CancelPending；禁止 Flags：0。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = CancelPending。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:108。
#### 02 Stopped｜停止
> 必需 Flags：Stopped；禁止 Flags：CancelPending。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = Wait。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:109。
#### 03 PreparingReplacementMove｜准备替换移动
> 必需 Flags：PendingMove；禁止 Flags：CancelPending | Stopped。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = ReplaceMove。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:110。
#### 04 SuspendedByExternalControl｜外部控制暂停
> 必需 Flags：Active | Suspended；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:111。
#### 05 StartTask｜启动任务
> 必需 Flags：Active；禁止 Flags：Blocked | Started。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:112。
#### 06 WorkUnitFinished｜工作单元完成
> 必需 Flags：Active | Started | WorkComplete；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:113。
#### 07 ManualMove｜手动移动
> 必需 Flags：Active | GuLiCommanderBehaviorFacts::Move；禁止 Flags：Blocked | Automatic。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:114。
#### 08 ManualTransit｜手动运输
> 必需 Flags：Active | Transit；禁止 Flags：Blocked | Automatic。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:115。
### 09–15 施工与换单｜审核分组
> 同组编号是 C++ AddChildState 的原始优先顺序。
#### 09 SelectConstructionDestination｜选择施工目的地
> 必需 Flags：Active | Started | Construction；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = ConstructionPrepared；Result = None；Task = ConstructionReserve。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:154。
#### 10 ReturnCancelledConstructionOrder｜退回已取消施工单
> 必需 Flags：Active | Started | Construction；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = ConstructionCancelled；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:155。
#### 11 MoveToConstructionSite｜前往施工地点
> 必需 Flags：Active | Started | Construction；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = ConstructionIntended；Result = TargetReady；Task = ConstructionMove。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:156。
#### 12 WaitForBudgetedConstructionPath｜等待预算寻路
> 必需 Flags：Active | Started | Construction；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = ConstructionWaitingPath；Result = Running；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:157。
#### 13 RetryConstructionPosition｜重试施工位置
> 必需 Flags：Active | Started | Construction；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = OutOfRange；Task = ConstructionRetry。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:158。
#### 14 ClaimPositionOnArrivalAndConstruct｜抵达抢位并施工
> 必需 Flags：Active | Started | Construction；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = ConstructionMoving；Result = Arrived；Task = ConstructionWork。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:159。
#### 15 WorkingOrWaitingForArrival｜施工或等待抵达
> 必需 Flags：Active | Started | Construction；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:160。
### 16–18 取单与空闲｜审核分组
> 同组编号是 C++ AddChildState 的原始优先顺序。
#### 16 SelectManualTask｜取手动任务
> 必需 Flags：Queued；禁止 Flags：Blocked | Active。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = TakeManual。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:174。
#### 17 SelectAutomaticConstructionSite｜选自动施工单
> 必需 Flags：AutomaticReady；禁止 Flags：Blocked | Active | Queued。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = TakeAutomatic。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:176。
#### 18 Idle｜空闲
> 必需 Flags：0；禁止 Flags：0。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = Wait。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:177。

## 审核关注点
### 外据点施工单在途中遇到本据点新单时退单
### 抵达后才抢占施工位，失败时重试
### 已经占位施工时保持任务

## 核对依据与边界
### 生成逻辑：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp
### 已保存状态名回读：Artifacts/Map2300/20260923/builder-tree-readback.json（2026-09-23，Compiled）
### 运行时行为仍待玩家在 UE 场景中验收
> 本图展示已保存状态名、生成代码中的条件／任务／优先级，不把编辑器编译状态等同于游戏运行验证。
