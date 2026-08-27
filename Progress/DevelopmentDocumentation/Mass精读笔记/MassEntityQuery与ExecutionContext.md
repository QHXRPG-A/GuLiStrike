# 精读笔记：MassEntityQuery.h + MassExecutionContext.h —— 查询与执行上下文

> 原文件：`Public/MassEntityQuery.h`（393 行）、`Public/MassExecutionContext.h`（669 行）
> 配套实现：`Private/MassEntityQuery.cpp`（867 行）、`Private/MassExecutionContext.cpp`（285 行），
> 另借 `Private/MassArchetypeData.cpp` 的 `ExecuteFunction` 补全拼图（查询最终把活儿派给它）
> 引擎版本：5.7.4 ｜ 阅读路线第 4 步（核心章节）

---

## 这篇应该怎么读

这篇同时服务两种读者：

- **只想把 Mass Processor 写对**：先读“项目背景”“角色分工”和文末五个实际案例，再回头看执行链路。
- **想读懂 Mass 源码**：按“查询缓存 → 主循环 → Archetype → Chunk 绑定 → 回调”的顺序阅读第 2～5 节。

读完后应该能回答四个实际问题：

1. `AddRequirement` 为什么既是筛选条件，也是读写权限声明？
2. `ForEachEntityChunk` 为什么每帧都能调用，却不会每帧重新扫描全部 Archetype？
3. Lambda 里的 `Context.GetFragmentView<T>()` 究竟从哪里拿到连续数组？
4. 串行版换成并行版后，哪些代码仍然安全，哪些操作必须进入延迟命令缓冲？

如果只记一句话：**Query 决定“哪些箱子能进来”，ExecutionContext 决定“当前箱子的哪些数据可以交给你”。**

## 先把四个角色分清

| 角色 | 人话解释 | 主要职责 | 不负责什么 |
|---|---|---|---|
| `FMassEntityQuery` | 筛选单 + 数据申请单 | 声明需要的 Fragment、Tag、共享数据和子系统，并缓存匹配的 Archetype | 不拥有实体数据，也不执行游戏逻辑 |
| `FMassExecutionContext` | 当前批次的工作台 | 保存当前 Chunk 的实体数量、Fragment 视图、时间、子系统和延迟命令缓冲 | 不决定哪些 Archetype 匹配查询 |
| `FMassArchetypeData` | 同构实体的仓库 | 管理符合某种组成的实体和 Chunk，并把正确的内存切片绑定到 Context | 不知道你的具体玩法逻辑 |
| `FMassExecuteFunction` | 你写的处理逻辑 | 对当前 Chunk 或 Subchunk 的连续数据执行计算 | 不应该在循环中直接改实体组成 |

用仓库比喻：

```text
Query                 = 采购单：我要“有位置、有速度、没死亡标签”的货
Cached Archetypes     = 符合采购单的仓库清单
Requirement Mapping   = 每个仓库自己的货架索引表
ExecutionContext      = 推到当前货架前的工作台
Lambda                = 工人对这一箱货执行的操作
Deferred Command      = 先填变更单，等安全时间统一搬货/换仓库
```

## 项目背景：开发《全面战争》/《最高指挥官》式的大规模战场

假设项目的战术地图需要同时模拟：

- 20,000 名步兵；
- 5,000 名骑兵；
- 2,000 名弓箭手；
- 数千名伤员、尸体、溃逃单位和等待增援的单位；
- 编队、军团、指挥官、载具、炮弹和局部战斗区域；
- 单位位置、速度、生命值、士气、武器、目标和编队信息。

这类项目不是简单地把传统 `AActor` 数量扩大。实际工程通常需要把**权威模拟、战术决策、编队控制和画面表现拆开**：高价值英雄或玩家直控单位可以保留 Actor，普通士兵、载具、炮弹和远处军团则更适合用 Mass Entity 承载数据与批处理逻辑。

如果每个普通单位都是带 Tick 的 `AActor`，每帧就会在大量分散的 UObject 上重复做函数调用、指针跳转和状态判断。CPU 看到的不是“连续处理三万份位置和速度”，而是“三万次定位对象，再从对象中取数据”。

ECS 的思路完全不同：

> 单位身份与单位数据分离；位置、生命值、武器分别存成连续的数据列；移动、攻击、士气等系统按查询批量处理拥有相应数据的实体。

### Mass 类型在项目中的工程职责

| Mass/ECS 类型 | 在大战略项目中的用途 | 生命周期/边界 |
|---|---|---|
| `FMassEntityHandle` | 引用一个具体单位、炮弹、编队控制实体或其他模拟对象 | 稳定身份；不直接包含 Fragment 数据 |
| Entity Fragment | 保存单实体状态，例如位置、生命值、目标、弹药、编队槽位 | 每实体一份；应按访问模式拆成小而聚合的数据块 |
| Tag | 表达只需要存在性的状态，例如 `Dead`、`Retreating`、`NeedsTarget` | 添加/删除会改变组成并触发 Archetype 迁移 |
| Shared/ConstShared Fragment | 保存一组实体共享的单位模板或武器参数 | 多实体共用；适合低频变化或只读配置 |
| Chunk Fragment | 保存一个物理 Chunk 的批次状态，例如模拟 LOD、空间格或休眠标记 | 每 Chunk 一份；不能直接代表玩法上的营、连或编队 |
| `FMassArchetypeData` | 存储拥有同一组成的全部实体，并管理其 Chunk | 引擎内部存储对象；玩法代码一般通过 Query 间接使用 |
| Chunk | Archetype 内的一块连续 SoA 内存 | 处理器实际批量遍历的粒度，不是玩法组织层级 |
| `UMassProcessor` | 实现移动、索敌、伤害、士气、补给、LOD 和表现同步等系统 | 描述“怎样处理数据”，通常不拥有长期单位状态 |
| `FMassEntityQuery` | 声明 Processor 处理哪些组成、读取什么、修改什么 | 可缓存匹配 Archetype；本身不保存本帧实体结果 |
| `FMassExecutionContext` | 承载当前 Chunk/Subchunk 的 Fragment View、实体列表和命令缓冲 | 极短生命周期；不得保存其中的 View 到回调之外 |
| `FMassArchetypeEntityCollection` | 描述某个 Archetype 中一组连续实体范围 | 适合炮击命中、框选命令等局部批处理；结构变化后可能过期 |
| `FMassEntityInChunkDataHandle` | 记录实体当前 Chunk、Chunk 内索引和安全校验信息 | 物理位置句柄，不等同于实体身份，不应长期缓存 |

三个容易混淆的句柄/集合分别解决不同问题：

```text
FMassEntityHandle              = 某个模拟对象的稳定身份
FMassEntityInChunkDataHandle   = 该对象当前位于哪一个 Chunk 的哪一个槽位
FMassArchetypeEntityCollection = 本次局部批处理涉及哪些连续槽位范围
```

按实际编码频率，可以把这些类型再分成三层：

| 使用层级 | 类型 | 项目代码中的使用方式 |
|---|---|---|
| 高频直接使用 | `UMassProcessor`、`FMassEntityQuery`、`FMassExecutionContext` | 几乎每个移动、战斗、士气、补给 Processor 都会接触 |
| 按业务需要使用 | `FMassEntityHandle`、`FMassArchetypeEntityCollection`、`FEntityCollection` | 保存目标身份、处理框选单位、炮击命中名单或事件涉及的局部实体 |
| 主要由引擎内部使用 | `FMassArchetypeData`、`FMassEntityInChunkDataHandle`、`FMassQueryRequirementIndicesMapping` | 理解源码、排查失效和性能问题时很重要，普通玩法代码通常不直接操作 |

因此，读到 `FMassArchetypeData::ExecuteFunction` 或 `FMassEntityInChunkDataHandle` 时，不代表每个游戏 Processor 都要手工构造它们。它们存在的价值，是让上层的 Query 和 Context 能够安全、高效地落到 Chunk 内存。

### 军队游戏需要哪些 Fragment 和 Tag

一套可能的数据设计如下：

```cpp
// 每名士兵各自拥有的数据
FArmyPositionFragment       // 世界位置
FArmyVelocityFragment       // 当前速度
FArmyHealthFragment         // 当前生命值
FArmyMoraleFragment         // 士气
FArmyMoveTargetFragment     // 行军目标
FArmyAttackTargetFragment   // 当前攻击目标实体
FArmyFormationSlotFragment  // 在阵型中的槽位与偏移

// 只有“有/没有”，不保存具体数值
FArmyInfantryTag            // 步兵
FArmyCavalryTag             // 骑兵
FArmyArcherTag              // 弓箭手
FArmyDeadTag                // 已死亡
FArmyRetreatingTag          // 正在撤退
FArmyNeedsTargetTag         // 需要重新索敌

// 大量同兵种士兵共用
FArmyUnitConfigConstSharedFragment
// 包含 MoveSpeed、AttackRange、BaseDamage、Armor 等兵种配置

// 每个 Chunk 一份
FArmySimulationChunkFragment
// 包含 SpatialCell、SimulationLOD、bSimulationEnabled 等批次状态
```

这里的重点不是类型名字，而是“状态变化频率”和“共享层级”：

