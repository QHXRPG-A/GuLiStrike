# 精读笔记：MassArchetypeTypes.h —— 从服务器权威体与客户端镜像理解 Archetype

> 源码核对日期：2026-08-31（原笔记始于 2026-08-28）
>
> 引擎基线：Unreal Engine 5.7.4，CL 51494982
>
> 引擎原文件：`C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassArchetypeTypes.h`
>
> 关联定义：`MassEntityTypes.h` 中的 `FMassArchetypeCompositionDescriptor`、`FMassArchetypeSharedFragmentValues`、`FMassArchetypeCreationParams`
>
>
>
> 项目基线：当前工作区源码，包含公共 Battle 提取及已有未提交修改；历史运行记录与本次静态核对分开列示。
>
> 阅读约定：源码摘录可省略外围代码；概念化定义、假设用法与错误示例明确标作“示意代码”，不表示项目已实现。

[Mass 阅读目录](./README.md) · [UE 网络教材](../UE网络教材/README.md)

## 一句话结论

Archetype 不是兵种类、不是 25 人小队、不是队伍，也不是某个 Entity。

它是“拥有完全相同 Mass 元素类型的一批 Entity”的存储与处理合同。当前的代码给出了最清楚的对照：

- 服务器出生组成有 13 个 Fragment（含 SoldierStats），加 ServerAuthorityTag、RuntimeTuningEvenTag 及两种 ConstShared 类型；后续可切换为 Odd。
- 客户端镜像 Soldier 只需要 Transform、Identity、Health 和 ClientMirrorTag。
- 同一名业务 Soldier 在双端因此属于两套不同 Archetype，并由同一个 SoldierId 关联。

## 一、先把四个概念分开

| 概念 | Commander 中的实例 | 它回答的问题 |
|---|---|---|
| Entity | 一名独立 Soldier 的本地 `FMassEntityHandle` | “具体是哪一个运行时实体？” |
| Fragment/Tag | Health、Order、Transform、ServerAuthorityTag | “这个实体拥有哪些数据列/标记？” |
| Archetype | ServerAuthority500DynamicSoldiers、ClientSnapshotMirrorSoldiers | “这一批实体的数据类型组合是什么？” |
| Chunk | Archetype 内连续存储若干 Entity 的物理块 | “这些列在内存中按什么批次排列？” |

再加两个项目业务概念：

| 业务概念 | 本质 | 为什么不是 Archetype/Chunk |
|---|---|---|
| ControlCohort | 一次选择后冻结的 1–25 个 SoldierId | 成员会随下一次选择重新组合；它表达控制语义 |
| OrderFormation | 一次已接受指令的 SoldierId、路径与槽位运行时记录 | 生命周期属于该指令；不是数据列组合 |

“25 人”只是一项玩法容量合同。Mass Chunk 的容量由数据布局和 ChunkMemorySize 决定，两者没有一一对应关系。

## 二、Archetype 的组成由五类类型位集描述

UE 5.7.4 的 `FMassArchetypeCompositionDescriptor` 持有：

> **示意代码**：提炼五类类型位集，省略 UPROPERTY 和弃用声明；参见 [MassEntityTypes.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityTypes.h>)。

~~~cpp
struct FMassArchetypeCompositionDescriptor
{
    FMassFragmentBitSet Fragments;
    FMassTagBitSet Tags;
    FMassChunkFragmentBitSet ChunkFragments;
    FMassSharedFragmentBitSet SharedFragments;
    FMassConstSharedFragmentBitSet ConstSharedFragments;
};
~~~

5.7 已把直接访问成员标为 deprecated，外部代码应使用：

> **示意代码**：描述符访问方式，非本项目的一组连续调用；参见 [MassEntityTypes.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityTypes.h>)。

~~~cpp
Descriptor.GetFragments();
Descriptor.GetTags();
Descriptor.GetChunkFragments();
Descriptor.GetSharedFragments();
Descriptor.GetConstSharedFragments();
~~~

描述符提供 `HasAll`、`Append`、`Remove`、`CalculateDifference` 和 Hash 等操作，本质上都在比较**类型集合**。

因此：

