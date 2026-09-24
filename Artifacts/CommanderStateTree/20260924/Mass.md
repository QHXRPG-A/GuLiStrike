# ST_CommanderMass｜据点推进｜18 状态

## 资产配置
### 单位绑定：扫荡者、战争机器
> 资产路径：/Game/GuLiStrike/Commander/Behavior/ST_CommanderMass.ST_CommanderMass
### Schema：GuLiCommanderMassStateTreeSchema
### 生命周期：InitialOnce；自动启用

## CommanderOrders｜根状态：按子状态顺序选择
> UE 原图中以下状态全部是 CommanderOrders 的直接子状态；导图分组仅供审核，不表示额外的 UE 状态层级。
> 每个子状态含一个 EnterCondition 和一个 BehaviorTask；成功时 GotoState(CommanderOrders)，重新按优先级选子状态。
### 01–07 入口与人工控制｜审核分组
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
### 08–15 据点推进｜审核分组
> 同组编号是 C++ AddChildState 的原始优先顺序。
#### 08 SelectStronghold｜选择据点
> 必需 Flags：Active | Started | Advance；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = AdvanceSelecting；Result = None；Task = AdvanceSelect。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:165。
#### 09 MoveToStronghold｜前往据点
> 必需 Flags：Active | Started | Advance；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = AdvanceSelecting；Result = TargetReady；Task = AdvanceMove。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:166。
#### 10 NoStronghold_Wait｜无据点时等待
> 必需 Flags：Active | Started | Advance；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = AdvanceSelecting；Result = NoTarget；Task = AdvanceWait。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:167。
#### 11 WaitForCapture｜等待占领
> 必需 Flags：Active | Started | Advance；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = AdvanceMoving；Result = Arrived；Task = AdvanceCapture。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:168。
#### 12 RejectUnreachableStronghold｜拒绝不可达据点
> 必需 Flags：Active | Started | Advance；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Failed；Task = AdvanceReject。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:169。
#### 13 StrongholdCaptured｜据点占领完成
> 必需 Flags：Active | Started | Advance；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Complete；Task = AdvanceComplete。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:170。
#### 14 RetryStrongholdSelection｜重新选择据点
> 必需 Flags：Active | Started | Advance；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = AdvanceWaiting；Result = NoTarget；Task = AdvanceSelect。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:171。
#### 15 AdvancingOrCapturing｜推进或占领中
> 必需 Flags：Active | Started | Advance；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:172。
### 16–18 取单与空闲｜审核分组
> 同组编号是 C++ AddChildState 的原始优先顺序。
#### 16 SelectManualTask｜取手动任务
> 必需 Flags：Queued；禁止 Flags：Blocked | Active。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = TakeManual。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:174。
#### 17 SelectAutomaticTask｜取自动任务
> 必需 Flags：AutomaticReady；禁止 Flags：Blocked | Active | Queued。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = TakeAutomatic。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:176。
#### 18 Idle｜空闲
> 必需 Flags：0；禁止 Flags：0。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = Wait。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:177。

## 审核关注点
### 无目标时等待并重试
### 不可达据点的拒绝与重新选择
### 抵达后占领、完成条件

## 核对依据与边界
### 生成逻辑：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp
### 已保存状态名回读：Artifacts/CommanderStateTree/tree-assets.json（2026-09-21）
### 运行时行为仍待玩家在 UE 场景中验收
> 本图展示已保存状态名、生成代码中的条件／任务／优先级，不把编辑器编译状态等同于游戏运行验证。
