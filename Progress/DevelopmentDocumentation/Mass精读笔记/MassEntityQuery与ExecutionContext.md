# 精读笔记：MassEntityQuery 与 ExecutionContext —— 避障捕获 Processor 的真实执行链

> 重写日期：2026-08-28
>
> 引擎基线：Unreal Engine 5.7.4，CL 51494982
>
> 引擎原文件：`MassEntity/Public/MassEntityQuery.h`、`MassExecutionContext.h`、`MassRequirements.h`
>
> 项目样本：2026-08-27 新增的 `UGuLiCommanderAvoidanceCaptureProcessor` 与服务器 30Hz Authority 固定步。
>
> Git 边界：Commander 目录当前仍未提交；日期归属依据文件时间、当天归档和日志，不是 Git commit 的逐行历史。

## 先说最重要的事实

当前项目只有一个真实的 `FMassEntityQuery`：

`Source/GuLiStrike/Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.cpp`

它只做一件非常具体的事：

1. 在 Epic Mass Avoidance 运行之后。
2. 找到所有带 ServerAuthorityTag、同时拥有 Force 与 AvoidanceOutput 的 Entity。
3. 按 Chunk 批量取得两列可写 View。
4. 把引擎 `FMassForceFragment::Value` 复制到项目 `FGuLiMassAvoidanceOutputFragment::Value`。
5. 清空源 Force。
6. 服务器自己的 30Hz 固定步随后读取这个稳定快照。

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

## 二、为什么昨天需要这个 Processor

Commander 权威模拟使用 30Hz 固定步，但 Epic Mass Avoidance 跟随 Mass 世界处理阶段产生 `FMassForceFragment`。

如果固定步直接把同一个累积 Force 在多个步骤或不同渲染帧节奏下反复消费，结果会随帧率和积累时机变化。昨天的实现增加一个项目 Fragment：

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

~~~cpp
#include "MassEntityQuery.h"
#include "MassProcessor.h"

/**
 * Captures Epic MovingAvoidance's result after the Avoidance phase and clears the
 * shared force accumulator. The 30 Hz authority integrator then reads the stable
 * project fragment, avoiding frame-rate-dependent accumulation between fixed steps.
 */