| 数据 | 推荐形式 | 原因 |
|---|---|---|
| 当前生命值 | Entity Fragment | 每个士兵不同，并且战斗中频繁变化 |
| 是否死亡 | Tag | 只需表达有/无，并且死亡后需要进入不同处理流程 |
| 步兵基础移速 | ConstShared Fragment | 同兵种大量士兵相同，没必要每人复制一份 |
| 当前模拟 LOD | Chunk Fragment | 常用于整批降频或跳过模拟 |
| 行军目标 | Entity Fragment 或编队共享数据 | 单兵寻路时每人一份；整支编队共用目标时可以提升共享层级 |

例如步兵、坦克和实验单位的基础参数可以做成只读共享 Fragment：

```cpp
USTRUCT()
struct FArmyUnitConfigConstSharedFragment : public FMassConstSharedFragment
{
    GENERATED_BODY()

    float MaxSpeed = 450.0f;
    float AttackRange = 300.0f;
    float BaseDamage = 10.0f;
    float Armor = 0.0f;
};
```

移动查询声明需要它，并在每个当前 Chunk 读取一次：

```cpp
// 声明：查询必须匹配拥有速度 Fragment 的实体，并且当前 Processor 会修改速度。
// 如果这里声明成 ReadOnly，后面的 GetMutableFragmentView 会触发运行时检查。
MovementQuery.AddRequirement<FArmyVelocityFragment>(
    EMassFragmentAccess::ReadWrite,
    EMassFragmentPresence::All);

// 声明：匹配的实体必须带有 FArmyUnitConfigConstSharedFragment。
// All 表示这是必需数据；某个 Archetype/Chunk 没有它，就不会进入本查询。
MovementQuery.AddConstSharedRequirement<FArmyUnitConfigConstSharedFragment>(
    EMassFragmentPresence::All);

// 遍历 MovementQuery 匹配到的全部 Archetype 和 Chunk。
// 传入的 Lambda 每次处理一个 Chunk 或一个连续 Subchunk，而不是单个实体。
MovementQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& ChunkContext)
{
    // 获取当前 Chunk 绑定的只读共享配置。
    // 同一个 UnitConfig 实例可以被大量同模板单位共享，因此这里取得的是引用，
    // 不是与实体数量相等的数组，也不应该在此处修改它。
    const FArmyUnitConfigConstSharedFragment& UnitConfig =
        ChunkContext.GetConstSharedFragment<FArmyUnitConfigConstSharedFragment>();

    // 获取当前 Chunk 内所有单位的可写速度数组视图。
    // Velocities.Num() 通常等于 ChunkContext.GetNumEntities()，
    // 下标 i 对应当前批次中的第 i 个单位。
    TArrayView<FArmyVelocityFragment> Velocities =
        ChunkContext.GetMutableFragmentView<FArmyVelocityFragment>();

    // 连续遍历当前 Chunk 的速度数据，CPU 缓存利用率高。
    for (FArmyVelocityFragment& Velocity : Velocities)
    {
        // 保留当前速度方向，但把速度长度限制在该兵种/单位模板的最大速度内。
        // 步兵、坦克、舰船可以绑定不同的 UnitConfig，从而使用不同 MaxSpeed。
        Velocity.Value = Velocity.Value.GetClampedToMaxSize(UnitConfig.MaxSpeed);
    }
});
```

这样一万个同模板步兵不必各自保存 `MaxSpeed/AttackRange/BaseDamage`。但当前弹药、生命值这类每单位不同且高频变化的数据不应放进共享 Fragment。

### 先决定 Entity 粒度：单位实体与编队实体应该分层

《全面战争》和《最高指挥官》的单位粒度并不相同，不能照搬同一套 Entity 设计：

| 游戏形态 | 建议的主要 Entity 粒度 | 说明 |
|---|---|---|
| 《全面战争》式近景战斗 | 单兵 Entity + 编队 Entity | 单兵负责局部位置、动画状态、生命值；编队实体负责阵型、整体目标、士气汇总和路径 |
| 《最高指挥官》式单位战斗 | 每台载具/飞机/舰船一个 Entity | 单位本身就是主要模拟对象；军团/排级 Entity 可负责高层命令和路径协调 |
| 超远距离战略模拟 | 军团或编队聚合 Entity | 不必继续模拟每名士兵的碰撞与攻击；保存兵力、补给、平均士气和战略位置 |
| 炮弹/导弹密集场景 | 可将弹体做成轻量 Entity | 位置、速度、寿命、伤害等数据非常适合连续批处理；近景特效与模拟数据分离 |

一个实用的《全面战争》式层级可以是：

```text
ArmyEntity（军队/玩家控制层）
└─ FormationEntity（编队命令、阵型、路径、汇总士气）
   ├─ SoldierEntity 0（位置、生命值、动画状态、槽位）
   ├─ SoldierEntity 1
   └─ SoldierEntity N
```

成员实体可以保存一个轻量链接 Fragment，而不是复制整份编队状态：

```cpp
USTRUCT()
struct FArmyFormationMemberFragment : public FMassFragment
{
    GENERATED_BODY()

    FMassEntityHandle FormationEntity;
    int32 SlotIndex = INDEX_NONE;
    FVector LocalOffset = FVector::ZeroVector;
};

USTRUCT()
struct FArmyFormationCommandFragment : public FMassFragment
{
    GENERATED_BODY()

    FVector Destination = FVector::ZeroVector;
    FVector Facing = FVector::ForwardVector;
    float DesiredWidth = 2000.0f;
};
```

编队 Processor 低频更新编队目标和各槽位；单兵移动 Processor 高频读取已经展开到成员 Fragment 的目标位置。这样可以避免每名士兵都运行昂贵的全局寻路，又能保留近景单位运动。

需要注意：`FMassEntityHandle` 链接只表达身份，目标实体仍可能被销毁或迁移。实际访问时必须通过 EntityManager/链接查询提供的安全 API 验证，不能把目标 Fragment 指针长期缓存。

Mass 也不应该接管项目里的所有对象：

| 适合 Mass | 更适合其他结构 |
|---|---|
| 数万普通士兵、载具、炮弹、资源运输单位 | 几十个国家/阵营的宏观经济数据，可由专门 Subsystem 管理 |
| 大量单位重复执行移动、索敌、伤害、LOD | UI 面板、菜单状态和编辑器资产 |
| 可以数据化、批处理、分级更新的模拟状态 | 玩家直控英雄、强交互载具等少量复杂 Actor |

现实方案通常是混合架构：Mass 承载规模化模拟，Actor/Subsystem 承载少量复杂行为和全局服务，两者通过明确的桥接 Processor 或消息/命令接口交换数据。

### 模拟实体与画面表现必须分离

同屏很多单位时，Mass Entity 不应该天然等同于一个 Actor：

```text
权威模拟层：Entity + Fragment + Processor
        ↓ 输出 Transform/状态
表现层：ISM / MassRepresentation / 动画实例 / 少量 Actor
```

可按距离或重要性使用分级方案：

| 层级 | 模拟内容 | 表现方式示例 |
|---|---|---|
| 近景高精度 | 单兵移动、局部避障、攻击判定、动画状态 | Actor/Character 或高质量 Mass Representation |
| 中景简化 | 编队槽位移动、简化碰撞、较低频索敌 | ISM/批量动画，降低 Processor 更新频率 |
| 远景战略 | 编队或军团聚合数据，不模拟单兵接触 | 图标、低模实例或完全不生成单兵表现 |

`FMassEntityQuery` 在这里负责选择不同精度层的实体。例如高精度攻击 Query 要求 `FArmyHighSimulationTag`，远景汇总 Query 要求 `FArmyLowSimulationTag`。LOD 切换改变 Tag 时会发生 Archetype 迁移，应批量、低频处理，避免单位在阈值附近每帧来回迁移。

### 推荐的项目模块边界

对于这类规模的项目，不建议把所有 Fragment、Processor、表现和战略 AI 都塞进一个模块：

| 模块示例 | 负责内容 | 建议依赖 |
|---|---|---|
| `ArmyMassCore` | 单位/编队 Fragment、Tag、共享配置、公共数据契约 | `Core`、`MassEntity` |
| `ArmyMassSimulation` | 移动、伤害、士气、补给、死亡和 LOD Processor | `ArmyMassCore`、必要的运行时导航模块 |
| `ArmyMassAI` | 战略 AI、军团规划、命令生成；输出目标和命令 Fragment | `ArmyMassCore`，尽量不反向依赖表现层 |
| `ArmyMassRepresentation` | ISM、动画、Actor 代理和可见性同步 | `ArmyMassCore`、`MassRepresentation` 等表现模块 |
| `ArmyMassEditor` | 调试可视化、数据检查器和编辑器工具 | 上述 Runtime 模块；仅 Editor 构建加载 |

核心方向应保持为：

```text
战略 AI ──写命令数据──▶ Core 数据契约 ◀──读取状态── Simulation
                                      │
                                      └──▶ Representation
```

模拟层不应直接依赖 UI，核心 Fragment 也不应持有表现 Actor 的强引用。这样远景或服务器模式可以关闭表现模块而继续运行权威模拟，也能避免模块循环依赖。

### Archetype 不是“兵种类”，而是“当前拥有的数据组合”

同一个步兵在战斗中可能迁移经过多个 Archetype：

