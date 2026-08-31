# 03 属性复制与 RepNotify

[教材目录](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/README.md) · [上一章](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/02-所有权与RPC.md) · [下一章](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/04-项目协议与序列化.md)

## 学习目标

从项目字段追到复制注册和本地消费者，区分“当前状态”与“一次请求的结果”。

## UE 概念

Actor 开启 `bReplicates` 后，还要声明需要复制的字段，并在 GetLifetimeReplicatedProps 注册。ReplicatedUsing 指定客户端收到属性更新后的回调；C++ 服务器普通赋值不会替服务器自动执行该回调。属性同步让副本趋向服务器当前值，不承诺客户端看见每一次中间赋值。[Epic UE 5.7 属性复制](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicate-actor-properties-in-unreal-engine?application_version=5.7)

相关性决定连接是否需要对象，复制条件进一步限制字段；更新频率是调度参数，不是固定收包率。条件满足、连接持续且更新继续时谈状态收敛，不能把它理解成断线后仍保证送达。

## 项目调用链与源码

NetSync 的选择与代次只给拥有者：


项目源码节选（[DOREPLIFETIME_CONDITION(UGuLiCommanderNetSyncComponent, SelectionState](D:/UE5.7/test1/Source/GuLiStrike/Commander/Framework/GuLiCommanderNetSyncComponent.cpp:140)）：

```cpp
DOREPLIFETIME_CONDITION(UGuLiCommanderNetSyncComponent, SelectionState, COND_OwnerOnly);
DOREPLIFETIME_CONDITION(UGuLiCommanderNetSyncComponent, SyncGeneration, COND_OwnerOnly);
```


链路为服务器 HandleSelectionRequest 更新 SelectionState → 属性复制 → [UGuLiCommanderNetSyncComponent::OnRep_SelectionState](D:/UE5.7/test1/Source/GuLiStrike/Commander/Framework/GuLiCommanderNetSyncComponent.cpp:956) → 本地 OnSelectionChanged → HUD。服务器更新时也主动调用 NotifySelectionChanged，使主机本地界面得到通知。

公共名册走 [AGuLiSoldierStateReplicator::ApplyAuthoritySnapshot](D:/UE5.7/test1/Source/GuLiStrike/Commander/Network/GuLiSoldierStateReplicator.cpp:27)。新增/修改调用 MarkItemDirty，删除调用 MarkArrayDirty；FastArray 根据内部复制标识发送增量，业务 SoldierId 仍用于稳定关联。


项目源码节选（[ReplicatedSoldiers.MarkItemDirty(ExistingItem);](D:/UE5.7/test1/Source/GuLiStrike/Commander/Network/GuLiSoldierStateReplicator.cpp:93)）：

```cpp
ReplicatedSoldiers.MarkItemDirty(ExistingItem);
++ChangedItemCount;
```


快照变化推进 SnapshotRevision，客户端 OnRep_SnapshotRevision 广播 OnSoldierStatesChanged。这里的“可靠状态”指持续同步的事实副本，不能推导每次变化都作为 Reliable RPC 被完整重放。

## 易错点

LastCommandAck 没有 Replicated 声明，实际由 Client RPC 传输；只读最近值会漏掉同帧多个回执，所以另有 ACK 队列。名册、SnapshotRevision 与 MatchEpoch 也不是跨字段事务，Bootstrap 还要验证数量和身份。ForceNetUpdate 不等于立即到达。

## 练习与答案

1. **理解题：OwnerOnly 选择能给旁观客户端直接读取吗？** 不能，这个字段只送拥有连接。
2. **理解题：为什么服务器要主动广播选择通知？** C++ 服务器赋值不会自动走客户端 OnRep 路径。
3. **源码题：删除名册成员却未 MarkArrayDirty 会怎样？** 集合变了但增量复制缺少删除脏标记；当前 ApplyAuthoritySnapshot 在批量删除后显式调用它。
