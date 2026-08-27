# 精读笔记：MassArchetypeTypes.h / .cpp —— Archetype 的对外零件

> 原文件：`C:\Program Files\Epic Games\UE_5.7\Engine\Source\Runtime\MassEntity\Public\MassArchetypeTypes.h`（474 行）
> 配套实现：`Private/MassArchetypeTypes.cpp`（491 行）
> 引擎版本：5.7.4 ｜ 阅读路线第 3 步（原计划第 2 步的后半）

---

## 预备知识1：Archetype 是什么

一句话：**Archetype（原型）= 一种特定的 Fragment/Tag 组合。组合完全相同的实体，归入同一个原型。**

拿"户型"打比方最直观——户型就是图纸（Fragment 怎么摆），住户就是实体（按户型分楼栋入住）：

```
原型 A：{ Velocity, Health }              原型 B：{ Velocity, Health, FGSNeedRepairTag }
  Chunk 0: Velocity[0..127] Health[0..127]     Chunk 0: Velocity[0..7] Health[0..7]
  Chunk 1: Velocity[0..63]  Health[0..63]      （待维修的僚机不多，一个 Chunk 都占不满）
```

五条要点：

1. **身份就是组合本身**。`{Velocity, Health}` 和 `{Velocity, Health, Target}` 是两个不同原型——没有继承链、没有类名，组合即身份。这就是"出新兵种不用写新类"的底层原因：换一组 Fragment 拼装，就是一个新原型。
2. **它为批处理而生**。同原型实体在 Chunk 里连续平铺，"给所有带 Velocity 的实体跑移动逻辑"时命中的内存天然聚簇。原型是查询和 Chunk 之间的索引单位：查询先按组合匹配原型，再对命中的原型整块迭代。
3. **加删 Fragment = 换原型 = 搬家**。给实体加一个 Tag，组合就变了，实体得从旧原型的 Chunk 搬进新原型——之前说的"运行时改结构有成本"，成本就是这个。所以状态表达优先用 Fragment 里的枚举字段，别靠动态加删 Tag。
4. **共享 Fragment 的取值也算进身份**。同样 Fragment 但 ConstSharedFragment 数值不同（不同兵种参数）的实体属于不同原型——同兵种自然聚簇，批量处理反而更顺（这点在第一篇讲 `FMassArchetypeSharedFragmentValues` 时提过）。
5. **代码里的真身**是 `FMassArchetypeData`（`Private/MassArchetypeData.h`，本对文件的幕后主角）：记录每类 Fragment 在 Chunk 里的布局偏移、Chunk 列表、每 Chunk 容量。本文件造的句柄、区间集合，全是为安全访问它服务的。

顺带说术语：Archetype 是 ECS 行业的通用叫法（Unity DOTS、Flecs、Bevy 同名同义），直译"原型"，Mass 沿用行业标准，不是 Epic 自造词。


## 预备知识2：Entity（实体）、Fragment（片段）、Archetype（原型）、Chunk（块）的关系
Mass 中可以把它理解成一张按“组件组合”分表存储的列式数据库：

```text
Archetype（相同 Fragment/Tag 组合）
└─ Chunk 0（连续的一批实体行）
   ├─ Entity：E0  E1  E2 ...
   ├─ Transform：T0  T1  T2 ...   ← Fragment 列
   └─ Velocity ：V0  V1  V2 ...   ← Fragment 列
└─ Chunk 1 ...
```

- **Entity（实体）**：`FMassEntityHandle`，只是一个轻量的身份/索引，不是 `UObject` 或 `AActor`。它在某个 Archetype 的某个 Chunk 中占一“行”。

- **Fragment（片段）**：类似 ECS 的组件，通常是继承 `FMassFragment` 的纯数据 `UStruct`，例如位置、速度、生命值。某实体拥有的每种 Fragment 对应该行中的一个数据元素。

- **Archetype（原型）**：拥有完全相同 Fragment、Tag（以及相关共享数据配置）的一组实体的存储布局。例如所有同时有 `TransformFragment + VelocityFragment` 的实体属于同一个 Archetype。Archetype 决定有哪些“列”。

- **Chunk（块）**：一个 Archetype 的实际连续内存分块，容纳一批该 Archetype 的实体。Fragment 在 Chunk 内采用 **SoA（数组结构）** 布局：同类 Fragment 连续放在一起，便于 Processor 批量遍历和 CPU 缓存命中。

最关键的对应关系是：