| 当前状态 | Fragment/Tag 组合示例 | 哪些 Processor 会处理它 |
|---|---|---|
| 正常行军步兵 | Position + Velocity + Health + MoveTarget + InfantryTag | 移动、士气、表现 |
| 进入战斗的步兵 | 上述数据 + AttackTarget | 移动、攻击、士气、表现 |
| 正在撤退的步兵 | Position + Velocity + Health + MoveTarget + InfantryTag + RetreatingTag | 撤退移动、士气、表现 |
| 死亡步兵 | Position + InfantryTag + DeadTag | 尸体表现、延迟清理 |

因此，不要设计一个庞大的 `FSoldierFragment`，把位置、生命值、武器、士气和所有状态全塞进去。那会让每个 Processor 都被迫加载大量无关数据，也会失去按组成筛选的价值。

```text
活着且有目标
Position + Velocity + Health + AttackTarget + InfantryTag
                           │ Health 归零，延迟添加 DeadTag/移除战斗数据
                           ▼
死亡状态
Position + InfantryTag + DeadTag
```

添加或删除 Fragment/Tag 会改变实体组成。Mass 会把士兵从旧 Archetype 搬到新 Archetype，所以这类操作必须避开正在遍历 Chunk 的时刻，通常通过 Deferred Command 排队执行。

### Chunk 为什么能让三万士兵跑得快

以“正常行军步兵”Archetype 为例，Chunk 内不是保存一排完整的 Soldier 对象，而是按数据类型分别连续保存：

```text
Entity       [E0][E1][E2][E3][E4]...
Position     [P0][P1][P2][P3][P4]...
Velocity     [V0][V1][V2][V3][V4]...
Health       [H0][H1][H2][H3][H4]...
MoveTarget   [M0][M1][M2][M3][M4]...
```

移动 Processor 只申请 Position、Velocity 和 MoveTarget。执行时 CPU 连续读取这三列，不需要触碰 Health、Morale 或 Weapon。`Position[Index]`、`Velocity[Index]`、`MoveTarget[Index]` 永远属于同一名士兵。

这就是 ECS 的核心收益：**按系统需要的数据列批量处理，而不是按对象把所有数据都搬进 CPU。**

### 一帧战斗是怎样由多个 Processor 接力完成的

```text
1. FormationProcessor
   根据军团命令，为士兵写入 MoveTarget / FormationSlot

2. MovementProcessor
   Query：有 Position + Velocity + MoveTarget，没有 DeadTag
   结果：更新活着士兵的位置

3. TargetAcquisitionProcessor
   Query：有 Position + Weapon，带 NeedsTargetTag，没有 DeadTag
   结果：选择敌人，写入 AttackTarget，延迟移除 NeedsTargetTag

4. AttackProcessor
   Query：有 Position + AttackTarget + Weapon，没有 DeadTag
   结果：计算命中和伤害，写入目标伤害队列或战斗结果

5. DamageProcessor
   Query：有 Health + PendingDamage，没有 DeadTag
   结果：扣血；生命值归零时延迟添加 DeadTag

6. Death/RepresentationProcessor
   Query：带 DeadTag
   结果：播放死亡表现，之后延迟销毁或转为廉价尸体表示
```

Processor 之间不是通过互相调用来组成巨型继承链，而是通过 Fragment/Tag 接力：上一个 Processor 写状态，下一个 Processor 用 Query 发现状态并继续处理。这种“数据驱动接力”正是 ECS 架构与传统 Actor 面向对象架构最明显的区别。

### Query 和 ExecutionContext 在这场战斗中的位置

以移动系统为例：

```cpp
MovementQuery.AddRequirement<FArmyPositionFragment>(
    EMassFragmentAccess::ReadWrite,
    EMassFragmentPresence::All);

MovementQuery.AddRequirement<FArmyVelocityFragment>(
    EMassFragmentAccess::ReadWrite,
    EMassFragmentPresence::All);

MovementQuery.AddRequirement<FArmyMoveTargetFragment>(
    EMassFragmentAccess::ReadOnly,
    EMassFragmentPresence::All);

MovementQuery.AddTagRequirement<FArmyDeadTag>(
    EMassFragmentPresence::None);
```

这不是在“获取几个数组”，而是在发布一条非常明确的军令：

> 找出所有有位置、有速度、有行军目标并且还活着的单位。我会修改位置和速度，只读取目标。

真正进入回调时，`FMassExecutionContext` 已经代表当前这一箱匹配士兵：

```cpp
// 遍历 MovementQuery 当前匹配到的所有 Archetype 与 Chunk。
MovementQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& ChunkContext)
{
    // 获取当前 Chunk 中所有单位的位置 Fragment，并取得可写数组视图。
    TArrayView<FArmyPositionFragment> Positions = ChunkContext.GetMutableFragmentView<FArmyPositionFragment>();

    // 获取当前 Chunk 中所有单位的速度 Fragment，并取得可写数组视图。
    TArrayView<FArmyVelocityFragment> Velocities = ChunkContext.GetMutableFragmentView<FArmyVelocityFragment>();

    // 获取当前 Chunk 中所有单位的移动目标 Fragment；这里只读取目标，因此使用只读视图。
    const TConstArrayView<FArmyMoveTargetFragment> Targets = ChunkContext.GetFragmentView<FArmyMoveTargetFragment>();

    // 缓存本帧时间，后面用它把“速度”积分成“这一帧的位移”。
    const float DeltaTime = ChunkContext.GetDeltaTimeSeconds();

    // 按相同下标遍历当前 Chunk 的实体；三个数组的同一 Index 对应同一个单位。
    for (int32 Index = 0; Index < ChunkContext.GetNumEntities(); ++Index)
    {
        // 用“目标位置减当前位置”计算从单位指向目标的方向向量。
        const FVector ToTarget = Targets[Index].Value - Positions[Index].Value;

        // 安全归一化方向向量，再乘以示例速度 450；正式项目应改用 UnitConfig.MaxSpeed。
        Velocities[Index].Value = ToTarget.GetSafeNormal() * 450.0f;

        // 根据“位移 = 速度 × 帧时间”更新当前位置，使移动速度不受帧率直接影响。
        Positions[Index].Value += Velocities[Index].Value * DeltaTime;
    }
});
```

此时各结构的职责非常清楚：

- Query 负责决定哪些士兵进入；
- Archetype 缓存负责找到符合条件的士兵仓库；
- Chunk 负责提供连续的一批数据；
- ExecutionContext 负责把当前批次的数据视图交给回调；
- Lambda 只负责移动公式；
- 如果到达目标后要删除 `MoveTarget`，则通过 `Context.Defer()` 排队，因为那会改变 Archetype。

## 全文一句话总结

这对文件回答的问题是：**“给所有满足条件的实体跑一段逻辑”，引擎到底怎样把这句话变成连续内存上的批处理。**

`FMassEntityQuery` 先声明“我要哪些数据、读还是写”，再缓存匹配的 Archetype 及其布局翻译表。执行时，`FMassExecutionContext` 被反复绑定到不同 Chunk，向你的 Lambda 暴露当前批次的 Fragment 数组、实体列表、帧时间和延迟命令缓冲。

因此，下面这行看似简单的调用：

```cpp
EntityQuery.ForEachEntityChunk(Context, Fn);
```

实际上完成了五件事：**检查需求 → 找到匹配仓库 → 选择 Chunk → 绑定连续数据视图 → 调用你的函数。**

## 先看全景：一次调用的完整链路

```
Processor.Execute(EntityManager, Context)
  └─ EntityQuery.ForEachEntityChunk(Context, Fn)          ← 你写的那行
       ├─ FScopedEntityQueryContext：查询压栈，缓存要用的子系统
       ├─ CacheArchetypes()：版本号对上就跳过；对不上才重新匹配原型
       ├─ Context.ApplyFragmentRequirements：把"要什么数据"挂上腰带
       └─ for 每个匹配的原型（按 OrderedArchetypeIndices 顺序）
            └─ ArchetypeData.ExecuteFunction(Context, Fn, 翻译表, 区间, Chunk过滤)
                 └─ for 每个 Chunk（按区间迭代器）
                      ├─ 共享 Fragment：哈希变了才重绑
                      ├─ 挂 Chunk 序号、绑 ChunkFragment
                      ├─ ChunkCondition 闸门（可选）
                      ├─ BindEntityRequirements：切出本段实体的数据视图
                      └─ Fn(Context)   ←←← 你的 lambda 在这里跑
```

记住这张图，下面逐段对号入座。

---

## 逐段精读

### 1. 查询的本体：两份需求清单

```cpp
struct FMassEntityQuery : public FMassFragmentRequirements, public FMassSubsystemRequirements
```

查询自己几乎没有逻辑，它的"内容"继承自两个基类（都在 `MassRequirements.h`）：**Fragment 需求清单**和**子系统需求清单**。声明需求用的就是导读示例里那两行：

```cpp
EntityQuery.AddRequirement<FArmyHealthFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
EntityQuery.AddTagRequirement<FArmyCombatUnitTag>(EMassFragmentPresence::All);
```

两个枚举的语义值得背下来：

- `EMassFragmentAccess`：`ReadOnly` / `ReadWrite`。它不是权限装饰——引擎真会拦（见第 6 节的宏），而且读写声明影响多线程调度（两查询写同一数据就不能并行）。
- `EMassFragmentPresence`：`All`（必须有）、`Any`（至少有清单里一个）、`None`（必须没有——比如排除 `DeadTag` 或 `RetreatingTag`）、`Optional`（有则绑上，没有也能跑）。

