# 精读笔记：MassEntityElementTypes.h —— 用 Commander 500 人实现理解五种 Mass 元素

> 重写日期：2026-08-28
>
> 引擎基线：Unreal Engine 5.7.4，CL 51494982
>
> 引擎原文件：`C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityElementTypes.h`
>
> 项目样本：2026-08-27 新增的 `Source/GuLiStrike/Commander/` 实现
>
> 时间边界：Commander/Mass、Network、Framework 源码均在 8 月 27 日创建；`GuLiCommanderPresentationActor.cpp` 在 8 月 28 日 01:08 跨午夜续改，涉及它的内容按“昨夜开发后的当前版本”表述。
>
> Git 边界：`Source/GuLiStrike/Commander/` 当前仍未提交；“8 月 27 日新增”依据文件时间、当天归档和运行日志，不是某个 Git commit 的逐行历史。

## 先说结论

`MassEntityElementTypes.h` 只定义五个几乎没有实现代码的基类，但它们决定了数据的**拥有粒度**：

| 基类 | 数据粒度 | Commander 当前实例 | 当前是否使用 |
|---|---|---|---|
| `FMassFragment` | 每个 Entity 一份 | Identity、Health、Order、SlotTarget、AvoidanceOutput，以及引擎 Transform/Velocity/Force 等 | 是 |
| `FMassTag` | 只表达组成中的有/无，不携带每实体 payload | ServerAuthority、ClientSnapshotMirror | 是 |
| `FMassChunkFragment` | 每个 Mass Chunk 一份 | 无 | 否 |
| `FMassSharedFragment` | 一组实体共享一份、允许修改 | 无 | 否 |
| `FMassConstSharedFragment` | 一组实体共享一份、查询侧只读 | MovementParameters、MovingAvoidanceParameters | 是，使用引擎内建类型 |

昨天的实现没有自定义 `FMassChunkFragment` 或可变 `FMassSharedFragment`。本文不会为它们编造项目案例；只解释它们与现有代码的边界。

## 一、引擎头文件究竟定义了什么

UE 5.7.4 的核心定义可以压缩为：

~~~cpp
USTRUCT()
struct FMassFragment
{
    GENERATED_BODY()
};

USTRUCT()
struct FMassTag
{
    GENERATED_BODY()
};

USTRUCT()
struct FMassChunkFragment
{
    GENERATED_BODY()
};

USTRUCT()
struct FMassSharedFragment
{
    GENERATED_BODY()
};

USTRUCT()
struct FMassConstSharedFragment
{
    GENERATED_BODY()
};
~~~

它们本身不提供移动、生命、查询或存储逻辑。派生关系的作用是让 Mass 在编译期和运行时把一个 `UScriptStruct` 归入正确类别，随后放进对应的类型位集：

- `FMassFragmentBitSet`
- `FMassTagBitSet`
- `FMassChunkFragmentBitSet`
- `FMassSharedFragmentBitSet`
- `FMassConstSharedFragmentBitSet`

这些位集共同描述 Archetype 的数据组成。也就是说，继承哪个基类不是命名偏好，而是存储和查询合同。

## 二、FMassFragment：每名 Soldier 自己的数据

### 2.1 昨天新增的真实 Fragment

源码：`Source/GuLiStrike/Commander/Mass/GuLiCommanderMassFragments.h`

