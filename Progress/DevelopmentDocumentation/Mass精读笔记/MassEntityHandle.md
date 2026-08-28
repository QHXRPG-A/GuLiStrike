# 精读笔记：MassEntityHandle.h —— 本地运行时句柄与 SoldierId 的分工

> 重写日期：2026-08-28
>
> 引擎基线：Unreal Engine 5.7.4，CL 51494982
>
> 引擎原文件：`C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityHandle.h`
>
> 项目样本：2026-08-27 新增的 Commander 服务器权威实现；客户端 Presentation 源文件在 8 月 28 日 01:08 跨午夜续改。
>
> Git 边界：`Source/GuLiStrike/Commander/` 当前仍未提交；日期归属来自文件时间、归档与日志，不是 Git commit 的逐行差异。

## 一句话结论

`FMassEntityHandle` 是某个 `FMassEntityManager` 内定位 Entity 的 8 字节运行时钥匙，不是网络身份、不是指针，也不是永久 ID。

昨天的 Commander 实现把这个边界落得很清楚：

- 服务器用 `FMassEntityHandle` 访问本地 Mass Fragment。
- 选择、指令、可靠状态和 10Hz 姿态流使用 `FGuLiSoldierId`。
- 客户端收到 SoldierId 后，创建自己的镜像 Entity，并建立 `SoldierId -> 客户端本地 FMassEntityHandle` 映射。
- 因而双端 Handle 都只是各自本地的访问键，互相没有相等语义；它们的两个整数甚至可能偶然相同，也不能据此认定是同一 Entity。

## 一、引擎句柄只有 Index 和 SerialNumber

UE 5.7.4 的核心定义：

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

~~~cpp
if (Entity.IsValid())
{
    // 只能说明句柄不是默认的 0/0。
}
~~~

不等于：

~~~cpp
if (EntityManager.IsEntityValid(Entity))
{
    // EntityManager 确认当前 Index + SerialNumber 仍对应一个有效 Entity。
}
~~~

昨天的项目代码使用的是第二种。

## 二、昨天的服务器 Soldier 同时保存 Handle 与稳定 ID

源码：`Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp`

~~~cpp
struct FSoldierRuntime
{
    FMassEntityHandle Entity;
    FGuLiSoldierId SoldierId;
    EGuLiTeam Team = EGuLiTeam::Unassigned;
    FVector Location = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    float FacingYawDegrees = 0.0f;
    uint8 Health = 100u;
    uint32 StateRevision = 1u;
    uint32 ActiveOrderId = 0u;
    // ...
};
~~~

公共头文件还直接写明边界：

~~~cpp
/**
 * Server-only authority for 500 independently identified Mass Soldiers.
 *
 * ControlCohorts and OrderFormations are transient server records. FMassEntityHandle
 * remains private to this subsystem and never crosses the network contract boundary.
 */
UCLASS(Config = Game)
class GULISTRIKE_API UGuLiBattleAuthoritySubsystem final
    : public UTickableWorldSubsystem
{
    // ...
};
~~~

这里不是重复存两份“ID”：

- `Entity` 回答：“这名 Soldier 在当前服务器 World 的 MassEntityManager 里去哪取 Fragment？”
- `SoldierId` 回答：“选择、指令、复制和客户端表现说的是哪名 Soldier？”

## 三、为什么网络一定使用 FGuLiSoldierId

源码：`Source/GuLiStrike/Commander/Network/GuLiCommanderTypes.h`

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
- 可靠 Soldier FastArray。
- 不可靠 PoseChunk。
- 客户端表现、选中脚环和 ISM 实例映射。

`FGuLiMoveRequest` 本身只携带目标点、SelectionRevision 和 ClientCommandId；`FGuLiCommandAck` 返回 CohortId 级结果。服务端根据当前已确认 Selection 解析成员，并没有把 SoldierId 数组塞进 Move 请求或 ACK。