> Entity 是一行；Fragment 是列中的单元格；Chunk 是一批连续的行；Archetype 是具有相同列定义的所有 Chunk 的集合。

当实体新增或移除 Fragment 时，它的“列集合”变了，因此 Mass 通常会把它迁移到匹配的新 Archetype，并复制保留的数据。这也是为什么 Mass 更适合大量、结构相似、按批处理的对象。

补充：`FMassChunkFragment` 是“每个 Chunk 一份”的数据，不是每实体一份；例如某批实体共享的临时处理状态。

---

## 全文一句话总结

这对文件讲的是 Archetype 的**周边零件**，不是 Archetype 本体——真正的 `FMassArchetypeData`（管 Chunk 内存和 Fragment 布局的那位）藏在 `Private/MassArchetypeData.h` 里。这里造的是你天天会碰到的那几样：Archetype 的句柄、**把任意一堆实体压成"Chunk 区间序列"的集合类型**、Chunk 迭代器、以及"Chunk 内物理地址"句柄。其中区间集合（`FMassArchetypeEntityCollection`）是重点——它是 Mass 一切批处理操作的通用货币，查询结果、延迟命令、批量修改，底层运输格式全是它。

## 类型速查表

| 类型 | 一句话职责 |
|---|---|
| `FMassArchetypeHandle` | Archetype 的"不透明"句柄（内部其实是 `TSharedPtr`） |
| `FMassArchetypeVersionedHandle` | 带版本快照的句柄，判断"实体顺序变过没" |
| `FArchetypeEntityRange` | 一段区间：哪个 Chunk、从第几个开始、多长 |
| `FMassArchetypeEntityCollection` | 任意实体列表 → 排好序的区间序列 |
| `FMassArchetypeEntityCollectionWithPayload` | 区间 + 与实体对齐的附带数据（命令缓冲专用） |
| `FMassArchetypeChunkIterator` | 在区间序列上走一遍的小迭代器 |
| `FMassRawEntityInChunkData` | Chunk 内物理地址：裸内存指针 + 槽内下标 |
| `FMassEntityInChunkDataHandle` | 物理地址 + Chunk 序号（可检测数据变动） |
| `FMassQueryRequirementIndicesMapping` | 查询需求 → 原型布局下标的缓存映射 |

---

## 两个前置概念

### 概念一：实体有两套坐标

上一篇讲过，`FMassEntityHandle::Index` 是**全局槽位号**——全世界实体共用一本账，槽位回收复用，所以它是"稀疏"的，相邻两个号在物理上八竿子打不着。

但实体落进 Archetype 之后，它在原型内部还有另一个下标：**稠密序号**（源码里叫 TrueIndex / InternalIndex）。同一个原型里第 0 个、第 1 个、第 2 个实体……物理上真的肩并肩躺在 Chunk 里。于是有一个换算：

```cpp
// CreateRangeForEntity（节选）
const int32* TrueIndexPtr = ArchetypeData->GetInternalIndexForEntity(EntityHandle.Index);
const int32 TrueIndex = *TrueIndexPtr;
const int32 NumEntitiesPerChunk = ArchetypeData->GetNumEntitiesPerChunk();
const int32 ChunkIndex = TrueIndex / NumEntitiesPerChunk;      // 在第几个 Chunk
const int32 SubchunkStart = TrueIndex % NumEntitiesPerChunk;   // 槽内第几个
```

除一下、模一下，稠密序号就变成了"第几 Chunk 第几个"。后面所有区间算法都建立在这个换算上。

### 概念二：Archetype 句柄为什么用 TSharedPtr

上一篇的实体句柄是两个 `int32`，这里却包了个 `TSharedPtr<FMassArchetypeData>`——同一框架内两套哲学，原因是数量级：

- 实体上万级，句柄必须压到 8 字节纯数值，靠代际号保安全；
- Archetype 只有几十上百个（Fragment 组合数有限），共享指针那点开销无所谓，换来的是**免费的生命周期管理**（原型被销毁时句柄自动失效）和**指针即身份**（比较、哈希都直接用指针）。

代价引擎自己认账——代码里两处 `@todo` 原话：*"Once ArchetypeHandle switches to using an index we'll use that instead"*、*"if FMassArchetypeHandle used indices the look up would be a lot faster"*。读源码看到这种注释，就知道 Epic 也把这当成待还的技术债。另外构造函数是 private 的，只对 `FMassEntityManager` 等友元开放——想拿句柄只能找管理器要，防止绕过账本私造。

