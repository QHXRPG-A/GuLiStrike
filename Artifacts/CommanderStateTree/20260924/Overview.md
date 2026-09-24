# GuLiStrike｜三棵指挥官 StateTree 审核总览

## 三棵资产与单位绑定
### ST_CommanderMass｜据点推进｜18 状态
> 扫荡者、战争机器共用此资产；Mass Schema；InitialOnce；每单位独立运行实例。
### ST_CommanderMiner｜采矿返厂｜57 状态
> 矿车使用；Actor Schema；Persistent；每车独立运行实例。
### ST_CommanderBuilder｜建造｜18 状态
> 建造车使用；Actor Schema；Persistent；每车独立运行实例。2026-09-23 仅此树更新接单、退单、移动与到位抢位阶段。

## 读图规则
### CommanderOrders 按子状态顺序选择
> SelectionBehavior = TrySelectChildrenInOrder。编号表示优先级，不能当作线性执行流程。
### 每个状态的准入、任务和成功回跳
> 每个直接子状态都设 Required/Forbidden Flags、Phase/Result 条件和一个 BehaviorTask；OnStateSucceeded → CommanderOrders。
### 导图分组只是审核导航
> 具体资产的所有编号状态均直接挂在 UE 原图的 CommanderOrders 下；导图里的入口、业务、取单分组不属于 UE 原图。
### 状态条件符号
> Blocked = CancelPending | Stopped | PendingMove；Any 表示该筛选维度不限；Flags=0 表示没有该类标志约束。

## 各树审核重点
### Mass：无据点／不可达／占领完成后如何重选
### Miner：采矿与返厂两组状态的目标丢失、失败和卸货
### Builder：本地优先、途中退单、抵达抢位与已施工锁单

## 来源与审查边界
### 状态条件与任务：Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp
### Mass／Miner 已保存状态名：Artifacts/CommanderStateTree/tree-assets.json
### Builder 最新状态名：Artifacts/Map2300/20260923/builder-tree-readback.json
### 单位绑定：Progress/Gameplay/指挥官/01-战局选兵与移动.md
### 建造规则：Progress/DevelopmentDocumentation/20260922-导航内存优化与对局容量预算.md
### 本图用于结构审核；运行时效果待玩家验收
