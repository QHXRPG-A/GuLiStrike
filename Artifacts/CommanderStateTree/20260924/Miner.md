# ST_CommanderMiner｜采矿返厂｜57 状态

## 资产配置
### 单位绑定：矿车
> 资产路径：/Game/GuLiStrike/Commander/Behavior/ST_CommanderMiner.ST_CommanderMiner
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
### 09–32 Mining 分支｜审核分组
> 同组编号是 C++ AddChildState 的原始优先顺序。
#### 09 Mining_SelectFactoryWithCargo｜携货选工厂
> 必需 Flags：Active | Started | Mining | ReturnFirst | RetryReady；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningIdle；Result = None；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:126。
#### 10 Mining_SelectMineTarget｜选择矿点
> 必需 Flags：Active | Started | Mining | RetryReady；禁止 Flags：Blocked | ReturnFirst。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningIdle；Result = None；Task = MiningSelect。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:127。
#### 11 Mining_WaitForMiningPosition｜等待采矿位置
> 必需 Flags：Active | Started | Mining | RetryReady；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningWaitingPosition；Result = WaitingPosition；Task = MiningSelect。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:128。
#### 12 Mining_WaitForBudgetedMiningPath｜等待预算寻路
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningWaitingPath；Result = Running；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:129。
#### 13 Mining_MoveToMine｜前往矿点
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningReserved；Result = TargetReady；Task = MiningMove。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:130。
#### 14 Mining_BeginExtraction｜开始采集
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningMoving；Result = CanMine；Task = MiningExtract。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:131。
#### 15 Mining_RepositionForExtraction｜调整采集位置
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = OutOfRange；Task = MiningReposition。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:132。
#### 16 Mining_RejectUnreachableMiningPosition｜拒绝不可达采矿位
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningMoving；Result = Failed；Task = MiningReposition。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:133。
#### 17 Mining_CargoFull_SelectFactory｜满载后选厂
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningExtracting；Result = Full；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:134。
#### 18 Mining_MineDepleted_SelectFactory｜矿点枯竭后选厂
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningExtracting；Result = Depleted；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:135。
#### 19 Mining_LostMine_ReturnCargo｜矿点丢失后返货
> 必需 Flags：Active | Started | Mining | Cargo；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = TargetLost；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:136。
#### 20 Mining_LostMine_Retry｜矿点丢失后重试
> 必需 Flags：Active | Started | Mining | Automatic；禁止 Flags：Blocked | Cargo。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = TargetLost；Task = MiningRetry。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:137。
#### 21 Mining_LostMine_ManualFailure｜矿点丢失时手动单失败
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked | Cargo | Automatic。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = TargetLost；Task = MiningFail。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:138。
#### 22 Mining_NoTarget_Retry｜无目标时重试
> 必需 Flags：Active | Started | Mining | Automatic；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = NoTarget；Task = MiningRetry。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:139。
#### 23 Mining_NoTarget_ManualFailure｜无目标时手动单失败
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked | Automatic。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = NoTarget；Task = MiningFail。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:140。
#### 24 Mining_TryNextUnloadPoint｜尝试下一卸货点
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningReturning；Result = Failed；Task = MiningReturn。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:141。
#### 25 Mining_UnreachableFactory_SelectNext｜工厂不可达时重选
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningReturning；Result = FactoryUnreachable；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:142。
#### 26 Mining_FailedAction_Retry｜操作失败后重试
> 必需 Flags：Active | Started | Mining | Automatic；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Failed；Task = MiningRetry。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:143。
#### 27 Mining_FailedAction_ManualFailure｜操作失败时手动单失败
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked | Automatic。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Failed；Task = MiningFail。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:144。
#### 28 Mining_ReturnToFactory｜返回工厂
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningIdle；Result = FactoryReady；Task = MiningReturn。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:145。
#### 29 Mining_FactoryLost_SelectAgain｜工厂失效后重选
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = FactoryLost；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:146。
#### 30 Mining_Unload｜卸货
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningReturning；Result = AtFactory；Task = MiningUnload。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:147。
#### 31 Mining_FinishCycle｜完成循环
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningUnloading；Result = Unloaded；Task = MiningFinish。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:148。
#### 32 Mining_WaitForActionOrRetry｜等待操作或重试
> 必需 Flags：Active | Started | Mining；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:149。
### 33–54 Return 分支｜审核分组
> 同组编号是 C++ AddChildState 的原始优先顺序。
#### 33 Return_SelectFactoryWithCargo｜携货选工厂
> 必需 Flags：Active | Started | Return | ReturnFirst | RetryReady；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningIdle；Result = None；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:126。
#### 34 Return_WaitForBudgetedMiningPath｜等待预算寻路
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningWaitingPath；Result = Running；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:129。
#### 35 Return_MoveToMine｜前往矿点
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningReserved；Result = TargetReady；Task = MiningMove。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:130。
#### 36 Return_BeginExtraction｜开始采集
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningMoving；Result = CanMine；Task = MiningExtract。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:131。
#### 37 Return_RepositionForExtraction｜调整采集位置
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = OutOfRange；Task = MiningReposition。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:132。
#### 38 Return_RejectUnreachableMiningPosition｜拒绝不可达采矿位
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningMoving；Result = Failed；Task = MiningReposition。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:133。
#### 39 Return_CargoFull_SelectFactory｜满载后选厂
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningExtracting；Result = Full；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:134。
#### 40 Return_MineDepleted_SelectFactory｜矿点枯竭后选厂
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningExtracting；Result = Depleted；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:135。
#### 41 Return_LostMine_ReturnCargo｜矿点丢失后返货
> 必需 Flags：Active | Started | Return | Cargo；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = TargetLost；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:136。
#### 42 Return_LostMine_Retry｜矿点丢失后重试
> 必需 Flags：Active | Started | Return | Automatic；禁止 Flags：Blocked | Cargo。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = TargetLost；Task = MiningRetry。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:137。
#### 43 Return_LostMine_ManualFailure｜矿点丢失时手动单失败
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked | Cargo | Automatic。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = TargetLost；Task = MiningFail。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:138。
#### 44 Return_NoTarget_Retry｜无目标时重试
> 必需 Flags：Active | Started | Return | Automatic；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = NoTarget；Task = MiningRetry。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:139。
#### 45 Return_NoTarget_ManualFailure｜无目标时手动单失败
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked | Automatic。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = NoTarget；Task = MiningFail。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:140。
#### 46 Return_TryNextUnloadPoint｜尝试下一卸货点
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningReturning；Result = Failed；Task = MiningReturn。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:141。
#### 47 Return_UnreachableFactory_SelectNext｜工厂不可达时重选
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningReturning；Result = FactoryUnreachable；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:142。
#### 48 Return_FailedAction_Retry｜操作失败后重试
> 必需 Flags：Active | Started | Return | Automatic；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Failed；Task = MiningRetry。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:143。
#### 49 Return_FailedAction_ManualFailure｜操作失败时手动单失败
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked | Automatic。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Failed；Task = MiningFail。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:144。
#### 50 Return_ReturnToFactory｜返回工厂
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningIdle；Result = FactoryReady；Task = MiningReturn。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:145。
#### 51 Return_FactoryLost_SelectAgain｜工厂失效后重选
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = FactoryLost；Task = MiningFactory。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:146。
#### 52 Return_Unload｜卸货
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningReturning；Result = AtFactory；Task = MiningUnload。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:147。
#### 53 Return_FinishCycle｜完成循环
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = MiningUnloading；Result = Unloaded；Task = MiningFinish。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:148。
#### 54 Return_WaitForActionOrRetry｜等待操作或重试
> 必需 Flags：Active | Started | Return；禁止 Flags：Blocked。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = RunTask。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:149。
### 55–57 取单与空闲｜审核分组
> 同组编号是 C++ AddChildState 的原始优先顺序。
#### 55 SelectManualTask｜取手动任务
> 必需 Flags：Queued；禁止 Flags：Blocked | Active。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = TakeManual。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:174。
#### 56 SelectAutomaticTask｜取自动任务
> 必需 Flags：AutomaticReady；禁止 Flags：Blocked | Active | Queued。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = TakeAutomatic。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:176。
#### 57 Idle｜空闲
> 必需 Flags：0；禁止 Flags：0。Blocked = CancelPending | Stopped | PendingMove。
> Phase = Any；Result = Any；Task = Wait。成功返回 CommanderOrders。
> C++ 来源：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp:177。

## 审核关注点
### 矿点丢失、无目标和操作失败时的自动重试／手动失败
### 满载或矿点枯竭后的选厂与返厂
### 卸货点失败、工厂不可达与重新选择

## 核对依据与边界
### 生成逻辑：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp
### 已保存状态名回读：Artifacts/CommanderStateTree/tree-assets.json（2026-09-22）
### 运行时行为仍待玩家在 UE 场景中验收
> 本图展示已保存状态名、生成代码中的条件／任务／优先级，不把编辑器编译状态等同于游戏运行验证。