`FMassEntityHandle::AsNumber()` 虽然能把两个 int 合成 uint64，但这不把它变成网络稳定身份。另一个 World 或另一个 EntityManager 可以产生相同数值却代表完全不同的实体。

## 四、500 个服务器 Handle 是怎样创建并登记的

源码：`UGuLiBattleAuthoritySubsystem::TrySpawnAuthorityPopulation`

~~~cpp
TArray<FMassEntityHandle> EntityHandles;
EntityHandles.Reserve(TotalSoldierCount);

TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext =
    EntityManager.BatchCreateEntities(
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

代码没有使用 `UMassSpawner`。它直接创建 Archetype，再一次批量创建 500 个 Entity。

随后每个 Handle 被绑定到一个新的 SoldierId：

~~~cpp
FSoldierRuntime& Soldier = AuthorityState->Soldiers.AddDefaulted_GetRef();
Soldier.Entity = EntityHandles[EntityIndex++];
Soldier.SoldierId =
    FGuLiSoldierId(AllocateNonZero(AuthorityState->NextSoldierId));
Soldier.Team = Team;

const int32 SoldierIndex = AuthorityState->Soldiers.Num() - 1;
AuthorityState->SoldierIndexById.Add(
    Soldier.SoldierId.Value,
    SoldierIndex);
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

~~~cpp
FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
if (!EntityManager.IsEntityValid(Soldier.Entity))
{
    continue;
}

FGuLiMassHealthFragment& Health =
    EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(
        Soldier.Entity);
~~~

这段顺序不能反过来：

1. 注册表中的 Handle 可能因世界结束、回滚或销毁而过期。
2. `EntityManager.IsEntityValid` 同时核对 Index 和 SerialNumber。
3. 只有通过后才调用 `GetFragmentDataChecked`。

`GetFragmentDataChecked<T>` 还会继续确认该 Entity 的当前 Archetype 确实含有 T。Handle 有效不代表它拥有任意 Fragment。

## 六、服务器 Handle 与客户端 Handle 不是同一张证件

客户端头文件保存：

~~~cpp
FMassArchetypeHandle ClientMirrorArchetype;
TMap<FGuLiSoldierId, FMassEntityHandle> ClientMirrorEntities;
~~~

客户端收到可靠 Soldier 状态后，按 SoldierId 查本地 Handle；如果旧 Handle 已失效，就移除映射并创建新的本地 Entity：

~~~cpp
if (const FMassEntityHandle* Existing =
        ClientMirrorEntities.Find(ReliableState.SoldierId))
{
    if (EntityManager.IsEntityValid(*Existing))
    {
        return;
    }
    ClientMirrorEntities.Remove(ReliableState.SoldierId);
}

const FMassEntityHandle Entity =
    EntityManager.CreateEntity(ClientMirrorArchetype);
if (!EntityManager.IsEntityValid(Entity))
{
    return;
}

FGuLiMassIdentityFragment& Identity =
    EntityManager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(Entity);
Identity.SoldierId = ReliableState.SoldierId;
Identity.Team = ReliableState.Team;
FGuLiMassHealthFragment& Health =
    EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(Entity);
Health.Health = ReliableState.Health;
Health.bDead = !ReliableState.IsAlive();
Health.WreckSecondsRemaining = 0.0f;
EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity)
    .SetTransform(FTransform::Identity);

ClientMirrorEntities.Add(ReliableState.SoldierId, Entity);
~~~

这段 Presentation 源文件在 8 月 27 日创建、8 月 28 日 01:08 续改；因为目录尚未提交，无法恢复逐行归属，本文按昨夜开发后的当前实现解释。

完整关系是：

| 层 | 稳定键 | 本地 Mass Handle |
|---|---|---|
| 服务器 Authority World | SoldierId=42 | 例如 Index=137、Serial=501 |
| 客户端 Mirror World | SoldierId=42 | 可能是 Index=19、Serial=44 |
| 第二个客户端 | SoldierId=42 | 又可能是另一组数值 |

