# 精读笔记：MassEntityQuery 与 ExecutionContext —— 避障捕获 Processor 的真实执行链

> 源码核对日期：2026-08-31（原笔记始于 2026-08-28）
>
> 引擎基线：Unreal Engine 5.7.4，CL 51494982
>
> 引擎原文件：`MassEntity/Public/MassEntityQuery.h`、`MassExecutionContext.h`、`MassRequirements.h`
>
>
>
> 项目基线：当前工作区源码，包含公共 Battle 提取及已有未提交修改；历史运行记录与本次静态核对分开列示。
>
> 阅读约定：源码摘录可省略外围代码；概念化定义、假设用法与错误示例明确标作“示意代码”，不表示项目已实现。

[Mass 阅读目录](./README.md) · [UE 网络教材](../UE网络教材/README.md)

## 先说最重要的事实

当前项目自有士兵代码只有一个实际使用的 `FMassEntityQuery`：

`Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp`

它只做一件非常具体的事：

1. 在 Epic Mass Avoidance 运行之后。
2. 找到所有带 ServerAuthorityTag、同时拥有 Force 与 AvoidanceOutput 的 Entity。
3. 按 Chunk 批量取得两列可写 View。
4. 把引擎 `FMassForceFragment::Value` 复制到项目 `FGuLiMassAvoidanceOutputFragment::Value`。
5. 清空源 Force。
6. 服务器 30Hz 固定步读取最近捕获的结果；这是数据依赖，不保证两者处于同一个 World Tick，补步也可能复用最近缓存。

圆形选兵、动态 25 人 Cohort、指令路径、生命伤害都**不是** Mass Query：

- 圆选使用 Authority Soldier 注册表和 100m 空间哈希。
- 主权威移动遍历 `AuthorityState->Soldiers`，用每个 Handle 直接访问 Fragment。
- Query 只用于适合 Chunk 批处理的避障结果捕获。

## 一、四个角色必须先分清

| 类型 | 职责 | 当前项目实例 |
|---|---|---|
| `UMassProcessor` | 决定什么时候运行、在哪个处理组运行 | `UGuLiCommanderAvoidanceCaptureProcessor` |
| `FMassEntityQuery` | 声明需要哪些元素、读写权限和 Presence，并缓存匹配 Archetype | `EntityQuery` |
| `FMassExecutionContext` | 某次执行的短生命周期工作台：当前 Chunk 的 Entity、Fragment View、DeltaTime、Deferred Buffer 等 | `Execute` 和 Chunk lambda 收到的 Context |
| `FMassEntityManager` | Entity/Archetype 的所有者，负责创建、销毁、结构迁移和单实体访问 | Authority Subsystem 与 Presentation Actor 都从各自 Mass Subsystem 取得 |

Query 不是 Entity 列表，ExecutionContext 也不是全局数据库：

~~~text
Processor 决定执行时机
    -> Query 找到匹配的 Archetype
        -> ForEachEntityChunk 逐 Chunk 绑定数据列
            -> ExecutionContext 暴露本 Chunk 的 View 和 Entity
                -> lambda 执行项目逻辑
~~~

## 二、为什么当前需要这个 Processor

Commander 权威模拟使用 30Hz 固定步，但 Epic Mass Avoidance 跟随 Mass 世界处理阶段产生 `FMassForceFragment`。

项目将 Mass 世界处理阶段的 Force 与固定步消费分开，避免直接共用未清理的累积列。固定步仍会按自身节拍读取最近一次快照；桥接 Fragment 不意味着两套调度已锁定为一一对应。当前使用：

> **当前源码摘录**：[FGuLiMassAvoidanceOutputFragment](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderMassFragments.h)。