~~~cpp
/** Stable per-match Soldier identity. It is independent of selection and order formations. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassIdentityFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    FGuLiSoldierId SoldierId;

    UPROPERTY(Transient)
    EGuLiTeam Team = EGuLiTeam::Unassigned;
};

/** Server-authoritative health and the five-second destroyed presentation window. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassHealthFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    uint8 Health = 100u;

    UPROPERTY(Transient)
    bool bDead = false;

    UPROPERTY(Transient)
    float WreckSecondsRemaining = 0.0f;

    bool IsAlive() const;
};

/** The latest accepted server order for one Soldier. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassOrderFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    uint32 ActiveOrderId = 0u;

    UPROPERTY(Transient)
    uint32 OrderRevision = 0u;

    UPROPERTY(Transient)
    FVector FormationTarget = FVector::ZeroVector;

    UPROPERTY(Transient)
    bool bHasMoveTarget = false;
};
~~~

逐个看它们在玩法中的职责：

> 本文中文术语：服务端接受并持续执行的 `Order` 统一称为“单位指令”，当前 `IssueMove` 场景具体称为“移动指令”；不使用容易与商业购买混淆的“订单”。

- `FGuLiMassIdentityFragment` 在 Mass 数据中保存 `SoldierId` 与队伍的副本，供 Mass 侧处理、调试和客户端镜像使用。当前选择、指令和网络查找的主路径实际走 `FSoldierRuntime`、业务映射与 `SoldierId`，并没有靠查询这个 Fragment 完成业务寻址。
- `FGuLiMassHealthFragment` 保存服务器权威生命状态，并实现了 5 秒残骸展示窗口；不过现有 smoke 没有专项验证客户端确实完整显示了这 5 秒，因此这里应区分“源码已实现”和“运行已验证”。
- `FGuLiMassOrderFragment` 保存某名 Soldier 最新接受的移动指令。动态 25 人 Cohort 只是一次选择结果，不是永久编制；指令最终仍写回每名独立 Soldier。

同一文件还定义：

| Fragment | 字段 | 真实用途 |
|---|---|---|
| `FGuLiMassSlotTargetFragment` | `LocalOffset`、`WorldTarget` | 保存弹性编队槽位及当前世界目标 |
| `FGuLiMassAvoidanceOutputFragment` | `Value` | 保存引擎 Mass Avoidance 在当前世界帧算出的力，供 30Hz 权威固定步消费 |

服务器 Archetype 还实际使用引擎 Fragment：

- `FTransformFragment`
- `FAgentRadiusFragment`
- `FMassVelocityFragment`
- `FMassForceFragment`
- `FMassMoveTargetFragment`
- `FMassNavigationEdgesFragment`
- `FMassNavigationObstacleGridCellLocationFragment`

自定义 Fragment 并没有取代引擎移动数据，而是补上 GuLiStrike 自己的身份、生命、指令、编队槽位和固定步避障桥接。

### 2.2 右键移动如何真正写入 Fragment

源码：`UGuLiBattleAuthoritySubsystem::IssueMove`

~~~cpp
FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
Soldier.ActiveOrderId = BatchOrderId;
++Soldier.StateRevision;

FGuLiMassOrderFragment& Order =
    AuthorityState->MassEntitySubsystem->GetMutableEntityManager()
        .GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
Order.ActiveOrderId = BatchOrderId;
Order.OrderRevision = Soldier.StateRevision;
Order.FormationTarget = Formation.TargetAnchor;
Order.bHasMoveTarget = true;

FMassMoveTargetFragment& MoveTarget =
    AuthorityState->MassEntitySubsystem->GetMutableEntityManager()
        .GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
MoveTarget.CreateNewAction(EMassMovementAction::Move, *GetWorld());
MoveTarget.IntentAtGoal = EMassMovementAction::Stand;
MoveTarget.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);
~~~

这段代码对应玩家的一次右键移动：

1. 网络请求先通过 SelectionRevision、队伍、存活、路径和终点足迹校验。
2. 服务端把新的 BatchOrderId 写入自己的 Soldier 注册表。
3. 再用该 Soldier 的 `FMassEntityHandle` 找到 `OrderFragment` 和引擎 `MoveTargetFragment`。
4. Order 保存业务事实；MoveTarget 接入 Mass 移动管线。

Fragment 是数据，不是行为类。真正决定“什么时候写、谁有权写”的，是服务端子系统和 Processor。

### 2.3 每个固定步如何回写可见状态

源码：`UGuLiBattleAuthoritySubsystem::TickAuthority`

~~~cpp
FTransformFragment& Transform =
    EntityManager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);
Transform.SetTransform(FTransform(
    FRotator(0.0f, Soldier.FacingYawDegrees, 0.0f),
    Soldier.Location));

EntityManager
    .GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity)
    .Value = Soldier.Velocity;

FGuLiMassOrderFragment& Order =
    EntityManager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
Order.ActiveOrderId = Soldier.ActiveOrderId;
Order.OrderRevision = Soldier.StateRevision;
Order.bHasMoveTarget = Soldier.ActiveOrderId != 0u;
~~~

这解释了为什么每个实体需要自己的 Fragment：500 名 Soldier 的位置、速度、朝向和指令可以不同，不能把它们塞进 SharedFragment。

## 三、FMassTag：把服务端权威体和客户端镜像彻底隔开

### 3.1 两个真实 Tag

源码：`GuLiCommanderMassFragments.h`

~~~cpp
/** Snapshot-driven client mirror. It intentionally carries no movement or avoidance tag. */
USTRUCT()
struct GULISTRIKE_API FGuLiClientSnapshotMirrorMassTag : public FMassTag
{
    GENERATED_BODY()
};

