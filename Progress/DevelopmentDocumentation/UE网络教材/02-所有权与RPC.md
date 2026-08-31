# 02 所有权与 RPC

[教材目录](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/README.md) · [上一章](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/01-UE网络模型与对象职责.md) · [下一章](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/03-属性复制与RepNotify.md)

## 学习目标

能沿 NetSync 的声明、调用和接收实现追踪一次跨端通信，理解所有权为何先于玩法校验。

## UE 概念

Owning Connection 把 Actor 关联到一个玩家连接。组件通过所属 Actor 找到连接，不能独立创建一条 RPC 通道。客户端调用 Server RPC 需要拥有该对象；服务器的 Client RPC 路由到拥有客户端。NetMulticast 通常由服务器发给相关客户端；本项目核心命令采用单连接通信，没有靠多播分发私人选择。[Epic UE 5.7 所有权](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-owner-and-owning-connection-in-unreal-engine?application_version=5.7)

Reliable 提供可靠传输机制，但不是“玩法成功”。RPC 没有同步返回值；调用普通名字进入 UE 包装层，接收端执行同名的 `_Implementation`。[Epic UE 5.7 RPC](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine?application_version=5.7)

## 项目调用链与源码

[UGuLiPlayerNetSyncComponent](D:/UE5.7/test1/Source/GuLiStrike/Battle/Network/GuLiPlayerNetSyncComponent.cpp) 打开组件复制并处理公共握手。BattlePlayerController 创建名为 `CommanderNetSync` 的唯一默认网络子对象；旧 CommanderController 用构造初始化器替换其具体类型，旧属性仍指向同一对象，没有增加第二条连接。

项目源码节选（[AGuLiCommanderPlayerController 构造函数初始化列表](D:/UE5.7/test1/Source/GuLiStrike/Commander/Framework/GuLiCommanderPlayerController.cpp)）：

```cpp
: Super(ObjectInitializer.SetDefaultSubobjectClass<UGuLiCommanderNetSyncComponent>(PlayerNetSyncComponentName))
```

派生 NetSync 保留选兵、移动与姿态 RPC。可靠选兵声明如下：

项目源码节选（[UGuLiCommanderNetSyncComponent::ServerRequestSelection](D:/UE5.7/test1/Source/GuLiStrike/Commander/Framework/GuLiCommanderNetSyncComponent.h)）：

```cpp
UFUNCTION(Server, Reliable)
void ServerRequestSelection(const FGuLiSelectionRequest& Request);
```

移动采用同样的声明形式；接收实现不直接把请求判为成功：

项目源码节选（[void UGuLiCommanderNetSyncComponent::ServerIssueMove_Implementation](D:/UE5.7/test1/Source/GuLiStrike/Commander/Framework/GuLiCommanderNetSyncComponent.cpp)）：

```cpp
void UGuLiCommanderNetSyncComponent::ServerIssueMove_Implementation(const FGuLiMoveRequest& Request)
{
    HandleMoveRequest(Request, true);
}
```

它将工作交给 [UGuLiCommanderNetSyncComponent::HandleMoveRequest](D:/UE5.7/test1/Source/GuLiStrike/Commander/Framework/GuLiCommanderNetSyncComponent.cpp)；服务器结果由 PublishAck 选择可靠或快速 Client RPC，最终汇入 ReceiveCommandAck。

可靠 RPC 在同一 Actor 及所属子对象通道内的顺序约束，不能扩展为跨 Actor 的全局顺序，也不能据此断言可靠与不可靠 RPC 混用时总按调用顺序到达；不同复制变量的 OnRep 顺序同样不能依赖。[Epic 执行顺序说明（版本边界见教材目录）](https://dev.epicgames.com/documentation/zh-cn/unreal-engine/replicated-object-execution-order-in-unreal-engine)

## 易错点

拥有连接只允许走合法路由，不代表该玩家可指挥所有士兵。直接调用 `_Implementation` 只是本地函数调用，不会发送 RPC。项目的 `DECLARE_MULTICAST_DELEGATE` 是本地多订阅者通知，与 NetMulticast 无关。

## 练习与答案

1. **理解题：可靠移动 RPC 返回后可以显示“已到达”吗？** 不可以，甚至尚未拿到业务接令结果。
2. **理解题：把组件变量改为 Replicated 就能绕过所有权吗？** 不能，组件依然使用拥有 Actor 的连接。
3. **源码题：快速移动与可靠移动是否有两套寻路？** 没有，ServerIssueMoveFast 与可靠入口都调用 HandleMoveRequest，只改变回执的可靠性选择。