头文件开头的注释还有一条硬规矩：合法查询**至少要有一个 All/Any/Optional 的 Fragment 需求**，光有 None 不算数——不然一个空查询会匹配世界上所有原型，那是笔糊涂账。

把 Presence 当成 SQL 条件会更容易理解：

| Presence | 类似 SQL | 实际含义 | 常见用途 |
|---|---|---|---|
| `All` | `AND Has(A)` | 每个匹配 Archetype 都必须拥有该类型 | 移动必须有位置和速度 |
| `Any` | `Has(A) OR Has(B)` | 同一组 Any 条件中至少满足一个 | 多种目标来源任选其一 |
| `None` | `AND NOT Has(A)` | 必须不拥有该类型 | 排除死亡、暂停、待销毁实体 |
| `Optional` | `LEFT JOIN A` | 有也能匹配，没有也能匹配 | 有编队偏移就使用，否则走默认逻辑 |

这里有个新手常踩的坑：**Presence 是 Archetype 级匹配，不是 Lambda 里逐实体判断。**同一个 Archetype 内的实体组成相同，因此如果当前 Archetype 有某个 Optional Fragment，这次 Chunk 回调里的所有实体都有它；如果没有，则整个 Chunk 都没有。

Access 也不是注释：

```cpp
// 只读声明只能配 GetFragmentView
EntityQuery.AddRequirement<FVelocityFragment>(EMassFragmentAccess::ReadOnly);

// 需要修改时必须声明 ReadWrite，才能调用 GetMutableFragmentView
EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
```

> **人话版**：`ConfigureQueries` 就是在开工前填写“我要处理谁、我要拿什么、哪些东西会被我修改”。筛选、依赖排序和运行时访问检查都依赖这张申请单。

### 2. CacheArchetypes：版本号增量缓存

```cpp
bool bUpdateArchetypes = CachedEntityManager->GetArchetypeDataVersion() != LastUpdatedArchetypeDataVersion;

if (EntitySubsystemHash != InEntityManagerHash || HasIncrementalChanges())
{
    bUpdateArchetypes = true;
    // 全量重置：ValidArchetypes、OrderedArchetypeIndices、ArchetypeFragmentMapping 清空
    ...
}
if (bUpdateArchetypes)
{
    TArray<FMassArchetypeHandle> NewValidArchetypes;
    CachedEntityManager->GetMatchingArchetypes(*this, NewValidArchetypes, LastUpdatedArchetypeDataVersion);
    // ↑ 只拿版本号之后新出现的原型——增量
    ...
}
```

每帧调用时绝大多数时候第一行版本号对得上，直接跳过——所以"缓存匹配结果"这事的均摊成本近乎为零。什么会打破版本号？新原型诞生（有实体第一次用上某个新 Fragment 组合）。而换管理器（哈希不等）或需求清单变过，才触发全量重来。

熟悉的配方又出现了：**值 + 版本号，先对账再干活**。实体句柄、Archetype 句柄、Chunk 地址、查询缓存，全框架第四次用同一招。看懂这一处，同类代码都能秒读。

不要把“缓存 Archetype”理解成“缓存实体列表”。查询缓存的是**哪些组成匹配**，不是这一帧具体有多少实体。因此同一个 Archetype 中新增或删除普通实体时，Query 不需要重新匹配类型组合；真正执行时直接读取该 Archetype 当前拥有的 Chunk 即可。只有新的组成出现、查询需求改变或切换 EntityManager，才需要更新匹配清单。

```text
第 1 帧：发现 Archetype A、B 匹配 → 缓存 A、B 和两张布局翻译表
第 2 帧：A 新增 300 个实体        → 匹配关系没变，直接复用
第 3 帧：首次出现新组成 C          → 只检查版本号之后新增的 C
第 4 帧：查询新增一个 Requirement  → 清空并全量重建查询缓存
```

> **人话版**：Query 记住的是“去哪几个仓库”和“每个仓库的目标货架在哪”，不会傻到每帧重新逛完整个仓库区。

放到大战略项目里，移动 Query 可能同时匹配步兵、坦克和海军单位，而它们的完整 Fragment 组合不同：

```text
MovementQuery 需求：Position + Velocity

步兵 Archetype 布局：Position[0] Health[1] Morale[2] Velocity[3] ...
坦克 Archetype 布局：Health[0] Armor[1] Position[2] Ammo[3] Target[4] Velocity[5] ...

缓存期生成：
步兵 Mapping：Position → 0，Velocity → 3
坦克 Mapping：Position → 2，Velocity → 5
```

这就是 `FMassQueryRequirementIndicesMapping` 的项目价值：同一个移动 Processor 可以处理多种单位组成，执行期却不必按类型名重新搜索每个 Archetype 的列位置。

### 3. ForEachEntityChunk 主循环

`ForEachEntityChunk` 本身不负责移动、攻击或扣血。它是一个**批处理调度入口**，主要回答三个问题：

1. 当前 Query 的执行环境和子系统是否已经准备好？
2. 本次应该处理全部匹配实体，还是只处理外部指定的一小批实体？
3. 每个 Archetype 应该使用哪张“查询需求 → 实际内存列”的翻译表？

下面是保留主干逻辑的**阅读版伪代码**。日志、统计、限制器等细节被省略，其中“区间”等中文参数只是帮助理解，不能直接复制编译。

```cpp
// Query 的执行入口。以《全面战争》式战场的“全军移动处理器”为例：
// ExecutionContext 保存这一帧的执行现场；ExecuteFunction 是稍后处理一批士兵的移动 Lambda。
void FMassEntityQuery::ForEachEntityChunk(FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
    // 告诉 Context：“现在正在执行 MovementQuery”，并准备它声明要访问的外部系统。
    // 例如移动逻辑可能还要读取导航、地形代价或战场空间划分子系统。
    // 该对象采用 RAII：无论正常结束还是中途 return，都会自动恢复 Context 原来的状态。
    FScopedEntityQueryContext ScopedQueryContext(*this, ExecutionContext);

    // 如果执行现场不合法，或者 Query 声明的导航/空间等子系统没有准备好，
    // 就不能让一部分军队在缺少必要数据的情况下继续移动。
    if (ScopedQueryContext.IsSuccessfullySetUp() == false)
    {
        // 放弃本次执行；任何步兵、骑兵或火炮 Chunk 都不会进入移动 Lambda。
        return;
    }

    // 省略日志、统计、执行限制器等与主分支无关的代码。
    // ...

    // 确认 MovementQuery 当前能匹配哪些 Archetype。
    // 例如“可移动步兵”“可移动骑兵”“可移动火炮”都可能匹配，而尸体不会匹配。
    // 如果这一帧没有产生新的实体组成，缓存版本不变，这一步通常会快速跳过。
    CacheArchetypes();

    // Context 中带有 EntityCollection，说明这次不是处理全战场，
    // 而是只处理外部给出的一小批实体，例如“刚被一发炮弹命中的步兵”。
    if (ExecutionContext.GetEntityCollection().IsEmpty() == false)
    {
        // Collection 只属于一个 Archetype。
        // 在 Query 的匹配结果中检查这个 Archetype 是否真的符合当前处理条件。
        const int32 ArchetypeIndex = ValidArchetypes.Find(ArchetypeHandle);

        // 没找到，说明这批单位缺少 Query 要求的数据，或带有被 Query 排除的 Tag。
        // 例如 DamageQuery 要求 Health，但名单实际来自已经转成“尸体 Archetype”的实体。
        if (ArchetypeIndex == INDEX_NONE)
        {
            // 即使名单是外部指定的，也不能绕过 Query 的数据契约，因此拒绝执行。
            return;
        }

        // 按 Query 声明，在 Context 中准备 Fragment View 及其读写权限。
        // 例如炮击伤害结算会准备 Health（可写）、Armor（只读）等 View 插槽；
        // 此时只是把“要用哪些数据”登记好，还没有绑定某个 Chunk 的实际数组。
        ExecutionContext.ApplyFragmentRequirements(*this);

        // 只遍历 Collection 描述的那些连续实体区间，并对每段调用伤害/移动 Lambda。
        // 同一 Archetype 中没有被炮弹命中的其他步兵不会被扫描，更不会波及全战场军队。
        ArchetypeData.ExecuteFunction(ExecutionContext, ExecuteFunction,
            GetRequirementsMappingForArchetype(ArchetypeHandle), 区间, ChunkCondition);
    }
    else
    {
        // 没有局部名单，说明要走常规全量路径。
        // 例如 MovementProcessor 每帧更新战场上所有仍然存活且拥有移动数据的单位。
        ExecutionContext.ApplyFragmentRequirements(*this);

        // 逐个处理 Query 匹配的单位布局：先处理某类步兵，再处理骑兵、火炮等。
        // 这些 Archetype 拥有不同附加数据，但都具备本 Query 所需的 Fragment。
        for (const int32 ArchetypeIndex : OrderedArchetypeIndices)
        {
            // 遍历当前兵种 Archetype 的全部有效 Chunk。
            // 每进入一个 Chunk，就把 Position、Velocity、MoveTarget 等连续数组绑定到 Context，
            // 然后调用一次用户 Lambda，批量更新这个 Chunk 中的数百个单位。
            // Mapping 已在缓存阶段记住各 Fragment 在该 Archetype 中的列号，
            // 因此执行期无需为每个单位按类型名查找组件。
            ArchetypeData.ExecuteFunction(ExecutionContext, ExecuteFunction,
                ArchetypeFragmentMapping[ArchetypeIndex], ChunkCondition);

            // 当前兵种布局处理完后，清除指向其 Chunk 内存的临时 View。
            // 例如处理完步兵后必须卸下“步兵数组”，再去绑定骑兵或火炮的数组；
            // 否则下一个 Archetype 可能误用上一个 Archetype 的旧内存地址。
            ExecutionContext.ClearFragmentViews(*this);
        }
    }
}
```