只有 SoldierId=42 能跨端表达“同一名 Soldier”。在同一个 EntityManager 内，Handle 可以正常复制、比较、哈希并作为 `TMap` 键；禁止的是把它复制到另一个 World/EntityManager 后，再通过 Handle 数值比较来认定同一业务对象。

## 七、Archetype 迁移不会更换 Entity Handle

真实死亡流程：

~~~cpp
FGuLiMassHealthFragment& Health =
    EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(
        Soldier.Entity);
Health.Health = Soldier.Health;
Health.bDead = Soldier.Health == 0u;

if (Soldier.Health == 0u)
{
    // 停止当前指令与速度……
    EntityManager.RemoveFragmentFromEntity(
        Soldier.Entity,
        FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
}
~~~

`RemoveFragmentFromEntity` 是结构修改。Mass 会把 Entity 从原 Archetype 搬到一个不含该 Fragment 的 Archetype，但：

- `Soldier.Entity` 仍代表同一 Entity。
- `Soldier.SoldierId` 仍代表同一业务对象。
- 旧 Archetype/Chunk 中的物理位置和数据地址会变化。

后续 30Hz 固定步仍用同一 Handle 读取 Health，并更新 5 秒残骸倒计时：

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
        FTransformFragment& Transform =
            EntityManager.GetFragmentDataChecked<FTransformFragment>(
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

~~~cpp
TArray<FMassEntityHandle> Entities;
Entities.Reserve(AuthorityState->Soldiers.Num());

for (const FSoldierRuntime& Soldier : AuthorityState->Soldiers)
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

AuthorityState->Soldiers.Reset();
AuthorityState->SoldierIndexById.Reset();
~~~

客户端镜像清理采用同一模式：

1. 遍历 `SoldierId -> Handle`。
2. 只收集 EntityManager 仍认可的 Handle。
3. `BatchDestroyEntities`。
4. Reset 映射、Archetype Handle 和 Subsystem 弱引用。

批量销毁后，外部仍保存的旧 Handle 数值不会自动清零；它们只是再也通不过该 EntityManager 的有效性校验。

## 九、Handle 自身还提供了什么

### 相等与哈希

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
6. 世界结束、实体销毁、失败回滚时批量销毁并清空业务映射。
7. 不把 Handle 的 Index、SerialNumber 或 AsNumber 作为 SoldierId、CohortId、OrderId 或网络键。

## 十一、验证边界

`CommanderDynamicPIE-FinalReliable.log` 证明当前流程曾创建 500 个服务器权威 Mass Soldier，并通过动态 25 人选择、移动、摧毁与未知 SoldierId 拒绝 smoke。`CommanderPIEValidation.json` 记录客户端 500 个 Unit/Ring 实例。

当前 `GuLiCommanderPresentationActor.cpp` 在 01:08 续改，晚于约 01:03 的最后一次成功 smoke；随后 01:09 的 Live Coding 没有新的成功记录。因此客户端 Handle 映射的当前精确代码属于源码审计事实，不应表述成已被现有运行证据逐行覆盖。

但现有 Automation 没有为 Handle 代际复用、BatchCreate/Destroy 或服务器/客户端 Handle 隔离建立专项测试。上述规则一部分来自 UE 5.7.4 引擎合同，一部分来自项目源码审计；不能把它们写成“已有 Handle 专项自动化验证”。

## 关联阅读

- 前置：[MassEntityElementTypes：Fragment、Tag 与 ConstShared 的真实使用](./MassEntityElementTypes.md)
- 下一篇：[MassArchetypeTypes：Handle 指向的 Entity 如何按数据组合存放](./MassArchetypeTypes.md)
- 后续：[MassEntityQuery 与 ExecutionContext：如何批量访问匹配 Entity](./MassEntityQuery与ExecutionContext.md)
- 技术方案：[20260827-Mass 双端同步架构草案](../20260827-Mass双端同步架构草案.md)
- 总归档：[20260827-Mass 动态 25 人控制组与双端平滑同步](../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)