---

## 逐段精读

### 1. FMassArchetypeVersionedHandle：给句柄盖个时间戳

```cpp
FMassArchetypeVersionedHandle::FMassArchetypeVersionedHandle(const FMassArchetypeHandle& InHandle)
    : ArchetypeHandle(InHandle)
    , HandleVersion(ArchetypeHandle.IsValid() ? ArchetypeHandle.DataPtr->GetEntityOrderVersion() : 0)
{
}

bool FMassArchetypeVersionedHandle::IsUpToDate() const
{
    return ArchetypeHandle.IsValid() && (ArchetypeHandle.DataPtr->GetEntityOrderVersion() == HandleVersion);
}
```

创建时抄一份当前"实体顺序版本号"，`IsUpToDate()` 就是比较两版是否一致。什么时候版本会变？原型内有实体进进出出（加删 Fragment 搬家、销毁）的时候。**它要保护的是区间集合**——你手里那串 `{Chunk2: 5~80, Chunk7: 0~30}` 是按当时的实体排布算的，排布一变就可能指向别人。头文件注释也说得很实在：多数用户不用关心这个值，内部拿来确保集合没过期（`ExportEntityHandles` 里过期就直接拒绝导出）。

和上一篇实体代际号对照着记：**实体句柄的 SerialNumber 防"实体没了"，Archetype 的 HandleVersion 防"队伍重排了"**。都是"对账"思想，管的对象不同。

### 2. FMassArchetypeEntityCollection：把散装实体压成区间

为什么需要它？因为 Mass 的所有批量操作（一条延迟命令作用到 500 个实体、一次查询遍历结果）底层都希望拿到**连续区间**——连续才好按 Chunk 整块处理。而调用方手里往往是"一把随机的实体句柄"。这个类型就是两种形态之间的转换器：

```cpp
FMassArchetypeEntityCollection::FMassArchetypeEntityCollection(
    const FMassArchetypeHandle& InArchetype, TConstArrayView<FMassEntityHandle> InEntities,
    EDuplicatesHandling DuplicatesHandling)
    : Archetype(InArchetype)
{
    // ... 单实体走快速通道 CreateRangeForEntity，这里看多实体路径
    TArray<int32> TrueIndices;
    TrueIndices.AddUninitialized(InEntities.Num());
    int32 NumValidEntities = 0;
    for (const FMassEntityHandle& Entity : InEntities)
    {
        if (Entity.IsValid())
        {
            if (const int32* TrueIndex = ArchetypeData->GetInternalIndexForEntity(Entity.Index))
            {
                TrueIndices[NumValidEntities++] = *TrueIndex;
            }
        }
    }
    TrueIndices.SetNum(NumValidEntities, EAllowShrinking::No);
    TrueIndices.Sort();
    // ... 去重（若选 FoldDuplicates）
    BuildEntityRanges(MakeStridedView<const int32>(TrueIndices));
}
```

流程四步：全局号换稠密序号（顺手滤掉无效实体）→ 排序 → 可选去重 → 压区间。注意一个贴心的分支：实体还没挂上 Archetype（Reserved 态）时拿不到稠密序号，就用全局号凑合排——注释说这样"多少还能沾点性能好处，而且 API 保持统一"。

去重的枚举也有讲究：`NoDuplicates` 是调用方拍胸脯保证没有重复，非 Shipping 构建下发现重复会直接 `check` 炸给你看；`FoldDuplicates` 则老实折叠掉。**给引擎交数据前先想清楚自己保证得了什么，选 NoDuplicates 能白拿运行时收益**——这个 API 设计习惯值得抄。

### 3. BuildEntityRanges：20 行的明星算法

```cpp
void FMassArchetypeEntityCollection::BuildEntityRanges(TStridedView<const int32> TrueIndices)
{
    const FMassArchetypeData* ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandle(Archetype);
    const int32 NumEntitiesPerChunk = ArchetypeData ? ArchetypeData->GetNumEntitiesPerChunk() : MAX_int32;

    int32 ChunkEnd = INDEX_NONE;
    FArchetypeEntityRange DummyChunk;
    FArchetypeEntityRange* SubChunkPtr = &DummyChunk;
    int32 SubchunkLen = 0;
    int32 PrevAbsoluteIndex = INDEX_NONE;
    for (const int32 Index : TrueIndices)
    {
        // 跨过 Chunk 边界，或者序号不连续了，就得开新区间
        if (Index >= ChunkEnd || Index != (PrevAbsoluteIndex + 1))
        {
            SubChunkPtr->Length = SubchunkLen;   // 给上一段写长度（写到 Dummy 上也无妨）
            const int32 ChunkIndex = Index / NumEntitiesPerChunk;
            ChunkEnd = (ChunkIndex + 1) * NumEntitiesPerChunk;
            SubchunkLen = 0;
            const int32 SubchunkStart = Index % NumEntitiesPerChunk;
            SubChunkPtr = &Ranges.Add_GetRef(FArchetypeEntityRange(ChunkIndex, SubchunkStart));
        }
        ++SubchunkLen;
        PrevAbsoluteIndex = Index;
    }
    SubChunkPtr->Length = SubchunkLen;
}
```