### 放进《全面战争》式项目里理解

假设移动 Query 的条件是：

```text
需要：Position（写）+ Velocity（写）+ MoveTarget（读）
排除：DeadTag
```

战场上可能同时存在这些 Archetype：

```text
步兵：Position + Velocity + MoveTarget + Health + Morale + InfantryTag
骑兵：Position + Velocity + MoveTarget + Health + Stamina + CavalryTag
火炮：Position + Velocity + MoveTarget + Health + Ammo + ArtilleryTag
尸体：Position + DeadTag
```

步兵、骑兵和火炮的完整组成不同，但都拥有移动 Query 要求的数据，所以都会被匹配；尸体缺少速度和目标，同时带有 `DeadTag`，不会进入查询。

#### 路径 A：没有指定 Collection——更新整个战场

常规 `MovementProcessor` 每次执行：

```cpp
// 没有传入局部实体名单，因此遍历全部匹配 Archetype 的全部 Chunk。
MovementQuery.ForEachEntityChunk(Context, MoveUnitsFunction);
```

内部大致按下面的顺序运行：

```text
步兵 Archetype 的全部 Chunk
        ↓
骑兵 Archetype 的全部 Chunk
        ↓
火炮 Archetype 的全部 Chunk
```

这适用于移动、士气恢复、补给消耗和表现同步等“所有符合条件单位都要执行”的系统。

#### 路径 B：指定 Collection——只处理一次炮击命中的单位

一发炮弹爆炸后，空间查询可能得到 300 个命中 EntityHandle。伤害系统不必为这次爆炸扫描全战场，而是把命中实体按 Archetype 整理成 Collection：

```text
命中名单 300 个单位
├─ 步兵 Collection：180 个
├─ 骑兵 Collection：70 个
└─ 火炮 Collection：50 个

DamageQuery 只遍历这三份 Collection 描述的连续范围
```

一份 `FMassArchetypeEntityCollection` 只对应一个 Archetype。混合兵种名单通常先交给 `FEntityCollection`，再重建成多份按 Archetype 分组的 Collection。

Collection 不是“命令缓冲专用入口”。炮击伤害、框选命令、局部 Buff、触发器事件和任务目标都可以使用这条局部路径。

### 三个真正需要记住的结论

1. **翻译表在这里上岗**。上一箱讲的 `FMassQueryRequirementIndicesMapping`，此刻作为 `ArchetypeFragmentMapping[ArchetypeIndex]` 传下去——每个（查询 × 原型）一张，需求到布局的翻译在缓存期就办完了。
2. **Collection 只缩小范围，不改变规则**。即使一个单位出现在炮击名单里，它所在的 Archetype 仍必须满足 `DamageQuery`；没有 Health 或被 Query 排除的实体不会强行进入 Lambda。
3. **先缓存，再安装需求**。当前实现中的缓存过程可能整理需求顺序，因此必须先 `CacheArchetypes()`，再让 `ApplyFragmentRequirements()` 按最终顺序准备 Context 视图。

两条执行路径可以这样选：

| 调用意图 | 输入范围 | 典型场景 |
|---|---|---|
| 普通 Processor Tick | 查询匹配的全部 Archetype/Chunk | 更新所有正在战术模拟的单位 |
| 指定 `FMassArchetypeEntityCollection` | 某个 Archetype 内的一组连续范围 | 炮击伤害、框选命令、局部 Buff、事件目标 |

第二条路径仍会验证 Collection 的 Archetype 是否满足 Query。也就是说，Collection 只是缩小“处理范围”，不会绕过 Query 声明的数据契约。

> **一句话版**：普通路径是“全战场所有符合条件的单位都处理”；Collection 路径是“只处理这份局部名单，但名单仍必须通过 Query 条件检查”。

### 4. ExecuteFunction：每个 Chunk 的绑定仪式

这是查询把接力棒交给原型数据的一棒（`Private/MassArchetypeData.cpp`）：

```cpp
for (FMassArchetypeChunkIterator ChunkIterator(EntityRangeContainer); ChunkIterator; ++ChunkIterator)
{
    FMassArchetypeChunk& Chunk = Chunks[ChunkIterator->ChunkIndex];
    const int32 SubchunkLength = ChunkIterator->Length > 0 ? ChunkIterator->Length
                                    : (Chunk.GetNumInstances() - ChunkIterator->SubchunkStart);
    if (SubchunkLength)
    {
        const uint32 SharedFragmentValuesHash = GetTypeHash(Chunk.GetSharedFragmentValues());
        if (PrevSharedFragmentValuesHash != SharedFragmentValuesHash)
        {
            PrevSharedFragmentValuesHash = SharedFragmentValuesHash;
            BindConstSharedFragmentRequirements(RunContext, Chunk.GetSharedFragmentValues(), ...);
            BindSharedFragmentRequirements(RunContext, Chunk.GetMutableSharedFragmentValues(), ...);
        }
        RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
        BindChunkFragmentRequirements(RunContext, RequirementMapping.ChunkFragments, Chunk);

        if (!ChunkCondition || ChunkCondition(RunContext))
        {
            BindEntityRequirements(RunContext, RequirementMapping.EntityFragments, Chunk,
                ChunkIterator->SubchunkStart, SubchunkLength);
            Function(RunContext);
        }
    }
}
```

注意绑定顺序就是数据的"共享层级"从粗到细：**共享 Fragment（按哈希去重绑定，同值的 Chunk 连着排就不用重绑）→ ChunkFragment → 实体 Fragment（切出偏移 + 长度的视图）→ 你的函数**。上一箱说"Length=0 表示到 Chunk 末尾"，`SubchunkLength` 那行就是它的兑现处。`SetCurrentChunkSerialModificationNumber` 挂的就是上上箱说的 ChunkSerialNumber——迭代期间的结构变更检测靠它对账。

把内存绑定展开看，就是下面这件事：

```text
Chunk 原始布局（SoA）

EntityHandles : [E10][E11][E12][E13][E14]
Transform     : [T10][T11][T12][T13][T14]
Velocity      : [V10][V11][V12][V13][V14]
                  ↑-------------↑
SubchunkStart = 1，Length = 3

绑定进 Context 后：
GetEntities()                    → [E11, E12, E13]
GetMutableFragmentView<Transform>() → [T11, T12, T13]
GetFragmentView<Velocity>()         → [V11, V12, V13]
```

这些数组视图下标严格对齐，所以 `Transforms[i]`、`Velocities[i]` 和 `Context.GetEntity(i)` 描述的是同一个实体。Lambda 只需要循环 `0..GetNumEntities()-1`，不必自己计算 Chunk 偏移。

共享层级的区别也可以直接记成：

| 类型 | 数据份数 | 适合保存什么 |
|---|---|---|
| Entity Fragment | 每实体一份 | 位置、速度、生命值 |
| Chunk Fragment | 每 Chunk 一份 | 本 Chunk 的粗筛状态、空间格或批次统计 |
| Shared Fragment | 一组实体共享一份 | 队伍配置、单位模板、只读参数 |

> **人话版**：`ExecuteFunction` 的工作不是算玩法，而是把原始 Chunk 内存切成长度一致、下标对齐的安全数组，再把工作台交给你的 Lambda。

### 5. ParallelForEachEntityChunk：并行的真面目

先看两个退路：全局开关 `bAllowParallelExecution` 关着且没带 `Force` 标志，直接退回串行版；查询用了 `GroupBy` 分组排序，也退回串行（源码注释：分组查询的并行"暂不支持"）。

正路的骨架：先把所有要跑的（原型 × 区间）收集成 `Jobs` 列表，然后：

```cpp
if (bAllowParallelCommands)
{
    struct FTaskContext
    {
        TSharedPtr<FMassCommandBuffer> GetCommandBuffer()
        {
            if (!CommandBuffer)
            {
                // 惰性创建：确保命令缓冲在"将要使用它的线程"里出生
                CommandBuffer = MakeShared<FMassCommandBuffer>();
            }
            else
            {
                // ParallelFor 的工人可能在线程间挪窝，强制刷新记录的线程号
                CommandBuffer->ForceUpdateCurrentThreadID();
            }
            return CommandBuffer;
        }
        TSharedPtr<FMassCommandBuffer> CommandBuffer;
    };
    TArray<FTaskContext> TaskContext;

    ParallelForWithTaskContext(TaskContext, Jobs.Num(), [...](FTaskContext& TaskContext, const int32 JobIndex)
    {
        FMassExecutionContext LocalExecutionContext(ExecutionContext, *this, TaskContext.GetCommandBuffer());
        Jobs[JobIndex].Archetype.ExecutionFunctionForChunk(LocalExecutionContext, ...);
        LocalExecutionContext.PopQuery(*this);
    }, ParallelForFlags);

    // 收工后把所有线程的命令缓冲合并回主缓冲
    for (FTaskContext& CommandContext : TaskContext)
    {
        ExecutionContext.Defer().MoveAppend(*CommandContext.GetCommandBuffer());
    }
}
```