- 红队与蓝队的 Team 字段值不同，但类型集合相同，可以位于同一 Archetype。
- Health=100 与 Health=0 不会拆成两个 Archetype。
- ActiveOrderId 不同不会拆 Archetype。
- 增加/移除一个 Fragment 或 Tag 才是组成变化。
- Shared/ConstShared 的**类型**进入组成描述；具体共享值不成为新的元素类型。

## 三、FMassArchetypeHandle 是不透明的 Archetype 引用

引擎定义：

> **示意代码**：提炼 Handle 成员，省略引擎导出宏及其他函数；参见 [MassArchetypeTypes.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassArchetypeTypes.h>)。

~~~cpp
/** An opaque handle to an archetype */
struct FMassArchetypeHandle final
{
    FMassArchetypeHandle() = default;
    bool IsValid() const;
    void Reset();

private:
    FMassArchetypeHandle(
        const TSharedPtr<FMassArchetypeData>& InDataPtr);

    TSharedPtr<FMassArchetypeData> DataPtr;
};
~~~

内联实现的核心是：

> **引擎源码摘录**：[FMassArchetypeHandle::IsValid](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassArchetypeTypes.h>)。

~~~cpp
inline bool FMassArchetypeHandle::IsValid() const
{
    return DataPtr.IsValid();
}
~~~

与 `FMassEntityHandle` 不同：

- Entity Handle 是 Index + SerialNumber。
- Archetype Handle 内部持有不透明的 `FMassArchetypeData` 共享指针。
- 项目代码不能直接依赖 Archetype 内部数据布局。
- 两个 Archetype Handle 相等，意味着它们持有同一 `DataPtr`。

当前服务器把最终出生 Archetype Handle 存在：