输入已排序的稠密序号，输出尽量长的连续区间。读它的技巧就一条：**循环里只在"开新区间"时动笔，长度都留到最后补写**。开新区间的条件有两个——跨 Chunk 边界（区间不能横跨两个 Chunk），或者序号断档（断档处后面的实体不归这次操作管）。第一个 `SubChunkPtr` 指向栈上的 `DummyChunk`，所以循环第一次"补写长度"写进了废纸——省掉一次"是不是第一个区间"的判断。这种小手法读引擎代码时会反复见到。

区间长度的特殊语义也在这对文件里定义：`Length == 0` 表示"从这个起点到 Chunk 末尾全算"。`GatherChunksFromArchetype()`（收集整个原型）就是每个 Chunk 塞一条 `{i, 0, 0}`，零成本表达"全要"。

### 4. WithPayload：命令缓冲的运输包装

```cpp
// CreateEntityRangesWithPayload（节选，删减约 60%）
for (int32 i = 0; i < Entities.Num(); ++i)
{
    const FMassEntityHandle& Entity = Entities[i];
    if (EntityManager.IsEntityValid(Entity))
    {
        const FMassArchetypeHandle ArchetypeHandle = EntityManager.GetArchetypeForEntityUnsafe(Entity);
        // ... 找到/登记该原型的分桶，记录 {ArchetypeIndex, TrueIndex}
        EntityData[i] = { ArchetypeIndex, ArchetypePtr ? ArchetypePtr->GetInternalIndexForEntityChecked(Entity.Index) : Entity.Index };
    }
    else
    {
        EntityData[i] = FEntityInArchetype();  // {INDEX_NONE, INDEX_NONE}
        UE_LOG(LogMass, Warning, TEXT("Invalid entity handle passed in. Ignoring it, but check your code "
            "to make sure you don't mix synchronous entity-mutating Mass API function calls with Mass commands"));
    }
}

// 带 swap 回调的排序：实体数据和载荷同步换位
UE::Mass::Utils::AbstractSort(Entities.Num(), [...](const int32 LHS, const int32 RHS) { ... }
    , [&EntityData, &Payload](const int32 A, const int32 B)
    {
        ::Swap(EntityData[A], EntityData[B]);
        Payload.Swap(A, B);   // ← 载荷跟着实体一起搬家
    });
```

延迟命令长这样："这 300 个实体，每人加 5 点血"。实体和载荷（+5）必须**一一配对着**重排，不然血就加错人了。这里的排序比较器故意写成 `A.ArchetypeIndex > B.ArchetypeIndex`——大于号让 `INDEX_NONE`（无效实体）全部沉到队尾，而后面的分桶循环按 Count 切片时自然把它们切掉。**静默过滤 + 一条警告日志**，既不崩也不装看不见。

这条警告日志本身就是一份教材：它点名的错误是"同步改实体的 API 和延迟命令混用"——命令还在排队，实体已经被同步 API 动过了，句柄自然失效。我们写僚机系统时的纪律同源：**改实体结构一律走命令缓冲，别在处理器循环里直呼管理器的同步接口**。

去重在这个版本里还有个漂亮处理：载荷只是个视图（View），没法删元素，于是把重复项**换位挪到末尾**晾着，同时把分桶计数减掉——排序后的世界照样干净。

### 5. FMassEntityInChunkDataHandle：Chunk 里的"门牌 + 门牌版本"

```cpp
struct FMassRawEntityInChunkData
{
    uint8* const ChunkRawMemory = nullptr;   // Chunk 裸内存起点
    const int32 IndexWithinChunk = INDEX_NONE; // 槽内第几个
};

struct FMassEntityInChunkDataHandle : FMassRawEntityInChunkData
{
    const int32 ChunkIndex = INDEX_NONE;
    const int32 ChunkSerialNumber = INDEX_NONE;  // ← 又见代际号
    MASSENTITY_API bool IsValid(const FMassArchetypeData* ArchetypeData) const;
};
```

