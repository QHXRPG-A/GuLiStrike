# 精读笔记：MassEntityHandle.h —— 本地运行时句柄与 SoldierId 的分工

> 源码核对日期：2026-08-31（原笔记始于 2026-08-28）
>
> 引擎基线：Unreal Engine 5.7.4，CL 51494982
>
> 引擎原文件：`C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityHandle.h`
>
>
>
> 项目基线：当前工作区源码，包含公共 Battle 提取及已有未提交修改；历史运行记录与本次静态核对分开列示。
>
> 阅读约定：源码摘录可省略外围代码；概念化定义、假设用法与错误示例明确标作“示意代码”，不表示项目已实现。

[Mass 阅读目录](./README.md) · [UE 网络教材](../UE网络教材/README.md)

## 一句话结论

`FMassEntityHandle` 是某个 `FMassEntityManager` 内定位 Entity 的 8 字节运行时钥匙，不是网络身份、不是指针，也不是永久 ID。

当前的 Commander 实现把这个边界落得很清楚：

- 服务器用 `FMassEntityHandle` 访问本地 Mass Fragment。
- 选择、指令、可靠状态和 10Hz 姿态流使用 `FGuLiSoldierId`。
- 客户端收到 SoldierId 后，创建自己的镜像 Entity，并建立 `SoldierId -> 客户端本地 FMassEntityHandle` 映射。
- 因而双端 Handle 都只是各自本地的访问键，互相没有相等语义；它们的两个整数甚至可能偶然相同，也不能据此认定是同一 Entity。

## 一、引擎句柄只有 Index 和 SerialNumber

UE 5.7.4 的核心定义：

> **示意代码**：按 [FMassEntityHandle](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityHandle.h>) 提炼相关声明，省略其他成员与导出标记。

~~~cpp
USTRUCT()
struct alignas(8) FMassEntityHandle
{
    GENERATED_BODY()

    FMassEntityHandle() = default;
    FMassEntityHandle(const int32 InIndex, const int32 InSerialNumber)
        : Index(InIndex), SerialNumber(InSerialNumber)
    {
    }

    UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
    int32 Index = 0;

    UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
    int32 SerialNumber = 0;

    bool IsSet() const
    {
        return Index != 0 && SerialNumber != 0;
    }

    inline bool IsValid() const
    {
        return IsSet();
    }
};

static_assert(sizeof(FMassEntityHandle) == sizeof(uint64));
static_assert(alignof(FMassEntityHandle) == sizeof(uint64));
~~~

两个字段分别解决两个问题：

| 字段 | 含义 | 不能据此推出什么 |
|---|---|---|
| `Index` | Entity storage 中的槽位号 | 不能推出对象地址、创建时间、网络 ID |
| `SerialNumber` | 该槽位当前分配的代际号 | 不能单独证明 Entity 仍存在 |

实体销毁后，Index 对应的槽位以后可能被复用。新实体会得到新的 SerialNumber，因此旧句柄即便 Index 相同，也不能冒充新实体。

### 最容易踩坑的命名：Handle.IsValid()

头文件自己的注释已经说明：`IsSet()` 只检查两个整数是否非零，无法向 EntityManager 对账。`IsValid()` 在这个结构体上只是 `IsSet()` 的别名。

所以：

> **示意代码**：只演示 Handle 自身的非零检查。

~~~cpp
if (Entity.IsValid())
{
    // 只能说明句柄不是默认的 0/0。
}
~~~

不等于：

> **示意代码**：演示向拥有该实体的 EntityManager 查询；不代表任意 Handle 都安全。

~~~cpp
if (EntityManager.IsEntityValid(Entity))
{
    // EntityManager 确认当前 Index + SerialNumber 仍对应一个有效 Entity。
}
~~~

项目对跨帧、清理等不确定句柄使用第二种；部分内部路径依赖生命周期不变量直接 Checked 访问，不代表可以普遍省略验证。

## 二、当前的服务器 Soldier 同时保存 Handle 与稳定 ID

源码：`Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp`