/** Marks the exact 500-member server archetype. Client mirrors must never carry this tag. */
USTRUCT()
struct GULISTRIKE_API FGuLiServerAuthorityMassTag : public FMassTag
{
    GENERATED_BODY()
};
~~~

Tag 的准确理解是：它没有每实体业务 payload，不需要像 Health 那样建立一列数值；它以“该组成是否包含此类型”参与 Archetype 和 Query 匹配。不要把它简单理解成 C++ 层面“结构体大小绝对为零”。

### 3.2 Tag 如何阻止客户端镜像被服务器 Processor 处理

服务器避障捕获 Query：

~~~cpp
EntityQuery.AddRequirement<FMassForceFragment>(EMassFragmentAccess::ReadWrite);
EntityQuery.AddRequirement<FGuLiMassAvoidanceOutputFragment>(EMassFragmentAccess::ReadWrite);
EntityQuery.AddTagRequirement<FGuLiServerAuthorityMassTag>(EMassFragmentPresence::All);
~~~

客户端镜像 Archetype 只有：

~~~cpp
const TArray<const UScriptStruct*> FragmentAndTagTypes = {
    FTransformFragment::StaticStruct(),
    FGuLiMassIdentityFragment::StaticStruct(),
    FGuLiMassHealthFragment::StaticStruct(),
    FGuLiClientSnapshotMirrorMassTag::StaticStruct()
};
~~~

因此有两道隔离：

1. 客户端镜像没有 Force 和 AvoidanceOutput。
2. 客户端镜像没有 ServerAuthorityTag。

即使未来某一侧补了相同 Fragment，Tag 仍能表达“这是不是服务器权威模拟体”。这比只依赖当前列集合更稳健。

源码注释把 ServerAuthorityTag 写作标记“exact 500-member server archetype”，这是出生组成的设计意图；死亡移除导航 Fragment 后，该 Tag 仍保留在迁移后的 Archetype。按当前实际行为，它更准确地表达 Authority 处理域，而不是“永远只对应一个 Archetype Handle”。

## 四、FMassConstSharedFragment：500 人共享移动与避障参数

项目没有自定义 ConstShared 类型，但真实使用了两个引擎类型：

- `FMassMovementParameters : FMassConstSharedFragment`
- `FMassMovingAvoidanceParameters : FMassConstSharedFragment`

源码：`UGuLiBattleAuthoritySubsystem::TrySpawnAuthorityPopulation`

~~~cpp
FMassMovementParameters MovementParameters;
MovementParameters.MaxSpeed = MovementSpeedCentimetersPerSecond;
MovementParameters.DefaultDesiredSpeed = MovementSpeedCentimetersPerSecond;
MovementParameters.DefaultDesiredSpeedVariance = 0.0f;
MovementParameters.MaxAcceleration = MovementSpeedCentimetersPerSecond * 4.0f;
MovementParameters.bIsCodeDrivenMovement = true;
MovementParameters.Update();

FMassMovingAvoidanceParameters AvoidanceParameters;
AvoidanceParameters.ObstacleDetectionDistance = MemberAgentRadiusCentimeters * 8.0f;
AvoidanceParameters.SeparationRadiusScale = 0.95f;
AvoidanceParameters.ObstacleSeparationDistance = MemberAgentRadiusCentimeters * 0.35f;
AvoidanceParameters.PredictiveAvoidanceDistance = MemberAgentRadiusCentimeters * 0.35f;