这是实体的**物理地址**：哪块内存、偏移多少。用起来最快（省掉句柄对账和 `GetInternalIndexForEntity` 查表），但地址这东西随时会因整理而变——所以又挂了一个 `ChunkSerialNumber`，校验时和原型账本对一下（`ArchetypeData->IsValidHandle(*this)`）。你会发现这是引擎里第三次用同一招：实体有 SerialNumber、Archetype 有 HandleVersion、Chunk 地址有 ChunkSerialNumber。**"值 + 代际号，用时对账"就是 Mass 的安全哲学**，认出这个模式，后面读 Query 和 CommandBuffer 会非常顺。

顺带一个 C++ 小品：这个结构俩成员都声明成 `const`，赋值运算符就只能用 placement new 整体重造——`new (this) FMassEntityInChunkDataHandle(Other);`。宁可这么别扭也不放开 const，是为了防误改（这两个值一旦错了就是写错内存）。引擎对"不变量"的执念随处可见。

### 6. FMassQueryRequirementIndicesMapping：查询和布局之间的翻译表

```cpp
using FMassFragmentIndicesMapping = TArray<int32, TInlineAllocator<16>>;

struct FMassQueryRequirementIndicesMapping
{
    FMassFragmentIndicesMapping EntityFragments;
    FMassFragmentIndicesMapping ChunkFragments;
    FMassFragmentIndicesMapping ConstSharedFragments;
    FMassFragmentIndicesMapping SharedFragments;
};
```

一个查询说"我要读写 Velocity、只读 Health"；一个原型说"我的 Chunk 里 Velocity 排第 3、Health 排第 7"。**每个（查询 × 原型）组合缓存一张翻译表**，执行时按表取数，不用每次字符串或反射比对。`TInlineAllocator<16>` 表示 16 个需求以内连堆都不分配——典型查询的需求就那几个。这个结构是下一篇（`MassEntityQuery.h`）的主角之一，这里先混个脸熟。

---

## 设计启示（落到僚机系统）

1. **批处理思维**：给部队下指令（整队转向、批量受伤）时，底层最优形态就是"区间集合"。自研 Processor 里如果先收集再操作，优先攒实体句柄、一次性交给命令缓冲，让引擎帮你排序压区间——别在循环里一条条发命令。
2. **去重枚举的 API 设计**：自己写批量接口时抄 `NoDuplicates / FoldDuplicates` 这手——调用方给保证就免检查，给不了就明说，非 Shipping 构建还能兜底验证。
3. **区间集合会过期**：拿着 `FMassArchetypeEntityCollection` 跨过会改实体结构的操作后，先 `IsUpToDate()` 再用；引擎导出句据时就是这么把关的。
4. **同步 API 和命令别混用**：WithPayload 那条警告日志值得抄进团队规范——它对应的正是最难查的一类"时好时坏" bug。

## 收尾自测

- **Q1：`Entity.Index` 和 TrueIndex 有什么区别？** 前者是全局账本上的槽位号（稀疏、会复用）；后者是实体在原型内的稠密下标（物理相邻）。换算 `ChunkIndex = TrueIndex / 每Chunk容量`。
- **Q2：为什么 Archetype 句柄敢用 TSharedPtr？** 原型数量级小，共享指针的开销可换生命周期安全；引擎留了 @todo 承认索引化会更快。
- **Q3：Length = 0 的区间是什么意思？** "从 SubchunkStart 到这个 Chunk 末尾的全部实体"。
- **Q4：Mass 里"代际号"思想出现在哪几处？** 实体句柄的 SerialNumber、Archetype 的 HandleVersion、Chunk 内地址的 ChunkSerialNumber——同一套"值 + 版本，用时对账"。
- **Q5：给受伤僚机动态加一个 FGSNeedRepairTag，运行时发生了什么？** 组合变化 → 换原型 → 实体从旧原型 Chunk 搬进新原型（还触发原型的 HandleVersion 变化）。偶发没事，高频发生就是性能坑——状态多档时改用 Fragment 内枚举。

**下一步** → `MassEntityQuery.h` + `MassExecutionContext.h`：查询如何声明需求、如何被翻译成翻译表、`ForEachEntityChunk` 一路走到 Chunk 内存的全过程。