四个知识点：

1. **每个工人一份克隆的上下文 + 独立命令缓冲**。多线程下大家各写各的命令队列，互不踩脚，收工再 `MoveAppend` 合并——这是"并行发延迟命令"能成立的全部基础。
2. `ForceUpdateCurrentThreadID` 那行注释很有味道：ParallelFor 的工人线程不是终身制的，命令缓冲记的线程号会过期，得强制刷新。多线程代码里连"记录自己是谁"都要小心。
3. **`bAllowParallelCommands` 是性能开关**（默认开）：处理器不发命令就 `SetParallelCommandBufferEnabled(false)`，省掉每线程命令缓冲的分配。头文件注释把丑话说在前面：关了还敢发命令，直接崩，没有运行时兜底。
4. `AutoBalance` 标志映射成 `EParallelForFlags::Unbalanced`：默认并行是**静态切分**（每线程预分一块，假设各 Chunk 耗时相近）；`AutoBalance` 换成**工作队列**（谁闲谁领，Chunk 大小不均时利用率更高，但领任务的启动开销也更大）。不同战区单位逻辑负载差异很大时，可以测试这个旋钮。

是否值得并行，不要只看实体总数：

| 情况 | 建议 | 原因 |
|---|---|---|
| 实体少、每实体只做几次加减乘除 | 先串行 | 线程调度成本可能比计算还高 |
| Chunk 多、每实体计算较重且互不依赖 | 尝试并行 | 工作容易按 Chunk 拆分 |
| 回调频繁访问 GameThread-only UObject | 保持串行或重构数据 | 并行线程不能随意访问游戏线程对象 |
| 不同 Chunk 工作量差异明显 | 测试 `AutoBalance` | 动态领任务可减少某些线程提前闲置 |
| 回调完全不发 Deferred Command | 关闭并行命令缓冲并实测 | 可减少每线程缓冲的创建成本 |

并行不会自动修复数据竞争。Query 的 ReadOnly/ReadWrite 声明主要帮助 Mass 做处理器依赖分析，但你的 Lambda 捕获的外部对象、全局数组、静态变量仍需自行保证线程安全。

> **人话版**：并行版就是把不同 Chunk 分给多个工人。每个工人有自己的工作台和变更单，但如果大家同时去改 Lambda 外面的同一个普通变量，Mass 也救不了你。

### 6. FMassExecutionContext：工具腰带的四组挂扣

```cpp
TArray<FFragmentView, TInlineAllocator<8>> FragmentViews;            // 每实体
TArray<FChunkFragmentView, TInlineAllocator<4>> ChunkFragmentViews; // 每 Chunk
TArray<FConstSharedFragmentView, TInlineAllocator<4>> ConstSharedFragmentViews;
TArray<FSharedFragmentView, TInlineAllocator<4>> SharedFragmentViews;
```

上下文就是个巡回工具腰带：查询每进一个 Chunk，四组挂扣换上对应数据视图。取数据的 API 全是"在挂扣上找类型再返回视图"：

```cpp
template<typename TFragment>
TArrayView<TFragment> GetMutableFragmentView()
{
    const FFragmentView* View = FragmentViews.FindByPredicate(...);
    CHECK_IF_VALID(View, FragmentType);     // 没声明需求 → 这里炸
    CHECK_IF_READWRITE(View);               // 声明的是只读 → 这里炸
    return MakeArrayView<TFragment>(...);
}
```

两个宏就是 Mass 数据安全的门神，错误信息也写得直白：*"Make sure it has been listed as required"*（没列需求别来取）、*"not bound for writing"*（要写就得声明成 ReadWrite）。**没声明的数据物理上就摸不到，声明的访问模式物理上就越不了权**——这不是代码规范，是结构保证。写传统 Actor 代码时"哪个系统改了这个变量"的破案难题，在这里被需求清单一刀切掉了。

还要注意视图的生命周期：`TArrayView` 不拥有数据，它只是指向当前 Chunk 内存的一扇窗。回调结束或 Context 切换到下一个 Chunk 后，之前拿到的 View 就不应继续保存或使用。

```cpp
// 正确：视图只在当前回调里使用
EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& ChunkContext)
{
    TArrayView<FTransformFragment> Transforms =
        ChunkContext.GetMutableFragmentView<FTransformFragment>();

    for (int32 Index = 0; Index < ChunkContext.GetNumEntities(); ++Index)
    {
        // 使用 Transforms[Index]
    }
});

// 错误思路：把 Transforms 保存到成员变量，下一帧继续使用。
// Chunk 可能已迁移、重排或释放，旧视图没有所有权，也不保证有效。
```

> **人话版**：Context 借给你的都是“当前这一箱”的临时窗口。用完即走，不要把指针或 View 偷偷带出回调。

### 7. 上下文的其他零件

- **QueriesStack（嵌套查询）**：`TInlineAllocator<2>`，注释说查询一般只嵌套一层。处理器在回调里再调另一个查询时压栈/出栈，腰带上的需求随内外查询切换——这就是"你取数据取的是当前查询声明过的那份"的实现。
- **FEntityIterator**：支持 range-for 的实体迭代器。**拷贝构造被显式删除**，注释说拷贝会让"迭代中的 Chunk 变没变"检测复杂化，干脆禁掉等有真实需求再说。加上迭代器自带 SerialNumber 做一致性校验——又是那个思想。
- **Defer()**：`FMassCommandBuffer& Defer() const`——发延迟命令的入口，下一篇主角。`bFlushDeferredCommands` 控制"攒一大波再冲"，避免小冲频繁。
- **DoesArchetypeHaveFragment\<T\>()**：查当前原型的组合表。**Optional 需求的标准搭档**——声明 Optional 后进回调，用它判断"这次到底有没有"，有则走完整逻辑，无则降级。
- **GetDeltaTimeSeconds / GetAuxData / GetSubsystem 一族**：帧时长、跨层附带数据（注释里的 @todo 说想改名 payload）、按声明取子系统（不声明同样取不到，和 Fragment 一个纪律）。

### 8. 三个小件收尾

- **SetChunkFilter**：给每个 Chunk 挂一道运行时闸门（回调返回 false 就跳过整 Chunk），时机在 ChunkFragment 绑好之后、实体数据绑定之前——闸门里能用 Chunk 级数据做粗筛。
- **GroupBy**（5.7 新增）：给原型排序。原型本无次序，但"同兵种的敌我按队列顺序结算伤害"这类需求要确定性顺序时用它。注意第 5 节说过：分组查询暂不能并行。
- **SetArchetypeMatchOverride**（仅编辑器）：塞一个自定义匹配器顶替常规需求匹配。实现是个小技巧——匹配器被要求平凡可复制、16 字节以内，整个 memcpy 进查询内部的定长存储，用类型擦除的函数指针回调。又是"平凡复制"约束换来的自由。

另一个值得留意的痕迹：文件尾一整段 5.6 标记的废弃 API——旧版 `ForEachEntityChunk` 都要传 `FMassEntityManager` 参数，新版全部去掉了，废弃注释说"查询不再绑定特定管理器实例"。看废弃接口的演化方向，能反推 Epic 在把查询往"可跨管理器复用"的形态推。

---

## 实际开发案例 1：地面作战单位移动 Processor

这是最小但完整的使用路径：定义数据 → 声明查询 → 遍历 Chunk → 修改数据。示例类型名是教学用的，项目中应按模块命名调整。

编译前先确认模块依赖中包含 `MassEntity`。如果 Fragment 或 Processor 类型出现在模块的 Public 头文件中，将依赖放入 `PublicDependencyModuleNames`；只在 Private 实现中使用则放入 `PrivateDependencyModuleNames`。常用头文件包括 `MassProcessor.h`、`MassEntityQuery.h`、`MassExecutionContext.h`、`MassEntityTypes.h` 和 `MassCommandBuffer.h`，最终以类型实际使用位置为准保持最小 include。

### 1.1 数据定义

```cpp
USTRUCT()
struct FArmyPositionFragment : public FMassFragment
{
    GENERATED_BODY()

    FVector Value = FVector::ZeroVector;
};

USTRUCT()
struct FArmyVelocityFragment : public FMassFragment
{
    GENERATED_BODY()

    FVector Value = FVector::ZeroVector;
};

USTRUCT()
struct FArmyActiveSimulationTag : public FMassTag
{
    GENERATED_BODY()
};

USTRUCT()
struct FArmyDeadTag : public FMassTag
{
    GENERATED_BODY()
};
```

### 1.2 Processor 声明

```cpp
UCLASS()
class UArmyUnitMovementProcessor : public UMassProcessor
{
    GENERATED_BODY()

protected:
    virtual void ConfigureQueries(
        const TSharedRef<FMassEntityManager>& EntityManager) override;

    virtual void Execute(
        FMassEntityManager& EntityManager,
        FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};
```

### 1.3 填写查询申请单

```cpp
void UArmyUnitMovementProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.AddRequirement<FArmyPositionFragment>(
        EMassFragmentAccess::ReadWrite,
        EMassFragmentPresence::All);

    EntityQuery.AddRequirement<FArmyVelocityFragment>(
        EMassFragmentAccess::ReadOnly,
        EMassFragmentPresence::All);

    EntityQuery.AddTagRequirement<FArmyActiveSimulationTag>(
        EMassFragmentPresence::All);

    EntityQuery.AddTagRequirement<FArmyDeadTag>(
        EMassFragmentPresence::None);

    EntityQuery.RegisterWithProcessor(*this);
}
```