> **当前源码摘录**：[FGuLiBattleAuthorityState 的 Archetype 与士兵容器字段](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
TWeakObjectPtr<UMassEntitySubsystem> MassEntitySubsystem;
FMassArchetypeHandle AuthorityArchetype;
FMassArchetypeHandle RuntimeTuningEvenBaseArchetype;
FMassArchetypeHandle RuntimeTuningOddBaseArchetype;
TArray<GuLiCommanderMassPrivate::FSoldierRuntime> Soldiers;
~~~

客户端则保存另一份：

> **当前源码摘录**：[AGuLiCommanderPresentationActor 的镜像成员](D:/UE5.7/test1/Source/GuLiStrike/Commander/Presentation/GuLiCommanderPresentationActor.h)。

~~~cpp
FMassArchetypeHandle ClientMirrorArchetype;
TMap<FGuLiSoldierId, FMassEntityHandle> ClientMirrorEntities;
~~~

远端客户端通常拥有不同 World/EntityManager；Listen Server 或 Standalone 的 Authority 与本地镜像则可能共处同一 Manager。无论是否同 Manager，它们都是两套独立 Entity/Archetype Handle，组成不同，也不能拿 Handle 数值表达跨端身份。

## 四、真实案例一：创建服务器 500 人出生 Archetype

源码：`UGuLiBattleAuthoritySubsystem::TrySpawnAuthorityPopulation`

### 4.1 先列出普通 Fragment 与 Tag 类型

> **当前源码摘录**：[TrySpawnAuthorityPopulation 的出生组成](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
TArray<const UScriptStruct*> FragmentAndTagTypes = {
    FTransformFragment::StaticStruct(),
    FAgentRadiusFragment::StaticStruct(),
    FMassVelocityFragment::StaticStruct(),
    FMassForceFragment::StaticStruct(),
    FMassMoveTargetFragment::StaticStruct(),
    FMassNavigationEdgesFragment::StaticStruct(),
    FMassNavigationObstacleGridCellLocationFragment::StaticStruct(),
    FGuLiMassIdentityFragment::StaticStruct(),
    FGuLiMassHealthFragment::StaticStruct(),
    FGuLiMassSoldierStatsFragment::StaticStruct(),
    FGuLiMassOrderFragment::StaticStruct(),
    FGuLiMassSlotTargetFragment::StaticStruct(),
    FGuLiMassAvoidanceOutputFragment::StaticStruct(),
    FGuLiServerAuthorityMassTag::StaticStruct(),
    FGuLiMassRuntimeTuningEvenTag::StaticStruct()
};
~~~

这是 **13 个 Fragment + 2 个 Tag**；出生时使用 Even，Odd 是预建的另一种调参组成。客户端仍是 3 个 Fragment + 1 个镜像 Tag，没有 SoldierStats 列；UI 所需 MaxHealth 来自复制名册。

每一列都有明确职责：

| 类型组 | 类型 | 服务器用途 |
|---|---|---|
| 空间 | Transform、Velocity、AgentRadius | 位置、朝向、速度与个体半径 |
| 移动 | Force、MoveTarget | 引擎移动与避障输出 |
| 导航 | NavigationEdges、ObstacleGridCellLocation | CommanderSoldier NavData 与障碍网格参与 |
| 玩法 | Identity、Health、SoldierStats、Order、SlotTarget | 独立身份、当前生命、兵种数值、指令和弹性槽位 |
| 桥接 | AvoidanceOutput | 世界帧 Avoidance → 30Hz 权威固定步 |
| 标签 | ServerAuthorityTag、RuntimeTuningEven/Odd | 前者限定 Capture Query 处理域，后者使调参交替迁移；服务器执行域另由 ExecutionFlags 限定 |

准确类型是 `FTransformFragment`，不是 `FMassTransformFragment`。

### 4.2 用 DebugName 创建基础 Archetype

> **当前源码摘录**：[TrySpawnAuthorityPopulation](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
FMassArchetypeCreationParams ArchetypeParams;
ArchetypeParams.DebugName = TEXT("GuLiServerAuthority500DynamicSoldiers");

const FMassArchetypeHandle BaseAuthorityArchetype = EntityManager.CreateArchetype(
        FragmentAndTagTypes,
        ArchetypeParams);

if (!BaseAuthorityArchetype.IsValid())
{
    UE_LOG(
        LogGuLiCommanderMass,
        Error,
        TEXT("Failed to create Soldier authority archetype."));
    return false;
}
~~~

`DebugName` 是调试标签，不是组成身份。即便名称相同，仍应以 EntityManager 返回的 Handle 和组成合同为准。

`FMassArchetypeCreationParams` 还可指定 `ChunkMemorySize`；当前项目保持 0，使用引擎默认 Chunk 大小。同函数随后将类型数组中的 Even 替换为 Odd，再创建 `OddBaseAuthorityArchetype`；不是按队伍分别创建。

### 4.3 把两种 ConstShared 类型加入组成

`MakeAuthoritySharedFragmentValues` 构造并校验 Movement、MovingAvoidance 两份 ConstShared 值，再 Sort；出生函数保存两套基础组成并选择 Even：

> **当前源码摘录**：[TrySpawnAuthorityPopulation 保存双基础组成并绑定 ConstShared 类型](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
AuthorityState->RuntimeTuningEvenBaseArchetype = BaseAuthorityArchetype;
AuthorityState->RuntimeTuningOddBaseArchetype = OddBaseAuthorityArchetype;
AuthorityState->bUsingRuntimeTuningEvenArchetype = true;
FMassArchetypeSharedFragmentValues SharedValues = MakeAuthoritySharedFragmentValues(
    EntityManager,
    MovementSpeedCentimetersPerSecond,
    MemberAgentRadiusCentimeters);
AuthorityState->AuthorityArchetype = EntityManager.GetOrCreateSuitableArchetype(
    BaseAuthorityArchetype,
    SharedValues.GetSharedFragmentBitSet(),
    SharedValues.GetConstSharedFragmentBitSet());
~~~

这段容易被误读，拆成四步：

1. `BaseAuthorityArchetype` 已有 13 Fragment + ServerAuthority、Even 两个 Tag；同函数还预建 Odd 基础组成。
2. `SharedValues` 装入 Movement 与 MovingAvoidance 两个 ConstShared 实例。
3. 两个 BitSet 只表达“需要哪些 Shared/ConstShared 类型”。
4. `GetOrCreateSuitableArchetype` 返回包含这些类型的最终组成。

项目在取得类型位集和批量创建前显式调用 `Sort()`，把 Shared/ConstShared 容器规范化为稳定顺序，便于后续比较、哈希与绑定；不应让调用方依赖加入顺序。

### 4.4 用最终 Archetype 和具体共享值批量创建

> **当前源码摘录**：[TrySpawnAuthorityPopulation](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
TArray<FMassEntityHandle> EntityHandles;
EntityHandles.Reserve(TotalSoldierCount);

TSharedRef<FMassEntityManager::FEntityCreationContext>
    CreationContext = EntityManager.BatchCreateEntities(
            AuthorityState->AuthorityArchetype,
            SharedValues,
            TotalSoldierCount,
            EntityHandles);
~~~

这里同时传入：

- 最终 Archetype：列类型合同。
- `SharedValues`：这批实体所在 Chunk 应绑定的具体共享配置。
- `TotalSoldierCount`：500。
- 输出 Handle 数组。

不同共享参数值可以让 Chunk 绑定不同共享值集合，但不会凭空创造一种新的 Fragment 类型。Archetype 的组成仍由五类类型位集决定。

### 4.5 红蓝两队为什么仍在同一 Archetype

出生循环给每名 Soldier 写：

> **当前源码摘录**：[TrySpawnAuthorityPopulation](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
FGuLiMassIdentityFragment& Identity = EntityManager.GetFragmentDataChecked<
        FGuLiMassIdentityFragment>(Soldier.Entity);
Identity.SoldierId = Soldier.SoldierId;
Identity.Team = Team;
~~~

Team 是 Identity Fragment 里的字段值，不是 Tag 类型。因此红方 250 人与蓝方 250 人仍能使用同一出生 Archetype。

如果未来需要“只匹配红队”，当前设计不能靠 Archetype 类型缓存直接区分；要么在 Chunk/Entity 遍历中检查 Team 值，要么经过专门设计引入队伍 Tag。当前实现没有引入 RedTeamTag/BlueTeamTag。

## 五、真实案例二：客户端只创建精简镜像 Archetype

源码：`AGuLiCommanderPresentationActor::EnsureClientMirrorArchetype`

> **当前源码摘录**：[EnsureClientMirrorArchetype](D:/UE5.7/test1/Source/GuLiStrike/Commander/Presentation/GuLiCommanderPresentationActor.cpp)。

~~~cpp
const TArray<const UScriptStruct*> FragmentAndTagTypes = {
    FTransformFragment::StaticStruct(),
    FGuLiMassIdentityFragment::StaticStruct(),
    FGuLiMassHealthFragment::StaticStruct(),
    FGuLiClientSnapshotMirrorMassTag::StaticStruct()
};

FMassArchetypeCreationParams ArchetypeParams;
ArchetypeParams.DebugName = TEXT("GuLiClientSnapshotMirrorSoldiers");

ClientMirrorArchetype = EntityManager.CreateArchetype(
        FragmentAndTagTypes,
        ArchetypeParams);
~~~

当前客户端镜像只有 3 个 Fragment + 1 个 Tag。它没有：

- AgentRadius
- Velocity
- Force
- MoveTarget
- NavigationEdges
- ObstacleGridCellLocation
- Order
- SlotTarget
- AvoidanceOutput
- ServerAuthorityTag
- Movement/Avoidance ConstShared 参数

插值样本、外推、短时预测和 ISM 实例池由 Presentation Actor 自己的容器管理；当前 Archetype 清单也没有显式加入 Representation 或插值 Fragment。不能把技术方案中的理想描述误写成当前源码事实。

### 为什么故意精简

客户端不是玩法权威：

- 服务器负责路径、避障、速度、碰撞、生命和指令事实。
- 客户端只根据可靠 Soldier 状态和不可靠 Pose 样本更新本地镜像及表现。
- 注释明确说明客户端没有 Mass Processor 拥有 Transform，Presentation 是唯一写入者。

Archetype 在这里直接表达“双端职责边界”，而不只是内存优化。

## 六、真实案例三：死亡与调参都会迁移 Archetype

大多数状态变化只改值：

> **当前源码摘录**：[ApplyDamage 先更新已有 Health 列，结构尚未变化](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
FGuLiMassHealthFragment& Health = EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(
    Soldier.Entity);
Health.Health = Soldier.Health;
Health.bDead = Soldier.Health == 0u;
~~~

这些不会改变类型组合。

死亡流程末尾才执行结构变化：

> **当前源码摘录**：[ApplyDamage](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
EntityManager.RemoveFragmentFromEntity(
    Soldier.Entity,
    FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
~~~

Mass 必须把该 Entity 从：

~~~text
{Transform, Radius, Velocity, Force, MoveTarget,
 NavigationEdges, ObstacleGridCellLocation,
 Identity, Health, SoldierStats, Order, SlotTarget, AvoidanceOutput,
 ServerAuthorityTag, RuntimeTuningEven或Odd, MovementParams, AvoidanceParams}
~~~

迁往：

~~~text
{Transform, Radius, Velocity, Force, MoveTarget,
 NavigationEdges,
 Identity, Health, SoldierStats, Order, SlotTarget, AvoidanceOutput,
 ServerAuthorityTag, RuntimeTuningEven或Odd, MovementParams, AvoidanceParams}
~~~

差异就是 `ObstacleGridCellLocation`。

迁移后的关键性质：

- `FMassEntityHandle` 仍是同一个业务 Entity 的本地句柄。
- `FGuLiSoldierId` 不变。
- Fragment 列在新 Archetype/Chunk 中重新安置。
- 原 Chunk 的 Entity 顺序可能变化。
- 旧的 Fragment 引用、View、Chunk 内索引或范围缓存不能继续假定有效。

`AuthorityState->AuthorityArchetype` 初始保存出生组成，成功调参后更新为本次目标组成；它不是逐实体当前组成的清单。`ApplyPendingMovementSpeed` 在固定步边界构造新共享参数，再调用 `EntityManager.MoveEntityToAnotherArchetype` 逐实体换组成、换共享值。当前循环迁移所有有效实体，没有按存活过滤，也没有再次移除尸体的导航障碍格列；目标取自完整基础组成。因此不能承诺死亡后缺少的列会跨调参永久保持，其实际运行影响仍需专项验证。

因此，除了死亡移列，当前项目主动切换 Even/Odd Tag 也会改变 Archetype；这不否定“共享值不属于类型组成”的引擎规则。两种迁移都保留 Entity Handle，重新获取 Fragment，并不表示所有 500 名士兵永远处于同一个 Archetype。

公共框架变化只影响启停与消费边界：[发布组件](../../../Source/GuLiStrike/Commander/Framework/GuLiCommanderWorldReplicationComponent.cpp) 启用 Authority 后才尝试整批生成；[Presentation::UpdateNetworkPresentationSource](../../../Source/GuLiStrike/Commander/Presentation/GuLiCommanderPresentationActor.cpp) 在同步源失效时销毁旧镜像，名册门重新满足后再按当前 SoldierId 建立本地实体。

## 七、Query 为什么按 Archetype 缓存，而不是逐实体猜

当前唯一真实 Query 要求：

> **概念对照，非 C++ 源码**。

~~~text
ForceFragment                  ReadWrite + All
AvoidanceOutputFragment        ReadWrite + All
ServerAuthorityTag             All
~~~

Mass 可以先比较每个 Archetype 的组成位集：

- 客户端镜像：缺 Force、AvoidanceOutput、Server Tag，整套 Archetype 排除。
- 服务器出生 Archetype：满足，整套纳入。
- 死亡后 Archetype：虽然少了 ObstacleGridCellLocation，但 Query 没要求它，仍然满足。

这就是 Archetype Query 的成本优势：大量实体先在“类型组合”层面批量筛选，随后按 Chunk 绑定连续列 View。

## 八、MassArchetypeTypes.h 中项目尚未直接使用的类型

以下是引擎能力，不是当前 Commander 已落地的项目 API。

### 8.1 FMassArchetypeVersionedHandle

它把 `FMassArchetypeHandle` 与 `HandleVersion` 放在一起。版本用于判断目标 Archetype 内的 Entity 是否发生过搬动，适合保护缓存的 Entity Range。

项目当前只长期保存普通 Archetype Handle，没有直接声明 VersionedHandle。

### 8.2 FMassArchetypeEntityCollection

它把“同一 Archetype 的任意 Entity Handle 列表”压成：

> **示意代码**：提炼实体范围字段，省略构造器、默认值及其他成员；参见 [MassArchetypeTypes.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassArchetypeTypes.h>)。

~~~cpp
struct FArchetypeEntityRange
{
    int32 ChunkIndex;
    int32 SubchunkStart;
    int32 Length;
};
~~~

用途是把散装实体整理成连续 Chunk/Subchunk 范围，供 Query 只处理一部分 Entity。

当前 Commander 没有直接创建 `FMassArchetypeEntityCollection`。选择与指令集合保存 SoldierId，主权威移动遍历自己的 Soldier 注册表，避障捕获 Processor 则处理所有匹配 Chunk。

### 8.3 FMassArchetypeEntityCollectionWithPayload

它在 EntityCollection 旁附带与输入对齐的 Payload Slice，适合批量命令缓冲等场景。当前项目未使用。

### 8.4 FMassArchetypeChunkIterator 与 EntityInChunkDataHandle

它们是引擎内部/底层工具：

- ChunkIterator 遍历 collection 中的连续范围。
- EntityInChunkDataHandle 用 ChunkIndex、ChunkSerialNumber 检测底层数据是否变化。

项目 Processor 使用更高层的 `FMassEntityQuery::ForEachEntityChunk` 和 `FMassExecutionContext`，没有手写这些迭代器。

## 九、当前实现纠正了哪些常见误解

### 误解 1：一个兵种就是一个 Archetype

不对。Archetype 看当前元素类型组合。同一 Soldier 死亡并移除导航 Fragment 后就迁移了，但仍是同一业务兵种和同一 SoldierId。

### 误解 2：红队和蓝队必须两个 Archetype

不对。Team 当前只是 Identity Fragment 的字段值。

### 误解 3：25 人 Cohort 就是一个 Archetype 或 Chunk

不对。Cohort 是临时 SoldierId 集合；Chunk 是 Mass 的物理存储批次。

### 误解 4：SharedFragment 的具体值决定 Archetype 身份

不准确。组成描述记录 Shared/ConstShared 的类型位集；具体值随 Entity 创建传入并用于共享值绑定/Chunk 分组。

### 误解 5：客户端镜像应该复制服务器完整 Archetype

不对。客户端不拥有移动和碰撞事实，精简组成避免了双写 Transform 和误命中服务器 Processor。

### 误解 6：项目使用 MassSpawner 生成 500 人

没有。当前源码直接调用 `CreateArchetype + BatchCreateEntities`，客户端镜像按可靠状态调用 `CreateEntity`。MassSpawner 只是既有模块依赖，并非当前的出生链。

## 十、运行证据与边界

**当前源码：** 本次只核对当前项目与本机 UE 5.7.4 源码，没有重新编译、启动 PIE 或执行网络测试。

**历史验证：** [2026-08-27 总归档](../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)保存了当时 500 兵、动态选兵与移动冒烟的记录；原始临时日志和 JSON 已清理，不能再把它们列成可读取的现存证据。[2026-08-31 公共框架归档](../../Archive/20260831-公共战局框架与三类角色接入.md)记录冷编译成功、现有测试 50/50 通过及混合战局联调。NetworkGate 最终 ACK P95=138.1ms 达标，但未标记硬跳变 1 次，整体验收仍未通过；本次文档修订没有修复该问题。

**专项边界：** 未新增 Archetype 组成、共享值分组、Even/Odd 参数迁移或死亡迁移专项测试；这些说明来自源码，不能用整体冒烟代替逐项证明。

## 关联阅读

- 前置：[MassEntityElementTypes：组成中的五种元素](./MassEntityElementTypes.md)
- 前置：[MassEntityHandle：Entity 迁移时什么保持稳定](./MassEntityHandle.md)
- 下一篇：[MassEntityQuery 与 ExecutionContext：Query 如何匹配这些 Archetype](./MassEntityQuery与ExecutionContext.md)
- 技术方案：[20260827-Mass 双端同步架构草案](../20260827-Mass双端同步架构草案.md)
- 总归档：[20260827-Mass 动态 25 人控制组与双端平滑同步](../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)
- 玩法记录：[指挥官](../../Gameplay/指挥官.md)