FMassArchetypeSharedFragmentValues SharedValues;
SharedValues.Add(
    EntityManager.GetOrCreateConstSharedFragment(MovementParameters.GetValidated()));
SharedValues.Add(
    EntityManager.GetOrCreateConstSharedFragment(AvoidanceParameters.GetValidated()));
SharedValues.Sort();
~~~

为什么这两个参数适合 ConstShared：

- 500 名服务器 Soldier 使用同一套最大速度、加速度和避障尺度。
- 它们是配置，不是某名 Soldier 的瞬时状态。
- Query/Processor 只应读取，不应逐实体改写。
- 共享一份可以避免把相同配置复制 500 次。

`FMassArchetypeSharedFragmentValues` 是“共享值容器”，不是某一种 SharedFragment。它同时装可变 Shared 和 ConstShared，并维护各自类型位集。

还有一个容易写错的点：**共享值本身不是 Archetype 身份的一部分**。Archetype 的组成描述记录 Shared/ConstShared 的类型位集；具体值随创建请求传入，并用于 Chunk 的共享值分组和绑定。昨天的代码也正是先用类型位集取得合适 Archetype，再把 `SharedValues` 传给批量创建。

## 五、当前没有使用的两种元素

### 5.1 FMassSharedFragment

当前 `Source/` 与 `Plugins/**/Source/` 没有项目自定义可变 SharedFragment。

服务器的临时编队关系放在：

~~~cpp
struct FOrderFormationRuntime
{
    uint32 FormationId = 0u;
    uint32 BatchOrderId = 0u;
    FGuLiControlCohortId SourceCohortId;
    EGuLiTeam Team = EGuLiTeam::Unassigned;
    TArray<FGuLiSoldierId> MemberIds;
    TMap<uint32, uint8> SlotBySoldierId;
    FVector GuideAnchor = FVector::ZeroVector;
    FVector TargetAnchor = FVector::ZeroVector;
    // ...
};
~~~

从当前代码可以作如下设计解读（推断，不是源码注释明确声明的动机）：ControlCohort 和 OrderFormation 生命周期短、成员会变化，而且“25 人”不是永久 Mass 分组。如果把它们做成 SharedFragment，成员或路径变化会频繁改变共享值，使实体重新归组到具有匹配共享值的 Chunk。共享值改变通常仍发生在同一 Archetype 内；只有共享 Fragment 的类型组成改变时，才涉及 Archetype 结构迁移。

未来只有在“一批实体确实共享同一份、允许运行期整体替换的状态”出现时，才值得评估 `FMassSharedFragment`。

### 5.2 FMassChunkFragment

当前也没有项目自定义 ChunkFragment。空间索引由 Authority 状态中的 `TMap<FIntPoint, TArray<int32>> SpatialGrid` 管理，而不是挂在 Mass Chunk 上。

ChunkFragment 适合“每个物理 Chunk 一份”的缓存、统计或粗筛状态。它不等于玩法里的 25 人 Cohort；Mass Chunk 是存储容器，Cohort 是一次选择形成的业务集合，两者不能混用。

## 六、值修改与结构修改不是一回事

下面这些操作只改 Fragment 内的数据，不改变 Archetype：

- Health 100 → 0。
- `bDead=false` → `true`。
- ActiveOrderId 更新。
- Transform、Velocity、SlotTarget 每固定步更新。

死亡流程最后还有一项真实的结构修改：