~~~cpp
/** Latest Epic Mass avoidance acceleration captured once per world frame for 30 Hz authority use. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassAvoidanceOutputFragment
    : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    FVector Value = FVector::ZeroVector;
};
~~~

Capture Processor 把“Mass 世界帧的输出”快照到这个 Fragment，再清空源 Force；Authority 固定步只读项目快照。

类注释中的 “shared force accumulator” 指多个处理阶段之间共用的普通 `FMassForceFragment` 数据，不是 `FMassSharedFragment` 元素类型。

## 三、真实头文件：Processor 拥有一个 Query

源码：`GuLiCommanderAvoidanceCaptureProcessor.h`

> **当前源码摘录**：[UGuLiCommanderAvoidanceCaptureProcessor 完整类声明](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.h)。

~~~cpp
UCLASS()
class GULISTRIKE_API UGuLiCommanderAvoidanceCaptureProcessor final : public UMassProcessor
{
    GENERATED_BODY()

public:
    UGuLiCommanderAvoidanceCaptureProcessor();

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};
~~~

Query 是 Processor 的长期成员；Fragment View 则不是。Query 可以缓存匹配 Archetype，View 只能活在一次 Chunk 回调期间。

## 四、构造器：注册 Query，并锁定执行阶段

真实代码：

> **当前源码摘录**：[UGuLiCommanderAvoidanceCaptureProcessor 构造函数](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
UGuLiCommanderAvoidanceCaptureProcessor::
UGuLiCommanderAvoidanceCaptureProcessor()
    : EntityQuery(*this)
{
    bAutoRegisterWithProcessingPhases = true;
    ExecutionFlags = static_cast<int32>(
        EProcessorExecutionFlags::Standalone
        | EProcessorExecutionFlags::Server);

    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::ApplyForces;
    ExecutionOrder.ExecuteAfter.Add(
        UE::Mass::ProcessorGroupNames::Avoidance);
}
~~~

### 4.1 EntityQuery(*this) 做了什么

UE 5.7.4 的构造实现：

> **示意代码**：两段引擎实现拼接，中间其他函数省略；参见 [MassEntityQuery.cpp](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Private/MassEntityQuery.cpp>)。

~~~cpp
FMassEntityQuery::FMassEntityQuery(UMassProcessor& Owner)
{
    RegisterWithProcessor(Owner);
}

void FMassEntityQuery::RegisterWithProcessor(UMassProcessor& Owner)
{
    ExpectedContextType = EMassExecutionContextType::Processor;
    Owner.RegisterQuery(*this);
}
~~~

UE 5.7 也支持默认构造后显式注册：

> **示意代码**：显式注册的假设用法，当前 Processor 使用构造初始化列表；参见 [MassEntityQuery.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassEntityQuery.h>)。

~~~cpp
FMassEntityQuery EntityQuery;

// 再在别处手动：
EntityQuery.RegisterWithProcessor(*this);
~~~

这种显式写法并未废弃；`EntityQuery(*this)` 只是更紧凑地完成 Owner 注册和 ExpectedContextType 设置。构造器此时还没有绑定 EntityManager；随后 Processor 初始化阶段会先对 OwnedQueries 调用 `Initialize(EntityManager)`，再调用 `ConfigureQueries`。

### 4.2 为什么执行标志只有 Server 与 Standalone

客户端镜像不负责路径和避障事实。该 Processor 只应在：

- Dedicated/Listen Server 的服务器处理域。
- Standalone。

在纯客户端调度它会违反当前权威分工并产生无用工作；现有客户端镜像还缺 Force、AvoidanceOutput 和 ServerAuthorityTag，即使误调度也不会匹配该 Query。该 Processor 本身不写 Transform。

### 4.3 为什么必须在 Avoidance 之后

`ExecutionOrder.ExecuteAfter.Add(Avoidance)` 声明在该 Mass 处理图中的先后依赖：Avoidance 之后再捕获并清空。它不决定 WorldSubsystem Tick 与整张 Mass 处理图的跨系统先后。

如果顺序反了：

- 捕获到的可能是上一帧残留或零值。
- 当前帧 Avoidance 随后又写入 Force。
- Authority 固定步读取的快照与源累积时序失配。

执行组和排序在这里属于玩法正确性，不只是性能调度。

## 五、ConfigureQueries：声明数据合同

真实代码：

> **当前源码摘录**：[ConfigureQueries](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
void UGuLiCommanderAvoidanceCaptureProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.AddRequirement<FMassForceFragment>(
        EMassFragmentAccess::ReadWrite);

    EntityQuery.AddRequirement<FGuLiMassAvoidanceOutputFragment>(
        EMassFragmentAccess::ReadWrite);

    EntityQuery.AddTagRequirement<FGuLiServerAuthorityMassTag>(
        EMassFragmentPresence::All);
}
~~~

### 5.1 AddRequirement 的三个问题

每条 Fragment Requirement 实际回答：

1. 类型是什么？
2. 访问权限是什么？
3. Presence 是什么？

完整形式：

> **示意代码**：API 参数形式，省略模板声明与返回类型；参见 [MassRequirements.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassRequirements.h>)。

~~~cpp
AddRequirement<TFragment>(
    EMassFragmentAccess Access,
    EMassFragmentPresence Presence = EMassFragmentPresence::All);
~~~

项目省略了 Presence，因此两个 Fragment 都是 `All`。

### 5.2 为什么两个 Fragment 都是 ReadWrite

真实执行中：

> **当前源码摘录**：[Execute](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
Outputs[It].Value = Forces[It].Value;
Forces[It].Value = FVector::ZeroVector;
~~~

两列都被修改，所以都必须声明 `ReadWrite`。若声明 `ReadOnly` 却调用 `GetMutableFragmentView<T>()`，ExecutionContext 的检查会失败。

### 5.3 Tag Requirement 为什么是 All

> **当前源码摘录**：[ConfigureQueries](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
EntityQuery.AddTagRequirement<FGuLiServerAuthorityMassTag>(
    EMassFragmentPresence::All);
~~~

意思是匹配 Archetype 必须含这个 Tag。

这让 Query 精确排除客户端镜像。客户端镜像既没有 ServerAuthorityTag，也没有 Force/AvoidanceOutput，因此是双重隔离。

### 5.4 当前实现没有按 bDead 过滤

死亡使用 `FGuLiMassHealthFragment::bDead` 字段，而不是 DeadTag。Query 也没有 Health Requirement。

死亡时只移除 `FMassNavigationObstacleGridCellLocationFragment`，Force、AvoidanceOutput 与 ServerAuthorityTag 仍在，所以迁移后的死亡 Archetype 仍能匹配 Capture Query。Capture Processor 会继续复制/清空这两列；Authority 固定步在自己的 Soldier 循环里先识别死亡并跳过移动。

这是当前源码事实，不要虚构“Query 使用 DeadTag 排除尸体”。

## 六、Presence 的四种语义

引擎定义：

> **引擎源码摘录**：[EMassFragmentPresence](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassRequirements.h>)。

~~~cpp
enum class EMassFragmentPresence : uint8
{
    All,
    Any,
    None,
    Optional,
    MAX
};
~~~

| Presence | 匹配含义 | 是否绑定 View |
|---|---|---|
| `All` | 每个 All Requirement 都必须存在 | Access 不是 None 时绑定 |
| `Any` | Any 组至少有一种存在 | 存在的列可绑定 |
| `None` | 指定类型必须不存在 | 不绑定该列 |
| `Optional` | 有也匹配、没有也匹配 | 有时绑定，使用前需处理缺失 |

当前的 Capture Query 只用 `All`。以下类型当前未在 Commander Query 中使用，不能冒充项目案例：

- `Any`
- `None`
- `Optional`
- `AddChunkRequirement`
- `AddSharedRequirement`
- `AddConstSharedRequirement`
- Subsystem Requirement

注意引擎限制：Chunk、Shared、ConstShared Requirement 不接受 `Any`；ConstShared 访问固定为 ReadOnly。

## 七、Execute：完整的真实 Chunk 代码

> **当前源码摘录**：[Execute](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
void UGuLiCommanderAvoidanceCaptureProcessor::Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context)
{
    EntityQuery.ForEachEntityChunk(
        Context,
        [](FMassExecutionContext& ChunkContext)
        {
            const TArrayView<FMassForceFragment> Forces = ChunkContext
                    .GetMutableFragmentView<FMassForceFragment>();

            const TArrayView<FGuLiMassAvoidanceOutputFragment> Outputs = ChunkContext
                    .GetMutableFragmentView<
                        FGuLiMassAvoidanceOutputFragment>();

            for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator();
                 It;
                 ++It)
            {
                Outputs[It].Value = Forces[It].Value;
                Forces[It].Value = FVector::ZeroVector;
            }
        });
}
~~~

这是本项目理解 Query 与 Context 的主代码，不需要再造与当前玩法无关的假想示例。

## 八、ForEachEntityChunk 内部发生了什么

UE 5.7.4 的主流程可以概括为：

~~~text
1. 校验 Query 与 ExecutionContext 来自同一 EntityManager
2. PushQuery，缓存 Subsystem Requirements
3. CacheArchetypes
4. 把 Query 的 Requirement 绑定进 Context
5. 遍历匹配 Archetype
6. 遍历每个 Archetype 的 Chunk
7. 为当前 Chunk 绑定 Fragment Views 与 EntityListView
8. 调用项目 lambda
9. 清理 View；按 Context 的 flush policy 在处理边界刷新 Deferred Commands；PopQuery
~~~

### 8.1 CacheArchetypes 不是每帧全量重算

`FMassEntityQuery` 缓存：

- `ValidArchetypes`
- `OrderedArchetypeIndices`
- 每个 Archetype 的 Requirement-to-column mapping
- EntityManager 指针 Hash
- 最近的 ArchetypeDataVersion

如果 EntityManager 未变、Requirement 未变且 Archetype 数据版本没有新增变化，缓存检查很轻。新 Archetype 出现或 Query Requirement 改变时才更新匹配集合。

### 8.2 Query 匹配的是组成，不检查 Force 的数值

它只问：

~~~text
这个 Archetype 是否同时拥有：
    FMassForceFragment
    FGuLiMassAvoidanceOutputFragment
    FGuLiServerAuthorityMassTag
~~~

它不会问：

~~~text
Force.Value 是否非零？
bDead 是否为 false？
Team 是否为 Red？
~~~

这些是 Fragment 字段值，若需要只能在 Chunk/Entity 执行阶段读取并分支，或重新设计成 Tag/Chunk Filter 等结构信号。

## 九、ExecutionContext 为什么能返回对齐数组

Context 为当前 Chunk 绑定了：

> **示意代码**：仅列 Context 相关存储成员，省略引擎宏及中间字段；参见 [MassExecutionContext.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassExecutionContext.h>)。

~~~cpp
TArrayView<FMassEntityHandle> EntityListView;
TArray<FFragmentView, TInlineAllocator<8>> FragmentViews;
TArray<FChunkFragmentView, TInlineAllocator<4>> ChunkFragmentViews;
TArray<FConstSharedFragmentView, TInlineAllocator<4>> ConstSharedFragmentViews;
TArray<FSharedFragmentView, TInlineAllocator<4>> SharedFragmentViews;
~~~

内部 `EntityListView` 是可绑定的 `TArrayView`；对外公开的 `GetEntities()` 返回 `TConstArrayView<FMassEntityHandle>`，调用方不能借它改写 Handle 列表。

项目取得：

> **示意代码**：只表示两种 View 类型，实际 Execute 使用 const 局部变量并立即初始化；参见 [GuLiCommanderAvoidanceCaptureProcessor.cpp](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
TArrayView<FMassForceFragment> Forces;
TArrayView<FGuLiMassAvoidanceOutputFragment> Outputs;
~~~

对同一个 It：

~~~text
EntityListView[It]  = 当前 Chunk 第 It 个 Entity
Forces[It]          = 该 Entity 的 Force 列
Outputs[It]         = 该 Entity 的 AvoidanceOutput 列
~~~

所以：

> **当前源码摘录**：[Execute](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
Outputs[It].Value = Forces[It].Value;
~~~

不是两次按 Handle 查找，而是对同一批 Entity 的两列连续内存做相同行号复制。这正是 Mass 的 Chunk 数据布局要带来的收益。

## 十、FEntityIterator 在当前代码里的价值

`CreateEntityIterator()` 返回不可复制的短生命周期迭代器。它：

- 从 0 遍历到当前 Chunk 的 Entity 数。
- 可隐式转换为 int32，直接作为 View 下标。
- 在调试版本中接入 Entity/Fragment 断点检查。
- 用序列号检测不一致的迭代使用。

当前 lambda 不需要具体 Handle，所以没有调用：

> **示意代码**：可用 API 的示例调用，当前 Execute 未使用；参见 [MassExecutionContext.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassExecutionContext.h>)。

~~~cpp
ChunkContext.GetEntity(It);
ChunkContext.GetEntities();
~~~

如果未来日志或命令需要实体句柄，可以从 Context 取；但这是 API 扩展方向，不是当前已经存在的代码。

## 十一、GetMutableFragmentView 的权限检查

引擎内部逻辑：

> **示意代码**：GetMutableFragmentView 检查步骤的伪代码，不是引擎函数体；参见 [MassExecutionContext.h](<C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/MassEntity/Public/MassExecutionContext.h>)。

~~~cpp
template<typename TFragment>
TArrayView<TFragment> GetMutableFragmentView()
{
    // 找到 Query 已声明的该类型 View
    // CHECK_IF_VALID
    // CHECK_IF_READWRITE
    // 返回当前 Chunk 的数组 View
}
~~~

因此下面三者必须一致：

~~~text
ConfigureQueries: AddRequirement<T>(ReadWrite)
Execute:          GetMutableFragmentView<T>()
实现:             确实修改 T
~~~

如果只读取，应声明 `ReadOnly` 并调用 `GetFragmentView<T>()`。当前项目没有真实只读 Query View 用例。

View 的寿命只覆盖当前 Chunk 回调：

- 不存进成员变量。
- 不跨下一次 `ForEachEntityChunk`。
- 不跨结构修改。
- 不在 lambda 结束后继续引用其中元素。

长期状态放 Fragment，长期业务身份用 SoldierId，本地 Entity 定位用 Handle。

## 十二、捕获结果如何进入 30Hz 权威移动

源码：`UGuLiBattleAuthoritySubsystem::TickAuthority`

> **当前源码摘录**：[TickAuthority](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
const FGuLiMassAvoidanceOutputFragment& AvoidanceOutput = EntityManager.GetFragmentDataChecked<
        FGuLiMassAvoidanceOutputFragment>(Soldier.Entity);

const FVector EngineAvoidanceDelta = AvoidanceOutput.Value.GetClampedToMaxSize(
        MovementSpeedCentimetersPerSecond * 4.0f)
    * FixedDeltaSeconds;

const FVector TargetVelocity = (DesiredVelocities[SoldierIndex]
        + AvoidanceVelocity
        + EngineAvoidanceDelta)
    .GetClampedToMaxSize(
        MovementSpeedCentimetersPerSecond);

Soldier.Velocity = FMath::VInterpTo(
    Soldier.Velocity,
    TargetVelocity,
    FixedDeltaSeconds,
    8.0f);
~~~

三项合流：

1. `DesiredVelocities[SoldierIndex]`：共享路径/局部流场与编队槽位产生的期望速度。
2. `AvoidanceVelocity`：项目空间哈希计算的局部分离。
3. `EngineAvoidanceDelta`：Capture Query 保存的 Epic Mass Avoidance 输出。

随后写回 Transform、Velocity、Order Fragment。唯一的 `GuLiCommanderWorldReplicationComponent` 按目标 10Hz 请求 Authority 捕获，再经专业 NetSync 发给已通过名册门的连接；Query 自身不发 RPC。

这条真实链路说明：Query 本身不“让 Soldier 移动”，它只高效搬运某个处理阶段的数据；最终玩法由多个系统接力完成。

## 十三、为什么主 Authority 循环没有改成 Query

当前系统同时维护 `FSoldierRuntime`：

- SoldierId
- Team
- Location/Velocity/Yaw
- Health / MaxHealth 与兵种数值
- StateRevision
- ActiveOrderId
- DeathSimulationSeconds
- 流场与编队关联所需的业务索引

选择和指令还依赖：

- `SoldierIndexById`
- `SpatialGrid`
- `OrderFormations`
- ControlCohort 的 SoldierId 成员

所以当前采用两条访问路径：

| 工作 | 当前路径 | 原因 |
|---|---|---|
| 对所有匹配 Authority Entity 搬运两列避障数据 | Mass Query + Chunk View | 规则只依赖组成，适合连续批处理 |
| 创建 500 人并初始化各列 | EntityManager + BatchCreate + Handle | 一次性建立业务注册表 |
| 圆形选兵 | 100m SpatialGrid + Soldier 注册表 | 需要空间圆查询、队伍、死亡、确定性 SoldierId 排序 |
| 为已选成员下令 | SoldierId → Runtime → Handle | 只访问某些业务成员 |
| 30Hz 权威移动 | Runtime 数组 + Handle | 与编队、路径、空间哈希和网络状态紧密耦合 |
| 单兵伤害/死亡 | SoldierId → Handle | 点操作，并包含结构迁移 |

这些路径分别适配批处理和按业务身份访问。公共 Battle 框架负责玩家身份、连接及 Pawn 生命周期；发布组件控制士兵模拟启停。停用时 Authority 无人口，Capture Query 即使注册在处理阶段也没有本项目权威实体可匹配。Ground/Air 的 CharacterMovement 不经过此 Query。

## 十四、结构修改与 Context.Defer 的边界

当前 Capture Processor 只改现有 Fragment 的值：

> **当前源码摘录**：[Execute](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
Outputs[It].Value = Forces[It].Value;
Forces[It].Value = FVector::ZeroVector;
~~~

它没有：

- Add/Remove Fragment
- Add/Remove Tag
- Destroy Entity
- Build Entity

因此当前代码没有调用 `Context.Defer()`。

如果未来要在 `ForEachEntityChunk` 中根据条件移除导航 Fragment或销毁 Entity，应通过 Context 的 Deferred Command Buffer 记录结构命令，让 Chunk 遍历结束后统一执行。否则当前 Archetype/Chunk 正在迭代时就被改写，会让 View、Entity 顺序和 Range 失效。

当前真实的：

> **当前源码摘录**：[ApplyDamage](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp)。

~~~cpp
EntityManager.RemoveFragmentFromEntity(
    Soldier.Entity,
    FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
~~~

当前调用点位于 Authority Subsystem 的单兵伤害路径，并同步调用 EntityManager。安全前提不只是“函数名不在 Query lambda 中”，还要求 EntityManager 此刻不处于会被该结构修改破坏的 Mass processing/并行遍历阶段，调用线程与时机合法。若伤害未来从 Processor 或正在 processing 的回调触发，就必须改用 Deferred Command。

> 在 Processor/正在 processing 的遍历中做结构修改要 Defer；外部同步结构操作也必须确认 EntityManager 当前时机和线程允许。

## 十五、ExecutionContext 当前未使用但必须认识的能力

| API | 当前 Commander 是否使用 | 用途 |
|---|---|---|
| `GetMutableFragmentView<T>()` | 是 | 当前 Chunk 的可写每实体列 |
| `CreateEntityIterator()` | 是 | 对齐遍历当前 Chunk |
| `GetFragmentView<T>()` | 否 | 只读每实体列 |
| `GetEntity()/GetEntities()` | 否 | 取当前 Chunk 的 Entity Handle |
| `GetChunkFragment<T>()` | 否 | 读取每 Chunk 一份数据 |
| `GetSharedFragment<T>()` | 否 | 读取可变共享值 |
| `GetConstSharedFragment<T>()` | 否 | 读取 ConstShared 值 |
| `GetSubsystem<T>()` | 否 | 访问 Query 已声明的 Subsystem |
| `GetDeltaTimeSeconds()` | 否 | 当前处理步 DeltaTime |
| `Defer()` | 否 | 延迟结构命令 |
| `SetEntityCollection`/Collection overload | 否 | 只处理指定 Archetype 范围 |

同样，项目没有使用：

- `ParallelForEachEntityChunk`
- `SetChunkFilter`
- `GroupBy`
- `FExecutionLimiter`
- `FMassArchetypeEntityCollection`

这些可以作为后续优化工具评估，但不能写成 8 月 27 日已经落地的实现。

## 十六、常见错误对照

### 错误 1：默认构造 Query 后立刻 AddRequirement

UE 5.7 的 Requirement 修改要求 Query 已初始化。Processor 成员采用：

> **当前源码摘录**：[UGuLiCommanderAvoidanceCaptureProcessor 构造初始化列表](D:/UE5.7/test1/Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp)。

~~~cpp
UGuLiCommanderAvoidanceCaptureProcessor()
    : EntityQuery(*this)
~~~

不要照搬旧教程里未初始化的默认 Query。

### 错误 2：ReadOnly Requirement 配 Mutable View

Context 会检查并报错。访问声明必须反映真实写行为。

### 错误 3：只靠 Force Fragment 区分服务器与客户端

当前客户端确实没有 Force，但显式 ServerAuthorityTag 才是处理域合同。未来列变化时也不应误命中镜像。

### 错误 4：把 Field Value 当成 Archetype Query 条件

Query 不会因为 Health=0 或 Team=Red 自动筛选。值筛选必须在执行阶段或通过新的结构设计表达。

### 错误 5：跨回调保存 View

View 指向当前 Chunk 连续列；下一 Chunk、结构迁移或 Query 结束后都不应继续使用。

### 错误 6：在 Chunk 遍历中同步 RemoveFragment

会使当前结构数据失效。Processor 内结构命令应 Defer。

### 错误 7：把圆选改写成“Query 自动选出 25 人”

当前的动态 Cohort 算法依赖圆心、空间哈希、同队过滤、SoldierId 确定性排序、动态质心与 300m 补员限制，当前不由 Mass Query 实现。

## 十七、真实执行链汇总

~~~text
Epic Mass Movement/Avoidance
    产生 FMassForceFragment
        |
        v
UGuLiCommanderAvoidanceCaptureProcessor
    Server/Standalone
    ApplyForces group
    ExecuteAfter Avoidance
        |
        v
FMassEntityQuery
    Force RW + AvoidanceOutput RW + ServerAuthorityTag All
        |
        v
ForEachEntityChunk(Context)
    绑定 Forces[] 与 Outputs[]
        |
        v
Outputs[i] = Forces[i]
Forces[i] = 0
        |
        v
UGuLiBattleAuthoritySubsystem 30Hz fixed step
    期望速度 + 项目局部分离 + 引擎避障快照
        |
        v
Transform / Velocity / Order
        |
        v
GuLiCommanderWorldReplicationComponent
    目标 10Hz 捕获与分摊调度
        |
        v
StateReplicator 属性复制 + NetSync 姿态 RPC
    公共就绪不代替士兵名册门
        |
        v
客户端样本、Mass 镜像、ISM 与脚环表现
~~~

## 十八、验证证据与边界

**当前源码：** 本次只核对当前项目与本机 UE 5.7.4 源码，没有重新编译、启动 PIE 或执行网络测试。

**历史验证：** [2026-08-27 总归档](../../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)保存了当时 500 兵、动态选兵与移动冒烟的记录；原始临时日志和 JSON 已清理，不能再把它们列成可读取的现存证据。[2026-08-31 公共框架归档](../../../Archive/20260831-公共战局框架与三类角色接入.md)记录冷编译成功、现有测试 50/50 通过及混合战局联调。NetworkGate 最终 ACK P95=138.1ms 达标，但未标记硬跳变 1 次，整体验收仍未通过；本次文档修订没有修复该问题。

**专项边界：** 未新增 Query/ExecutionContext/AvoidanceCapture 专项测试，也没有本轮非零 Force 捕获清空、逐 Chunk 次数或独立性能采样；不能由整体测试通过推断这些观测已经完成。500 人窄口拥堵和长期性能仍需专门验证。

## 十九、写新的 Mass Processor 时检查什么

1. Query 是否通过 `EntityQuery(*this)` 或有效 EntityManager 正确初始化？
2. Processor 的 ExecutionFlags 是否符合 Server/Client/Standalone 权威边界？
3. 执行组与 Before/After 是否保证输入已经产生？
4. 每个 Fragment 的 Access 与实际读写是否一致？
5. Presence 是在筛类型，还是你真正想筛字段值？
6. Tag 是否明确表达处理域？
7. Mutable/ReadOnly View 是否配对正确？
8. View 是否只活在当前 Chunk 回调？
9. 若处于 Processor/Mass processing 遍历中，结构修改是否通过 Deferred Command；若在外部同步路径，当前线程和 EntityManager 时机是否允许？
10. 这项工作真的适合全 Archetype/Chunk 批处理，还是 SoldierId → Handle 的点访问更清晰？
11. 文档是否把“当前实现”和“未来可用 API”分开？
12. 是否有真实运行证据，还是仅完成源码接入？

## 关联阅读

- 前置：[MassEntityElementTypes：Requirement 中的 Fragment 与 Tag 属于哪一类](./MassEntityElementTypes.md)
- 前置：[MassEntityHandle：Context Entity 与本地句柄边界](./MassEntityHandle.md)
- 前置：[MassArchetypeTypes：Query 实际缓存和匹配什么](./MassArchetypeTypes.md)
- 技术方案：[20260827-Mass 双端同步架构草案](../../20260827-Mass双端同步架构草案.md)
- 总归档：[20260827-Mass 动态 25 人控制组与双端平滑同步](../../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)
- 玩法记录：[指挥官](../../../Gameplay/指挥官.md)