这张申请单翻译成人话：

> 找到所有“处于战术模拟、尚未死亡、拥有位置和速度”的单位。我会修改位置，但只读取速度。

`RegisterWithProcessor(*this)` 不只是登记所有权。Processor 的依赖求解需要汇总这些查询需求，漏掉注册可能导致执行上下文类型检查失败，也会让调度器无法完整了解该 Processor 的数据访问。

### 1.4 批量执行

```cpp
void UArmyUnitMovementProcessor::Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context)
{
    const float DeltaTime = Context.GetDeltaTimeSeconds();

    EntityQuery.ForEachEntityChunk(
        Context,
        [DeltaTime](FMassExecutionContext& ChunkContext)
        {
            TArrayView<FArmyPositionFragment> Positions =
                ChunkContext.GetMutableFragmentView<FArmyPositionFragment>();

            const TConstArrayView<FArmyVelocityFragment> Velocities =
                ChunkContext.GetFragmentView<FArmyVelocityFragment>();

            const int32 NumEntities = ChunkContext.GetNumEntities();
            check(Positions.Num() == NumEntities);
            check(Velocities.Num() == NumEntities);

            for (int32 Index = 0; Index < NumEntities; ++Index)
            {
                Positions[Index].Value += Velocities[Index].Value * DeltaTime;
            }
        });
}
```

这里没有逐个 EntityHandle 查询 Fragment。`Positions` 和 `Velocities` 已经是当前 Chunk 的连续数组，并且下标对齐。循环体只做纯数据运算，这正是 Mass 最擅长的工作形态。

### 1.5 这段代码背后的真实执行

```text
ConfigureQueries
  └─ 形成 Position=写、Velocity=读、Active=有、Disabled=无 的需求

首次 Execute
  └─ CacheArchetypes 找到匹配组成并生成布局映射

每个匹配 Chunk
  ├─ Position 列绑定成可写 TArrayView
  ├─ Velocity 列绑定成只读 TConstArrayView
  └─ Lambda 连续更新这一箱实体
```

## 实际开发案例 2：Optional + None 实现两档编队逻辑

需求：所有活动单位都要向战术目标移动；拥有 `FArmyFormationMemberFragment` 的步兵按编队槽位前进，没有该 Fragment 的独立单位直接移动到目标。死亡实体完全不参与。

查询声明：

```cpp
EntityQuery.AddRequirement<FArmyPositionFragment>(
    EMassFragmentAccess::ReadWrite,
    EMassFragmentPresence::All);

EntityQuery.AddRequirement<FArmyMoveTargetFragment>(
    EMassFragmentAccess::ReadOnly,
    EMassFragmentPresence::All);

EntityQuery.AddRequirement<FArmyFormationMemberFragment>(
    EMassFragmentAccess::ReadOnly,
    EMassFragmentPresence::Optional);

EntityQuery.AddTagRequirement<FArmyDeadTag>(
    EMassFragmentPresence::None);
```

执行逻辑：

```cpp
EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& ChunkContext)
{
    TArrayView<FArmyPositionFragment> Positions =
        ChunkContext.GetMutableFragmentView<FArmyPositionFragment>();

    const TConstArrayView<FArmyMoveTargetFragment> Targets =
        ChunkContext.GetFragmentView<FArmyMoveTargetFragment>();

    const bool bHasFormationMember =
        ChunkContext.DoesArchetypeHaveFragment<FArmyFormationMemberFragment>();

    TConstArrayView<FArmyFormationMemberFragment> FormationMembers;
    if (bHasFormationMember)
    {
        FormationMembers =
            ChunkContext.GetFragmentView<FArmyFormationMemberFragment>();
    }

    for (int32 Index = 0; Index < ChunkContext.GetNumEntities(); ++Index)
    {
        const FVector Offset = bHasFormationMember
            ? FormationMembers[Index].LocalOffset
            : FVector::ZeroVector;

        const FVector DesiredPosition = Targets[Index].Value + Offset;
        Positions[Index].Value = FMath::VInterpTo(
            Positions[Index].Value,
            DesiredPosition,
            ChunkContext.GetDeltaTimeSeconds(),
            5.0f);
    }
});
```

为什么不用每实体 `if (EntityHasFragment)`？因为组成相同的实体本来就在同一个 Archetype。`bHasFormationMember` 对整个当前 Chunk 都成立或都不成立，只需在 Chunk 开头判断一次。

为什么死亡用 `None` 而不是 Lambda 内判断？因为 `None` 会让带 `FArmyDeadTag` 的 Archetype 整体不进入执行链路，连 Fragment 绑定和循环成本都省掉。

## 实际开发案例 3：生命值归零后添加死亡 Tag

修改数值 Fragment 不会改变实体组成，可以在当前 View 中直接写。但添加/删除 Fragment 或 Tag 会让实体迁移到新 Archetype，属于结构性修改，应该通过 `Context.Defer()` 延迟执行。

```cpp
EntityQuery.AddRequirement<FArmyHealthFragment>(
    EMassFragmentAccess::ReadWrite,
    EMassFragmentPresence::All);

EntityQuery.AddTagRequirement<FArmyDeadTag>(
    EMassFragmentPresence::None);
```

```cpp
EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& ChunkContext)
{
    TArrayView<FArmyHealthFragment> Health =
        ChunkContext.GetMutableFragmentView<FArmyHealthFragment>();

    const TConstArrayView<FMassEntityHandle> Entities =
        ChunkContext.GetEntities();

    for (int32 Index = 0; Index < ChunkContext.GetNumEntities(); ++Index)
    {
        Health[Index].Value = FMath::Max(0.0f, Health[Index].Value);

        if (Health[Index].Value <= 0.0f)
        {
            ChunkContext.Defer().AddTag<FArmyDeadTag>(Entities[Index]);
        }
    }
});
```

执行期间实体仍留在当前 Chunk，因此当前数组视图不会被中途搬动。等安全阶段 Flush 命令缓冲后，实体才添加 `FArmyDeadTag` 并迁移到新的 Archetype。下一次该 Query 执行时，因为声明了 `None`，死亡实体自然不会再次进入。

反例是直接调用会立即改变组成的 EntityManager API：当前 Chunk 一旦因迁移发生重排，`Health`、`Entities` 和迭代器状态都可能失效。

## 实际开发案例 4：ChunkFilter 做整箱粗筛

假设每个 Chunk 有一个 `FArmySimulationChunkFragment`，记录这批单位的模拟 LOD 和是否休眠。休眠 Chunk 完全不需要进入实体循环：

```cpp
EntityQuery.AddChunkRequirement<FArmySimulationChunkFragment>(
    EMassFragmentAccess::ReadOnly,
    EMassFragmentPresence::All);

EntityQuery.SetChunkFilter([](const FMassExecutionContext& ChunkContext)
{
    const FArmySimulationChunkFragment& SimulationState =
        ChunkContext.GetChunkFragment<FArmySimulationChunkFragment>();

    return SimulationState.bSimulationEnabled;
});
```

`ChunkFilter` 在 ChunkFragment 已绑定、Entity Fragment 尚未切片时运行。返回 `false` 会跳过整个 Chunk，因此适合根据 Chunk 级数据做便宜的粗筛。

不适合的情况是“同一个 Chunk 里只有几个实体要跳过”。这类条件应尽量编码成 Tag/Fragment 组成，让实体分到不同 Archetype；否则只能进入回调后逐实体判断。

这个例子还有一个工程前提：项目必须真的维护了“按物理 Chunk 生效”的模拟状态。不要因为玩法上有编队，就假设一个编队天然占据一个 Chunk。若 LOD 属于逻辑编队，应通过 Formation Entity、成员 Tag/Fragment 或共享配置表达，再由专门 Processor 把结果批量映射到可粗筛的数据上。

## 实际开发案例 5：只处理一份命中名单

爆炸、框选、事件回调经常只需要处理一小批实体，而不是扫描 Query 匹配的全部实体。若这些实体已经确认属于同一个 Archetype，可以构造 `FMassArchetypeEntityCollection`：

```cpp
FMassArchetypeEntityCollection HitCollection(
    HitArchetype,
    HitEntities,
    FMassArchetypeEntityCollection::FoldDuplicates);

DamageQuery.ForEachEntityChunk(
    HitCollection,
    Context,
    [](FMassExecutionContext& ChunkContext)
    {
        TArrayView<FArmyHealthFragment> Health =
            ChunkContext.GetMutableFragmentView<FArmyHealthFragment>();

        for (FArmyHealthFragment& Item : Health)
        {
            Item.Value -= 25.0f;
        }
    });
```

这条路径有三个边界：

1. `HitCollection` 只描述一个 Archetype；混合 Archetype 的 EntityHandle 需要先分组为多份 Collection。
2. Query 仍会验证该 Archetype 是否拥有 `FArmyHealthFragment` 等需求，不匹配就不调用 Lambda。
3. Collection 保存的是实体在 Archetype/Chunk 内的连续范围；结构变化后可能过期，长期保存名单时应保留 EntityHandle，并在使用前重建或检查有效性。

在《全面战争》式框选或《最高指挥官》式范围武器中，命中列表通常包含步兵、载具和其他不同组成的单位。此时更实用的是先让 `FEntityCollection` 按 Archetype 建立最新缓存：