~~~cpp
EntityManager.RemoveFragmentFromEntity(
    Soldier.Entity,
    FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
~~~

移除 Fragment 会让该实体迁往“不含 ObstacleGridCellLocation”的合适 Archetype。其 `FMassEntityHandle` 和 `FGuLiSoldierId` 没有因此改变，但旧 Chunk 的布局和实体顺序可能变化。

当前代码把结构操作放在 `ApplyDamage` 的末尾，之后不再使用先前取得的 Fragment 引用。这一点很重要：结构迁移后，之前指向 Chunk 数据的引用或 View 不应继续保存和访问。

## 七、从玩家操作到五种元素的真实链路

| 玩家行为/运行阶段 | 写入或读取的数据 | 所属元素 |
|---|---|---|
| 左键圆选 | 读取 Authority `FSoldierRuntime` 中的 SoldierId、Team、Location，并用 SpatialGrid 生成临时 Cohort | 当前不读 Mass Fragment；Cohort 也不是 Mass 元素 |
| 右键移动 | 移动指令受理时立即写 Order、MoveTarget；SlotTarget 在后续权威固定步回写 | 每实体 Fragment |
| Mass Avoidance 阶段 | 产生 Force | 引擎每实体 Fragment |
| Avoidance Capture Processor | Force → AvoidanceOutput，并清空 Force | 每实体 Fragment + ServerAuthority Tag |
| 30Hz 权威固定步 | 合成目标速度、手写分离和引擎避障，写 Transform/Velocity | 每实体 Fragment |
| 可靠状态到客户端 | 以 SoldierId 找到或按需创建本地镜像 Entity，再写 Identity/Health | 每实体 Fragment + ClientMirror Tag |
| 10Hz Pose 到客户端 | 只更新可靠名册中已经存在的 SoldierId；未知 ID 直接丢弃 | 已有镜像的 Transform/表现缓存，不创建权威身份 |
| 统一移动/避障配置 | 500 人共用 Movement/Avoidance 参数 | ConstSharedFragment |

## 八、源码与验证边界

主要源码：

- `Source/GuLiStrike/Commander/Mass/GuLiCommanderMassFragments.h/.cpp`
- `Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp`
- `Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp`
- `Source/GuLiStrike/Commander/Presentation/GuLiCommanderPresentationActor.cpp`

运行证据：

- `Progress/CommanderDynamicPIE-FinalReliable.log` 记录创建 500 个独立服务器权威 Mass Soldier，500/500 投射到 CommanderSoldier NavData。
- 同一日志的 command-flow smoke 记录 `dynamic_members=25`、`moved=1813cm`、`destroyed=25`、`unknown_id=rejected`。
- `Progress/CommanderPIEValidation.json` 记录客户端 UnitInstances=500、RingInstances=500、顶层 `errors=[]`；但其嵌套的 private probe 字段仍有 4 个 `ERROR:Exception` 字符串，不能据此把整份 JSON 概括成“零错误”。

当前 `GuLiCommanderPresentationActor.cpp` 的精确文件版本修改于 01:08，晚于约 01:03 的最后一次成功 smoke；01:09 的 Live Coding 没有新的成功记录。因此本文引用的当前客户端实现属于源码审计事实，不能说它的每一行都已被现有 PIE/Automation 精确覆盖。

边界：现有 18 项 Automation 主要覆盖 Cohort、Navigation 和 Network 合同，没有专门针对这些自定义 Fragment/Tag、ConstShared 组装或结构迁移的单元测试。因此本文只能说“源码实现存在，整体 PIE 流程有运行证据”，不能写成“每一种元素已有专项自动化覆盖”。

## 九、读完后的检查题

1. Soldier 的当前生命值应该放 Fragment 还是 SharedFragment？为什么？
2. ServerAuthorityTag 为什么比“服务器 Entity 有 Force”这条隐含规则更可靠？
3. 500 人相同的 MaxSpeed 为什么适合 ConstShared，而每人的 Velocity 不适合？
4. 25 人 ControlCohort 为什么不是 ChunkFragment？
5. `RemoveFragmentFromEntity` 之后，为什么不能继续持有原 Fragment 引用？

## 关联阅读

- 下一篇：[MassEntityHandle：本地句柄与网络稳定身份](./MassEntityHandle.md)
- 后续：[MassArchetypeTypes：服务器权威体与客户端镜像为何是两套 Archetype](./MassArchetypeTypes.md)
- 后续：[MassEntityQuery 与 ExecutionContext：避障捕获 Processor 的真实执行链](./MassEntityQuery与ExecutionContext.md)
- 技术方案：[20260827-Mass 双端同步架构草案](../20260827-Mass双端同步架构草案.md)
- 总归档：[20260827-Mass 动态 25 人控制组与双端平滑同步](../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)
- 玩法记录：[指挥官](../../Gameplay/指挥官.md)
