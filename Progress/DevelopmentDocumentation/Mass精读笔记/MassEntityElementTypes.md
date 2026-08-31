# 精读笔记：MassEntityElementTypes.h —— 用 Commander 500 人实现理解五种 Mass 元素

> 源码核对日期：2026-08-31（原笔记始于 2026-08-28）
>
> 引擎基线：Unreal Engine 5.7.4，CL 51494982
>
> 引擎原文件：`C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityElementTypes.h`
>
>
>
>
> 项目基线：当前工作区源码，包含公共 Battle 提取及已有未提交修改；历史运行记录与本次静态核对分开列示。
>
> 阅读约定：源码摘录可省略外围代码；概念化定义、假设用法与错误示例明确标作“示意代码”，不表示项目已实现。

[Mass 阅读目录](./README.md) · [UE 网络教材](../UE网络教材/README.md)

## 先说结论

`MassEntityElementTypes.h` 只定义五个几乎没有实现代码的基类，但它们决定了数据的**拥有粒度**：

| 基类 | 数据粒度 | Commander 当前实例 | 当前是否使用 |
|---|---|---|---|
| `FMassFragment` | 每个 Entity 一份 | Identity、Health、SoldierStats、Order、SlotTarget、AvoidanceOutput，以及引擎 Transform/Velocity/Force 等 | 是 |
| `FMassTag` | 只表达组成中的有/无，不携带每实体 payload | ServerAuthority、ClientSnapshotMirror、RuntimeTuningEven/Odd | 是 |
| `FMassChunkFragment` | 每个 Mass Chunk 一份 | 无 | 否 |
| `FMassSharedFragment` | 一组实体共享一份、允许修改 | 无 | 否 |
| `FMassConstSharedFragment` | 一组实体共享一份、查询侧只读 | MovementParameters、MovingAvoidanceParameters | 是，使用引擎内建类型 |

当前的实现没有自定义 `FMassChunkFragment` 或可变 `FMassSharedFragment`。本文不会为它们编造项目案例；只解释它们与现有代码的边界。

## 一、引擎头文件究竟定义了什么

UE 5.7.4 的核心定义可以压缩为：

> **示意代码**：仅提炼五类基类，省略引擎导出、弃用与辅助声明；参见 [MassEntityElementTypes.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityElementTypes.h>)。

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

### 2.1 当前新增的真实 Fragment

源码：`Source/GuLiStrike/Commander/Mass/GuLiCommanderMassFragments.h`

> **当前源码摘录**：[Identity / Health / SoldierStats / Order 声明](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderMassFragments.h)。

~~~cpp
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