```cpp
// SelectedOrHitEntities 可以混合多个 Archetype。
FEntityCollection AffectedEntities(SelectedOrHitEntities);

const TConstArrayView<FMassArchetypeEntityCollection> PerArchetypeCollections =
    AffectedEntities.GetUpToDatePerArchetypeCollections(EntityManager);

for (const FMassArchetypeEntityCollection& Collection : PerArchetypeCollections)
{
    DamageQuery.ForEachEntityChunk(
        Collection,
        Context,
        [](FMassExecutionContext& ChunkContext)
        {
            TArrayView<FArmyHealthFragment> Health =
                ChunkContext.GetMutableFragmentView<FArmyHealthFragment>();

            for (FArmyHealthFragment& Item : Health)
            {
                Item.Value -= 25.0f;
            }
        });
}
```

这里的业务流程是：碰撞/空间查询负责产生 EntityHandle 名单，`FEntityCollection` 负责按物理存储重新分组，`DamageQuery` 负责验证数据契约并批量修改生命值。三者职责分开后，范围伤害不需要对整张战场做一次全量 Query。

如果回调还会发出导致 Archetype 迁移的命令，应明确命令何时 Flush，避免在仍使用旧 Collection 缓存时改变底层索引。最稳妥的原则仍是：**EntityHandle 可长期保存；Archetype Collection 是使用前临时生成的加速视图。**

---

## 设计启示（落到大规模战略战场）

1. **需求清单即文档**：`ConfigureQueries` 里的 `AddRequirement` 就是这台 Processor“读什么、写什么、排除谁”的完整自述。修改 Lambda 时必须同步检查查询声明。
2. **优先在 Query 层筛选**：用 `None` 排除死亡/暂停实体，用 Optional 做 Archetype 级两档逻辑。能在组成层过滤，就不要进入 Lambda 后逐实体筛。
3. **数值变化直接写，组成变化延迟做**：位置、速度、生命值可通过可写 View 更新；添加 Tag、删除 Fragment、销毁实体等操作通过 `Context.Defer()`。
4. **View 不跨回调保存**：ExecutionContext 和其中的数组视图都是极短生命周期对象。长期状态放 Fragment；长期身份用 `FMassEntityHandle`。
5. **并行前先保证纯数据化**：避免捕获可变全局状态，避免访问只能在 GameThread 使用的 UObject。确认不发命令的纯计算查询，可关闭并行命令缓冲后测量收益。
6. **负载不均时再试 AutoBalance**：Chunk 数量、单 Chunk 计算量和分支差异都足够大时，动态调度才可能抵消额外开销。
7. **不要把所有布尔状态都做成 Tag**：会高频切换且不影响处理管线的状态更适合放在 Fragment 枚举/位标记中；否则频繁 Archetype 迁移并产生大量组合。
8. **编队是玩法实体，不是 Chunk**：Formation Entity 保存命令、阵型和汇总状态，成员 Fragment 保存链接与槽位；不要依赖某支部队永远恰好位于同一个物理 Chunk。
9. **模拟和表现解耦**：战斗结果以 Fragment 为权威状态，Actor/ISM/动画只消费结果。远景时可以替换表现或降低模拟精度，而不必销毁战略状态。

## 常见问题排查表

| 现象 | 最可能原因 | 检查与修复 |
|---|---|---|
| Processor 在跑，但 Lambda 一次都不进 | 没有匹配 Archetype；查询只有 `None`；所需 Tag/Fragment 没被 Trait 添加 | 确认至少有一个 All/Any/Optional Fragment；打印实体组成；检查 Trait/模板 |
| 报 `not listed as required` | 调用了 View API，但 Query 没声明对应类型 | 在 `ConfigureQueries` 添加正确 Requirement，并注册 Query |
| 报 `not bound for writing` | Requirement 声明为 `ReadOnly`，却调用 `GetMutable...` | 改为 `ReadWrite`，并重新检查是否会影响 Processor 调度依赖 |
| Optional Fragment 的 GetView 崩溃 | 当前 Archetype 没有该 Optional 类型 | 先用 `DoesArchetypeHaveFragment<T>()` 判断，再取 View |
| 在回调中添加 Tag 后出现视图/迭代异常 | 直接进行了结构性修改 | 改用 `Context.Defer().AddTag/Remove.../DestroyEntity` |
| Collection 偶发处理错实体或失效 | Archetype 内部索引在保存后发生变化 | 使用前检查时效；长期保存 EntityHandle，临时重建 Collection |
| 并行版偶发崩溃或数据不一致 | Lambda 捕获并写外部共享状态，或访问线程受限对象 | 把状态放进 Fragment/线程局部结果；串行处理 GameThread-only 工作 |
| 关闭 Parallel Command Buffer 后 `Defer()` 崩溃 | 配置与实际行为矛盾 | 重新启用命令缓冲，或确保该并行回调绝不发命令 |
| 明明新增了实体，Query 缓存却没重建 | 这通常是正常行为：实体数量变了，但组成没变 | Query 缓存的是 Archetype 匹配关系，执行时会看到 Archetype 当前实体 |

## 性能检查顺序

遇到 Processor 慢时，建议按以下顺序优化：

1. **先看筛选是否过宽**：能否用 Tag `None` 或更精确的 All 条件减少进入回调的 Archetype？
2. **再看循环是否数据连续**：是否在每实体循环里反复查 EntityManager、UObject 或 Map？优先把热数据放进 Fragment 数组。
3. **再看结构命令数量**：是否每实体都单独发相同类型命令？尽量利用 CommandBuffer 的批处理接口。
4. **再测并行**：比较串行与并行实际耗时，不用实体数量猜收益。
5. **最后考虑 ChunkFilter/AutoBalance**：只有明确存在 Chunk 级粗筛条件或负载不均时再加复杂度。

## 写 Processor 时的最小检查清单

- Query 是否至少包含一个 All/Any/Optional 的 Fragment Requirement？
- 每次 `GetFragmentView<T>()` 是否对应 `ReadOnly` 或 `ReadWrite` 声明？
- 每次 `GetMutable...<T>()` 是否对应 `ReadWrite` 声明？
- Optional 类型是否在取 View 前检查当前 Archetype？
- Query 是否通过构造函数或 `RegisterWithProcessor(*this)` 注册？
- Lambda 是否只在当前回调中使用 View/Context？
- 添加/删除元素和销毁实体是否走 Deferred Command？
- 并行 Lambda 是否捕获了可变共享状态或线程受限 UObject？
- 优化是否有 Unreal Insights/计时数据支撑？

## 收尾自测

- **Q1：查询怎么做到几乎零成本复用匹配结果？** 缓存 ValidArchetypes + 翻译表，靠管理器的 ArchetypeDataVersion 版本号判断过期，未过期直接跳过；新原型只做增量补录。
- **Q2：回调里 `GetMutableFragmentView<T>()` 崩了，报"not listed as required"，为什么？** 视图绑定完全由需求清单驱动，没声明就没绑；声明成 ReadOnly 想写同样会被 CHECK_IF_READWRITE 拦下。
- **Q3：并行版的延迟命令怎么保证线程安全？** 每工人克隆上下文 + 独立命令缓冲（惰性创建、刷新线程号），收工 MoveAppend 合并回主缓冲。
- **Q4：Length=0 的区间在执行期怎么解释？** `Chunk.GetNumInstances() - SubchunkStart`——从起点到该 Chunk 最后一个实体。
- **Q5：共享 Fragment 为什么用"哈希变了才重绑"？** 同共享值的实体倾向连续存放，相邻 Chunk 哈希相同就跳过重绑，把共享层的绑定成本摊薄。
- **Q6：为什么不能直接把一个 Chunk 当成一个步兵编队？** Chunk 是引擎的物理存储块，实体迁移、压缩和数量变化都可能改变其内容；玩法编队应由 Formation Entity、链接 Fragment 或明确的共享数据表达。
- **Q7：框选的步兵和载具属于不同 Archetype，为什么不能直接构造一份 Archetype Collection？** 单份 `FMassArchetypeEntityCollection` 只描述一个 Archetype；混合名单应通过 `FEntityCollection` 重建为多份按 Archetype 分组的 Collection。
- **Q8：远处单位为什么不应继续跑完整的单兵攻击 Query？** 大战略项目需要分级模拟；远处可查询 LowSimulation Tag 走编队/军团聚合 Processor，近景实体才进入高精度移动、索敌和攻击链。

## 官方 API 对照

- [`FMassEntityQuery`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MassEntity/FMassEntityQuery)：查询需求、缓存和遍历入口。
- [`FMassExecutionContext`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MassEntity/FMassExecutionContext)：Fragment View、Chunk 数据、实体列表、子系统和延迟命令上下文。
- [`UMassProcessor`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MassEntity/UMassProcessor)：`ConfigureQueries`、`Execute` 和查询注册关系。
- [`FMassCommandBuffer`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MassEntity/FMassCommandBuffer)：添加/删除元素、Tag 和销毁实体的延迟命令接口。

本文源码结论以 UE 5.7.4 对应实现为准。Epic 在线 API 页面可能默认跳转到更新版本，遇到函数签名差异时应优先对照本地 5.7.4 头文件。

**下一步** → `MassProcessor.h` + `MassProcessingTypes.h`：处理器的生命周期、六个执行阶段怎么挂进引擎 Tick、处理器间的依赖排序。