UCLASS()
class GULISTRIKE_API UGuLiCommanderAvoidanceCaptureProcessor final
    : public UMassProcessor
{
    GENERATED_BODY()

public:
    UGuLiCommanderAvoidanceCaptureProcessor();

protected:
    virtual void ConfigureQueries(
        const TSharedRef<FMassEntityManager>& EntityManager) override;

    virtual void Execute(
        FMassEntityManager& EntityManager,
        FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};
~~~

Query 是 Processor 的长期成员；Fragment View 则不是。Query 可以缓存匹配 Archetype，View 只能活在一次 Chunk 回调期间。

## 四、构造器：注册 Query，并锁定执行阶段

真实代码：

~~~cpp
UGuLiCommanderAvoidanceCaptureProcessor::
UGuLiCommanderAvoidanceCaptureProcessor()
    : EntityQuery(*this)
{
    bAutoRegisterWithProcessingPhases = true;
    ExecutionFlags = static_cast<int32>(
        EProcessorExecutionFlags::Standalone
        | EProcessorExecutionFlags::Server);

    ExecutionOrder.ExecuteInGroup =
        UE::Mass::ProcessorGroupNames::ApplyForces;
    ExecutionOrder.ExecuteAfter.Add(
        UE::Mass::ProcessorGroupNames::Avoidance);
}
~~~

### 4.1 EntityQuery(*this) 做了什么

UE 5.7.4 的构造实现：

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

`ExecuteAfter(Avoidance)` 保证引擎先产出 Force；Capture Processor 再读取并清空。

如果顺序反了：

- 捕获到的可能是上一帧残留或零值。
- 当前帧 Avoidance 随后又写入 Force。
- Authority 固定步读取的快照与源累积时序失配。

执行组和排序在这里属于玩法正确性，不只是性能调度。

## 五、ConfigureQueries：声明数据合同

真实代码：

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

~~~cpp
AddRequirement<TFragment>(
    EMassFragmentAccess Access,
    EMassFragmentPresence Presence = EMassFragmentPresence::All);
~~~

项目省略了 Presence，因此两个 Fragment 都是 `All`。

### 5.2 为什么两个 Fragment 都是 ReadWrite

真实执行中：

~~~cpp
Outputs[It].Value = Forces[It].Value;
Forces[It].Value = FVector::ZeroVector;
~~~

两列都被修改，所以都必须声明 `ReadWrite`。若声明 `ReadOnly` 却调用 `GetMutableFragmentView<T>()`，ExecutionContext 的检查会失败。

### 5.3 Tag Requirement 为什么是 All

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

昨天的 Capture Query 只用 `All`。以下类型当前未在 Commander Query 中使用，不能冒充项目案例：

- `Any`
- `None`
- `Optional`
- `AddChunkRequirement`
- `AddSharedRequirement`
- `AddConstSharedRequirement`
- Subsystem Requirement

注意引擎限制：Chunk、Shared、ConstShared Requirement 不接受 `Any`；ConstShared 访问固定为 ReadOnly。

## 七、Execute：完整的真实 Chunk 代码

~~~cpp
void UGuLiCommanderAvoidanceCaptureProcessor::Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context)
{
    EntityQuery.ForEachEntityChunk(
        Context,
        [](FMassExecutionContext& ChunkContext)
        {
            const TArrayView<FMassForceFragment> Forces =
                ChunkContext
                    .GetMutableFragmentView<FMassForceFragment>();

            const TArrayView<FGuLiMassAvoidanceOutputFragment> Outputs =
                ChunkContext
                    .GetMutableFragmentView<
                        FGuLiMassAvoidanceOutputFragment>();

            for (FMassExecutionContext::FEntityIterator It =
                    ChunkContext.CreateEntityIterator();
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

~~~cpp
TArrayView<FMassEntityHandle> EntityListView;
TArray<FFragmentView, TInlineAllocator<8>> FragmentViews;
TArray<FChunkFragmentView, TInlineAllocator<4>> ChunkFragmentViews;
TArray<FConstSharedFragmentView, TInlineAllocator<4>> ConstSharedFragmentViews;
TArray<FSharedFragmentView, TInlineAllocator<4>> SharedFragmentViews;
~~~

内部 `EntityListView` 是可绑定的 `TArrayView`；对外公开的 `GetEntities()` 返回 `TConstArrayView<FMassEntityHandle>`，调用方不能借它改写 Handle 列表。

项目取得：

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

~~~cpp
ChunkContext.GetEntity(It);
ChunkContext.GetEntities();
~~~

如果未来日志或命令需要实体句柄，可以从 Context 取；但这是 API 扩展方向，不是昨天已经存在的代码。

## 十一、GetMutableFragmentView 的权限检查

引擎内部逻辑：

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

~~~cpp
const FGuLiMassAvoidanceOutputFragment& AvoidanceOutput =
    EntityManager.GetFragmentDataChecked<
        FGuLiMassAvoidanceOutputFragment>(Soldier.Entity);

const FVector EngineAvoidanceDelta =
    AvoidanceOutput.Value.GetClampedToMaxSize(
        MovementSpeedCentimetersPerSecond * 4.0f)
    * FixedDeltaSeconds;

const FVector TargetVelocity =
    (DesiredVelocities[SoldierIndex]
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

随后写回 Transform、Velocity、Order Fragment，并由 10Hz Pose 捕获发给客户端表现。

这条真实链路说明：Query 本身不“让 Soldier 移动”，它只高效搬运某个处理阶段的数据；最终玩法由多个系统接力完成。

## 十三、为什么主 Authority 循环没有改成 Query

当前系统同时维护 `FSoldierRuntime`：

- SoldierId
- Team
- Location/Velocity/Yaw
- StateRevision
- ActiveOrderId
- DeathSimulationSeconds
- 流场与编队关联所需的业务索引

选择和指令还依赖：

- `SoldierIndexById`
- `SpatialGrid`
- `OrderFormations`
- ControlCohort 的 SoldierId 成员

所以昨天采用两条访问路径：

| 工作 | 当前路径 | 原因 |
|---|---|---|
| 对所有匹配 Authority Entity 搬运两列避障数据 | Mass Query + Chunk View | 规则只依赖组成，适合连续批处理 |
| 创建 500 人并初始化各列 | EntityManager + BatchCreate + Handle | 一次性建立业务注册表 |
| 圆形选兵 | 100m SpatialGrid + Soldier 注册表 | 需要空间圆查询、队伍、死亡、确定性 SoldierId 排序 |
| 为已选成员下令 | SoldierId → Runtime → Handle | 只访问某些业务成员 |
| 30Hz 权威移动 | Runtime 数组 + Handle | 与编队、路径、空间哈希和网络状态紧密耦合 |
| 单兵伤害/死亡 | SoldierId → Handle | 点操作，并包含结构迁移 |

Query 不是越多越“Mass”。选择最符合数据访问模式的路径，才是当前实现的真实设计。

## 十四、结构修改与 Context.Defer 的边界

当前 Capture Processor 只改现有 Fragment 的值：

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

昨天真实的：

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

昨天的动态 Cohort 算法依赖圆心、空间哈希、同队过滤、SoldierId 确定性排序、动态质心与 300m 补员限制，当前不由 Mass Query 实现。

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
10Hz PoseChunk + Reliable Soldier State
        |
        v
客户端插值、预测、ISM 与脚环表现
~~~

## 十八、验证证据与边界

已存在的整体链路证据：

- `Progress/CommanderDynamicCohortTests-FinalReliableFallback.log`：18 项 Automation 成功、0 失败；覆盖 Cohort、Navigation、Network/Protocol。
- `Progress/CommanderDynamicPIE-FinalReliable.log`：500 个独立 Authority Soldier、500/500 CommanderSoldier NavMesh 投射；smoke 记录动态成员 25、移动 1813cm、摧毁 25、未知 ID 拒绝。
- `Progress/CommanderPIEValidation.json`：UnitInstances=500、RingInstances=500、两套 NavData、顶层 `errors=[]`。

边界：

- 没有名为 Query/ExecutionContext/AvoidanceCapture 的专项 Automation。
- 源码已编译接入，整体 Commander 流程有 PIE 运行证据；现有日志没有该 Processor/Fragment 的专属 marker，也没有非零 Force 复制并清空的观测值，因此不能直接证明当前 `Execute` 被调用，更不能量化逐 Chunk 次数或独立性能收益。
- 当前跨午夜 Presentation 源码版本晚于最后一次成功 smoke；这不影响本篇 8 月 27 日 Capture Processor 的源码归属，但限制了对完整“直到当前客户端版本”端到端链路的验证表述。
- 500 人同时移动、复杂窄口长期拥堵、服务器 GameThread p95/p99 等性能 Gate 仍未完成。

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
- 技术方案：[20260827-Mass 双端同步架构草案](../20260827-Mass双端同步架构草案.md)
- 总归档：[20260827-Mass 动态 25 人控制组与双端平滑同步](../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)
- 玩法记录：[指挥官](../../Gameplay/指挥官.md)