> **当前源码摘录**：[FSoldierRuntime 的身份、数值和指令字段](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
FMassEntityHandle Entity;
FGuLiSoldierId SoldierId;
EGuLiTeam Team = EGuLiTeam::Unassigned;
FVector Location = FVector::ZeroVector;
FVector Velocity = FVector::ZeroVector;
float FacingYawDegrees = 0.0f;
uint8 Health = 100u;
uint8 MaxHealth = 100u;
float AttackPower = 0.0f;
float Defense = 0.0f;
float AttackRangeCentimeters = 0.0f;
// StateRevision 标识离散状态变化；ActiveOrderId 指向当前批次，0 表示无活动指令。
uint32 StateRevision = 1u;
uint32 ActiveOrderId = 0u;
~~~

公共头文件将它声明为 WorldSubsystem；控制组和编队仍是服务器记录，Handle 不进入网络协议：

> **当前源码摘录**：[UGuLiBattleAuthoritySubsystem 声明起始；余下成员省略](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.h)。

~~~cpp
UCLASS(Config = Game)
class GULISTRIKE_API UGuLiBattleAuthoritySubsystem final : public UTickableWorldSubsystem
{
    GENERATED_BODY()
~~~

这里不是重复存两份“ID”：

- `Entity` 回答：“这名 Soldier 在当前服务器 World 的 MassEntityManager 里去哪取 Fragment？”
- `SoldierId` 回答：“选择、指令、复制和客户端表现说的是哪名 Soldier？”

## 三、为什么网络一定使用 FGuLiSoldierId

源码：`Source/GuLiStrike/Commander/Network/GuLiCommanderTypes.h`

> **当前源码摘录**：[FGuLiSoldierId](D:/UE5.7/test1/Source/GuLiStrike/Commander/Network/GuLiCommanderTypes.h)。

~~~cpp
/** Stable, match-local soldier identity. Zero is invalid and values are never reused in a match. */
USTRUCT()
struct GULISTRIKE_API FGuLiSoldierId
{
    GENERATED_BODY()

    FGuLiSoldierId() = default;
    explicit FGuLiSoldierId(const uint32 InValue)
        : Value(InValue)
    {
    }

    UPROPERTY(EditAnywhere, Category = "Commander|Network")
    uint32 Value = 0u;

    bool IsValid() const { return Value != 0u; }
    void Reset() { Value = 0u; }
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

    friend bool operator==(const FGuLiSoldierId& Lhs, const FGuLiSoldierId& Rhs) { return Lhs.Value == Rhs.Value; }
    friend bool operator!=(const FGuLiSoldierId& Lhs, const FGuLiSoldierId& Rhs) { return !(Lhs == Rhs); }
    friend bool operator<(const FGuLiSoldierId& Lhs, const FGuLiSoldierId& Rhs) { return Lhs.Value < Rhs.Value; }
    friend uint32 GetTypeHash(const FGuLiSoldierId& SoldierId) { return ::GetTypeHash(SoldierId.Value); }
};
~~~

它的网络序列化实现：

> **当前源码摘录**：[FGuLiSoldierId::NetSerialize](D:/UE5.7/test1/Source/GuLiStrike/Commander/Network/GuLiCommanderTypes.cpp)。

~~~cpp
bool FGuLiSoldierId::NetSerialize(
    FArchive& Ar,
    UPackageMap* Map,
    bool& bOutSuccess)
{
    (void)Map;
    Ar.SerializeIntPacked(Value);
    bOutSuccess = !Ar.IsError();
    return true;
}
~~~

项目给 SoldierId 的合同是“本局唯一、0 无效、本局不复用”。它可以安全地出现在：

- SelectionState 与动态 ControlCohort 成员列表。
- 服务器内部 ControlCohort 与 OrderFormation 的成员列表。
- Soldier FastArray 属性状态；保证状态收敛，不逐次通知全部中间变化。
- 不可靠 PoseChunk。
- 客户端表现、选中脚环和 ISM 实例映射。

`FGuLiMoveRequest` 本身只携带目标点、SelectionRevision 和 ClientCommandId；`FGuLiCommandAck` 返回 CohortId 级结果。服务端根据当前已确认 Selection 解析成员，并没有把 SoldierId 数组塞进 Move 请求或 ACK。

`FMassEntityHandle::AsNumber()` 虽然能把两个 int 合成 uint64，但这不把它变成网络稳定身份。另一个 World 或另一个 EntityManager 可以产生相同数值却代表完全不同的实体。

## 四、500 个服务器 Handle 是怎样创建并登记的

源码：`UGuLiBattleAuthoritySubsystem::TrySpawnAuthorityPopulation`

> **当前源码摘录**：[TrySpawnAuthorityPopulation](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
TArray<FMassEntityHandle> EntityHandles;
EntityHandles.Reserve(TotalSoldierCount);

TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager.BatchCreateEntities(
        AuthorityState->AuthorityArchetype,
        SharedValues,
        TotalSoldierCount,
        EntityHandles);

if (EntityHandles.Num() != TotalSoldierCount)
{
    UE_LOG(
        LogGuLiCommanderMass,
        Error,
        TEXT("Expected 500 Soldiers but Mass created %d."),
        EntityHandles.Num());
    EntityManager.BatchDestroyEntities(EntityHandles);
    return false;
}
~~~

代码没有使用 `UMassSpawner`。发布组件启用士兵模拟、世界与专用导航就绪后，Authority 直接创建 Archetype，再批量创建 500 个 Entity；创建子系统本身不保证已经有人口。

随后每个 Handle 被绑定到一个新的 SoldierId：

> **当前源码摘录**：[TrySpawnAuthorityPopulation 的逐兵身份初始化](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
FSoldierRuntime& Soldier = AuthorityState->Soldiers.AddDefaulted_GetRef();
Soldier.Entity = EntityHandles[EntityIndex++];
Soldier.SoldierId = FGuLiSoldierId(AllocateNonZero(AuthorityState->NextSoldierId));
Soldier.Team = Team;
~~~

这里形成两条索引路径：

~~~text
玩法/网络：SoldierId
    -> SoldierIndexById
    -> FSoldierRuntime
    -> FMassEntityHandle
    -> EntityManager
    -> Fragment
~~~

`SoldierIndexById` 是业务注册表，`FMassEntityHandle` 是进入 Mass 存储的最后一跳。

## 五、真实访问模式：先向 EntityManager 对账

固定步遍历中的代码：

> **当前源码摘录**：[TickAuthority](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
if (!EntityManager.IsEntityValid(Soldier.Entity))
{
    continue;
}

FGuLiMassHealthFragment& Health = EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(
        Soldier.Entity);
~~~

这段顺序不能反过来：

1. 注册表中的 Handle 可能因世界结束、回滚或销毁而过期。
2. `EntityManager.IsEntityValid` 同时核对 Index 和 SerialNumber。
3. 只有通过后才调用 `GetFragmentDataChecked`。

`GetFragmentDataChecked<T>` 还会继续确认该 Entity 的当前 Archetype 确实含有 T。Handle 有效不代表它拥有任意 Fragment。

## 六、服务器 Handle 与客户端 Handle 不是同一张证件

客户端头文件保存：

> **当前源码摘录**：[AGuLiCommanderPresentationActor 的镜像成员](D:/UE5.7/test1/Source/GuLiStrike/Commander/Presentation/GuLiCommanderPresentationActor.h)。

~~~cpp
FMassArchetypeHandle ClientMirrorArchetype;
TMap<FGuLiSoldierId, FMassEntityHandle> ClientMirrorEntities;
~~~

客户端收到 Soldier FastArray 状态后，按 SoldierId 查本地 Handle；如果旧 Handle 已失效，就移除映射并创建新的本地 Entity：

> **当前源码摘录**：[EnsureClientMirrorEntity](D:/UE5.7/test1/Source/GuLiStrike/Commander/Presentation/GuLiCommanderPresentationActor.cpp)。

~~~cpp
if (const FMassEntityHandle* Existing = ClientMirrorEntities.Find(ReliableState.SoldierId))
{
    if (EntityManager.IsEntityValid(*Existing))
    {
        return;
    }
    ClientMirrorEntities.Remove(ReliableState.SoldierId);
}

const FMassEntityHandle Entity = EntityManager.CreateEntity(ClientMirrorArchetype);
if (!EntityManager.IsEntityValid(Entity))
{
    return;
}

FGuLiMassIdentityFragment& Identity = EntityManager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(Entity);
Identity.SoldierId = ReliableState.SoldierId;
Identity.Team = ReliableState.Team;
FGuLiMassHealthFragment& Health = EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(Entity);
Health.Health = ReliableState.Health;
Health.bDead = !ReliableState.IsAlive();
Health.WreckSecondsRemaining = 0.0f;
EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity)
    .SetTransform(FTransform::Identity);

ClientMirrorEntities.Add(ReliableState.SoldierId, Entity);
~~~

这里的创建发生在合法名册驱动的表现重建流程；姿态包不能凭未知 SoldierId 创建身份。`UpdateNetworkPresentationSource` 发现连接、PlayerState、Replicator、战局、代次或就绪边沿变化时，清理样本、预测和全部旧镜像，之后再使用新名册重建映射。

完整关系是：

| 层 | 稳定键 | 本地 Mass Handle |
|---|---|---|
| 服务器 Authority World | SoldierId=42 | 例如 Index=137、Serial=501 |
| 客户端 Mirror World | SoldierId=42 | 可能是 Index=19、Serial=44 |
| 第二个客户端 | SoldierId=42 | 又可能是另一组数值 |

只有在相同战局范围内，SoldierId=42 才能跨端表达“同一名 Soldier”；它不是 BattlePlayerState 的玩家 GUID 或席位。在同一个 EntityManager 内，Handle 可以正常复制、比较、哈希并作为 `TMap` 键；禁止的是把它复制到另一个 World/EntityManager 后，再通过 Handle 数值比较来认定同一业务对象。

## 七、Archetype 迁移不会更换 Entity Handle

真实死亡流程：

> **当前源码摘录**：[ApplyDamage 的死亡分支末尾；此前已更新生命和指令](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
EntityManager.RemoveFragmentFromEntity(
    Soldier.Entity,
    FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
~~~

`RemoveFragmentFromEntity` 是结构修改。Mass 会把 Entity 从原 Archetype 搬到一个不含该 Fragment 的 Archetype，但：

- `Soldier.Entity` 仍代表同一 Entity。
- `Soldier.SoldierId` 仍代表同一业务对象。
- 旧 Archetype/Chunk 中的物理位置和数据地址会变化。

后续 30Hz 固定步仍用同一 Handle 读取 Health，并更新 5 秒残骸倒计时：

> **当前源码摘录**：[TickAuthority](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
if (!Soldier.IsAlive())
{
    Health.WreckSecondsRemaining = FMath::Max(
        0.0f,
        static_cast<float>(
            Soldier.DeathSimulationSeconds
            + WreckLifetimeSeconds
            - AuthorityState->SimulationSeconds));

    if (!Soldier.bWreckExpired
        && Health.WreckSecondsRemaining <= 0.0f)
    {
        FTransformFragment& Transform = EntityManager.GetFragmentDataChecked<FTransformFragment>(
                Soldier.Entity);
        FTransform HiddenTransform = Transform.GetTransform();
        HiddenTransform.SetScale3D(FVector::ZeroVector);
        Transform.SetTransform(HiddenTransform);
        Soldier.bWreckExpired = true;
    }
    continue;
}
~~~

因此：Handle 是稳定访问键，Fragment 引用和 Chunk View 才是短生命周期数据地址。结构迁移前取得的 Fragment 引用不能跨迁移继续使用。

## 八、销毁时怎样避免把旧 Handle 当活实体

服务器清理人口：

> **当前源码摘录**：[DestroyAuthorityPopulation 的实体收集与销毁；外层条件见原函数](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
TArray<FMassEntityHandle> Entities;
Entities.Reserve(AuthorityState->Soldiers.Num());
for (const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
{
    if (EntityManager.IsEntityValid(Soldier.Entity))
    {
        Entities.Add(Soldier.Entity);
    }
}
if (!Entities.IsEmpty())
{
    EntityManager.BatchDestroyEntities(Entities);
}
~~~

客户端镜像清理采用同一模式：

1. 遍历 `SoldierId -> Handle`。
2. 只收集 EntityManager 仍认可的 Handle。
3. `BatchDestroyEntities`。
4. Reset 映射、Archetype Handle 和 Subsystem 弱引用。

批量销毁后，外部仍保存的旧 Handle 数值不会自动清零；它们只是再也通不过该 EntityManager 的有效性校验。

## 九、Handle 自身还提供了什么

### 相等与哈希

> **示意代码**：提炼 [相等与哈希](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityHandle.h>) 的相关成员，省略导出宏与中间声明。

~~~cpp
bool operator==(const FMassEntityHandle Other) const
{
    return Index == Other.Index
        && SerialNumber == Other.SerialNumber;
}

friend uint32 GetTypeHash(const FMassEntityHandle Entity)
{
    return HashCombine(Entity.Index, Entity.SerialNumber);
}
~~~

所以 `TMap<FMassEntityHandle, TValue>` 在本地运行时可用，但查到后仍可能需要向 EntityManager 验证。

### 排序

> **引擎源码摘录**：[FMassEntityHandle::operator<](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityHandle.h>)。

~~~cpp
bool operator<(const FMassEntityHandle Other) const
{
    return Index < Other.Index;
}
~~~

它只为排序比较 Index，不表达完整身份，也不保证严格按创建时间排序。相等判断仍比较两个字段。

### AsNumber / FromNumber

它们依赖句柄恰好是对齐的两个 int，可用于匿名传递本地数值。不能把这个 uint64 写进存档或网络协议，并期待在另一个 EntityManager 中恢复同一实体。

## 十、当前项目的使用规则

1. 长期玩法身份用 `FGuLiSoldierId`。
2. `FMassEntityHandle` 只保留在拥有它的 World/EntityManager 一侧。
3. 对来自外部、缓存、跨帧或清理路径的“不确定 Handle”，访问前用 `EntityManager.IsEntityValid`，不要只看 `Handle.IsValid()`。
4. 对生命周期受控、由当前内部状态保证有效的路径，项目也会直接调用 `GetFragmentDataChecked`；这是依赖 Archetype 与生命周期不变量的 checked 合同，违反时会断言，而不是普遍免检规则。
5. 结构修改后不保留旧 Fragment 引用、数组 View 或 Chunk 内位置。
6. 世界结束、士兵模块停用、实体销毁或失败回滚时清理实体和映射；客户端还必须处理同步源失效。
7. 不把 Handle 的 Index、SerialNumber 或 AsNumber 作为 SoldierId、CohortId、OrderId 或网络键。

## 十一、验证边界

**当前源码：** 本次只核对当前项目与本机 UE 5.7.4 源码，没有重新编译、启动 PIE 或执行网络测试。

**历史验证：** [2026-08-27 总归档](../../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)保存了当时 500 兵、动态选兵与移动冒烟的记录；原始临时日志和 JSON 已清理，不能再把它们列成可读取的现存证据。[2026-08-31 公共框架归档](../../../Archive/20260831-公共战局框架与三类角色接入.md)记录冷编译成功、现有测试 50/50 通过及混合战局联调。NetworkGate 最终 ACK P95=138.1ms 达标，但未标记硬跳变 1 次，整体验收仍未通过；本次文档修订没有修复该问题。

**专项边界：** 50 项测试不代表 Handle 代际复用、BatchCreate/Destroy 或跨 Manager 隔离都有专项覆盖；访问规则依靠引擎合同及项目生命周期审查。

## 关联阅读

- 前置：[MassEntityElementTypes：Fragment、Tag 与 ConstShared 的真实使用](./MassEntityElementTypes.md)
- 下一篇：[MassArchetypeTypes：Handle 指向的 Entity 如何按数据组合存放](./MassArchetypeTypes.md)
- 后续：[MassEntityQuery 与 ExecutionContext：如何批量访问匹配 Entity](./MassEntityQuery与ExecutionContext.md)
- 技术方案：[20260827-Mass 双端同步架构草案](../../20260827-Mass双端同步架构草案.md)
- 总归档：[20260827-Mass 动态 25 人控制组与双端平滑同步](../../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)