/** Authoritative data-bearing stats. Combat systems do not consume these yet. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassSoldierStatsFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    uint8 MaxHealth = 100u;

    UPROPERTY(Transient)
    float AttackPower = 0.0f;

    UPROPERTY(Transient)
    float Defense = 0.0f;

    UPROPERTY(Transient)
    float AttackRangeCentimeters = 0.0f;
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
- `FGuLiMassHealthFragment` 保存生命、死亡标志及残骸剩余秒数；默认五秒的计时由 Authority 与 Presentation 管理，Fragment 自身不执行计时。当前没有本轮残骸时长专项实测。
- `FGuLiMassSoldierStatsFragment` 保存 MaxHealth、AttackPower、Defense 和 AttackRangeCentimeters。Health 是当前生命，Stats.MaxHealth 是上限；攻击、防御和射程目前没有正式战斗消费者。
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

> **当前源码摘录**：[IssueMove 的成功批次提交段](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
Soldier.ActiveOrderId = BatchOrderId;
++Soldier.StateRevision;
FGuLiMassOrderFragment& Order = EntityManager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
Order.ActiveOrderId = BatchOrderId;
Order.OrderRevision = Soldier.StateRevision;
Order.FormationTarget = Formation.TargetAnchor;
Order.bHasMoveTarget = true;
FMassMoveTargetFragment& MoveTarget = EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
MoveTarget.CreateNewAction(EMassMovementAction::Move, *GetWorld());
MoveTarget.IntentAtGoal = EMassMovementAction::Stand;
MoveTarget.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);
~~~

这段代码对应玩家的一次右键移动：

1. CommanderNetSync 先检查公共与士兵就绪、Commander 权限；Authority 再检查选择版本、队伍、存活、路径及终点。
2. 服务端把新的 BatchOrderId 写入自己的 Soldier 注册表。
3. 再用该 Soldier 的 `FMassEntityHandle` 找到 `OrderFragment` 和引擎 `MoveTargetFragment`。
4. Order 保存业务事实；MoveTarget 提供给 Mass 相关处理器，实际位置积分仍由 Authority 完成。

Fragment 是数据，不是行为类。真正决定“什么时候写、谁有权写”的，是服务端子系统和 Processor。

### 2.3 每个固定步如何回写可见状态

源码：`UGuLiBattleAuthoritySubsystem::TickAuthority`

> **当前源码摘录**：[TickAuthority](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
FTransformFragment& Transform = EntityManager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);
Transform.SetTransform(FTransform(
    FRotator(0.0f, Soldier.FacingYawDegrees, 0.0f),
    Soldier.Location));

EntityManager
    .GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity)
    .Value = Soldier.Velocity;

FGuLiMassOrderFragment& Order = EntityManager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
Order.ActiveOrderId = Soldier.ActiveOrderId;
Order.OrderRevision = Soldier.StateRevision;
Order.bHasMoveTarget = Soldier.ActiveOrderId != 0u;
~~~

这解释了为什么每个实体需要自己的 Fragment：500 名 Soldier 的位置、速度、朝向和指令可以不同，不能把它们塞进 SharedFragment。

## 三、FMassTag：把服务端权威体和客户端镜像彻底隔开

### 3.1 权威、镜像与调参标签

源码：`GuLiCommanderMassFragments.h`

> **当前源码摘录**：[ClientSnapshotMirror / ServerAuthority Tag](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderMassFragments.h)。

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

> **当前源码摘录**：[ConfigureQueries](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
EntityQuery.AddRequirement<FMassForceFragment>(EMassFragmentAccess::ReadWrite);
EntityQuery.AddRequirement<FGuLiMassAvoidanceOutputFragment>(EMassFragmentAccess::ReadWrite);
EntityQuery.AddTagRequirement<FGuLiServerAuthorityMassTag>(EMassFragmentPresence::All);
~~~

客户端镜像 Archetype 只有：

> **当前源码摘录**：[EnsureClientMirrorArchetype](D:/UE5.7/test1/Source/GuLiStrike/Commander/Presentation/GuLiCommanderPresentationActor.cpp)。

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

源码注释中的“exact 500-member server archetype”描述出生设计；死亡或调参迁移后 ServerAuthorityTag 仍保留，实际表达 Authority 处理域，而非某个永久 Archetype Handle。

同文件还定义 `FGuLiMassRuntimeTuningEvenTag` / `OddTag`。初始组成带 Even，提交新速度时交替切换标签，使新的 ConstShared 参数随实体迁移绑定。它们不表示队伍、玩家角色或网络连接代次。客户端镜像不带这些调参标签。

## 四、FMassConstSharedFragment：500 人共享移动与避障参数

项目没有自定义 ConstShared 类型，但真实使用了两个引擎类型：

- `FMassMovementParameters : FMassConstSharedFragment`
- `FMassMovingAvoidanceParameters : FMassConstSharedFragment`

源码：`UGuLiBattleAuthoritySubsystem::TrySpawnAuthorityPopulation`

> **当前源码摘录**：[GuLiCommanderMassPrivate::MakeAuthoritySharedFragmentValues](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
FMassMovementParameters MovementParameters;
MovementParameters.MaxSpeed = MovementSpeedCentimetersPerSecond;
MovementParameters.DefaultDesiredSpeed = MovementSpeedCentimetersPerSecond;
MovementParameters.DefaultDesiredSpeedVariance = 0.0f;
MovementParameters.MaxAcceleration = MovementSpeedCentimetersPerSecond * 4.0f;
MovementParameters.bIsCodeDrivenMovement = true;
MovementParameters.Update();

FMassMovingAvoidanceParameters AvoidanceParameters;
AvoidanceParameters.ObstacleDetectionDistance = AgentRadiusCentimeters * 8.0f;
AvoidanceParameters.SeparationRadiusScale = 0.95f;
AvoidanceParameters.ObstacleSeparationDistance = AgentRadiusCentimeters * 0.35f;
AvoidanceParameters.PredictiveAvoidanceDistance = AgentRadiusCentimeters * 0.35f;

FMassArchetypeSharedFragmentValues SharedValues;
SharedValues.Add(EntityManager.GetOrCreateConstSharedFragment(
    MovementParameters.GetValidated()));
SharedValues.Add(EntityManager.GetOrCreateConstSharedFragment(
    AvoidanceParameters.GetValidated()));
SharedValues.Sort();
return SharedValues;
~~~

为什么这两个参数适合 ConstShared：

- 500 名服务器 Soldier 使用同一套最大速度、加速度和避障尺度。
- 它们是配置，不是某名 Soldier 的瞬时状态。
- Query/Processor 只应读取，不应逐实体改写。
- 共享一份可以避免把相同配置复制 500 次。

`FMassArchetypeSharedFragmentValues` 是“共享值容器”，不是某一种 SharedFragment。它同时装可变 Shared 和 ConstShared，并维护各自类型位集。

还有一个容易写错的点：**共享值本身不是 Archetype 身份的一部分**。Archetype 的组成描述记录 Shared/ConstShared 的类型位集；具体值随创建请求传入，并用于 Chunk 的共享值分组和绑定。当前的代码也正是先用类型位集取得合适 Archetype，再把 `SharedValues` 传给批量创建。

## 五、当前没有使用的两种元素

### 5.1 FMassSharedFragment

当前指挥官士兵实现没有自定义可变 SharedFragment；这里不对引擎或第三方插件作全局断言。

服务器的临时编队关系放在：

> **当前源码摘录**：[FOrderFormationRuntime 的部分字段](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
uint32 FormationId = 0u;
uint32 BatchOrderId = 0u;
FGuLiControlCohortId SourceCohortId;
EGuLiTeam Team = EGuLiTeam::Unassigned;
TArray<FGuLiSoldierId> MemberIds;
TMap<uint32, uint8> SlotBySoldierId;
// GuideAnchor 是沿共享路径推进的虚拟领队；TargetAnchor 是本批移动的共同终点。
FVector GuideAnchor = FVector::ZeroVector;
FVector TargetAnchor = FVector::ZeroVector;
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

> **当前源码摘录**：[ApplyDamage](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
EntityManager.RemoveFragmentFromEntity(
    Soldier.Entity,
    FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
~~~

移除 Fragment 会让该实体迁往“不含 ObstacleGridCellLocation”的合适 Archetype。其 `FMassEntityHandle` 和 `FGuLiSoldierId` 没有因此改变，但旧 Chunk 的布局和实体顺序可能变化。

当前代码把死亡结构操作放在 `ApplyDamage` 末尾，之后不再使用旧 Fragment 引用。`ApplyPendingMovementSpeed` 则在固定步边界通过 Even/Odd 组成迁移更新共享速度，再重新取得所需 Fragment。两条路径都不能跨迁移保存旧引用或 View；共享值改变本身不定义新类型，这里是项目主动切换 Tag 的实现策略。

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
| 统一移动/避障配置 | 全体士兵共用 Movement/Avoidance 参数；新速度提交时 Even/Odd 迁移替换 | ConstSharedFragment + 调参 Tag |

## 八、源码与验证边界

**当前源码：** 本次只核对当前项目与本机 UE 5.7.4 源码，没有重新编译、启动 PIE 或执行网络测试。

**历史验证：** [2026-08-27 总归档](../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)保存了当时 500 兵、动态选兵与移动冒烟的记录；原始临时日志和 JSON 已清理，不能再把它们列成可读取的现存证据。[2026-08-31 公共框架归档](../../Archive/20260831-公共战局框架与三类角色接入.md)记录冷编译成功、现有测试 50/50 通过及混合战局联调。NetworkGate 最终 ACK P95=138.1ms 达标，但未标记硬跳变 1 次，整体验收仍未通过；本次文档修订没有修复该问题。

**专项边界：** 这些整体结果不等于自定义 Fragment/Tag、ConstShared 绑定、迁移安全性或客户端五秒残骸均有专项测试；本文对这些细节的依据是源码。

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
