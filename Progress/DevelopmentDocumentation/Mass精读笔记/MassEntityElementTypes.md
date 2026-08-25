# 精读笔记：MassEntityElementTypes.h —— Mass 框架的"词汇表"

> 原文件：`C:\Program Files\Epic Games\UE_5.7\Engine\Source\Runtime\MassEntity\Public\MassEntityElementTypes.h`
> 引擎版本：5.7.4 ｜ 原文 83 行
> 阅读路线第 1 步（共 12 步，见《20260824-Mass框架启用与源码导读.md》第 4 节）

---

## 预备知识

### 1. 平凡复制（Trivially Copyable）

Fragment 的所有硬约束都源于这一个 C++ 概念，先讲清楚它，后面处处要用。

**定义**：一个类型是"平凡可复制"的，当且仅当（对应标准库 `std::is_trivially_copyable_v<T>`）：

1. 没有虚函数、没有虚基类；
2. 拷贝构造、移动构造、拷贝赋值、移动赋值全部"平凡"——即编译器默认生成（没写、也没 `=delete`）或显式 `=default`；
3. 析构函数平凡（同样：非自定义、非 `=delete`）。

**它意味着什么**：这种类型的对象可以被当作"一坨可搬运的字节"——把它的内存快照 `memcpy` 到另一块内存，新位置上的字节**直接就是一个合法有效的对象**，全程不需要调用任何构造/析构函数。

**正例 / 反例：**

| 类型 | 平凡可复制 | 原因 |
|---|---|---|
| `float`、`FVector`、`FTransform`、枚举 | ✓ | 纯数值字节 |
| 裸指针、实体句柄、定长数组 `T[4]` | ✓ | 指针/数值的排布，无堆所有权 |
| `TObjectPtr<T>` | ✓ | 只是指针的薄包装，拷贝/析构都平凡 |
| `FString`、`TArray<T>`、`TMap<K,V>` | ✗ | 内部持有堆内存：拷贝要分配、析构要释放，`memcpy` 会双重释放/悬垂 |
| 带虚函数的类 | ✗ | 有 vptr/vtable，不能当字节搬 |

**两个容易误解的点：**

1. **可以写默认成员赋值**，如 `FVector Velocity = FVector::ZeroVector;`——这不会破坏平凡可复制性。被约束的只有**拷贝/移动/析构**这些"搬运相关"的特殊成员。引擎自己的 Fragment 就是这么用的（`MassMovementFragments.h` 里满是 `FVector Value = FVector::ZeroVector;`）。
2. **"平凡可复制" ≠ "POD"**：POD 额外要求默认构造也平凡、且是 standard layout。Mass 只要求前者，所以给成员设初值完全没问题。

**为什么 Mass 把它当铁律**：实体在 Archetype 之间迁移（加删 Fragment）、Chunk 整理碎片时，引擎对内存块做的是**批量裸 memcpy**——快就快在跳过所有构造/析构调用。类型不可平凡复制，这套搬运会静默产生悬垂指针和双重释放。下文「逐段精读」第 2 段讲的静态检查报错宏，就是这条规则的守门员。

### 2. Chunk——存实体的"集装箱"

"Chunk"直译"大块"，在 Mass（以及几乎所有现代 ECS 框架）里指**一块大小固定的连续内存，是存放实体数据的基本容器**。

**里面的布局**：同一个 Archetype（= 同一种 Fragment 组合）的实体，按"每类 Fragment 一段连续数组"的方式平铺在 Chunk 里。比如 1000 架僚机的组合是 `{Velocity, Health}`，那每个 Chunk 里装的就是：一段连续的 Velocity 数组 + 一段连续的 Health 数组。这种布局叫 **SoA**（Structure of Arrays，按列平铺），与传统 OOP"每实体一个结构体对象"的 AoS（Array of Structures）相对。组合越大，一个 Chunk 装得下的实体越少（容量由 `FMassArchetypeData::GetNumEntitiesPerChunk()` 按 Fragment 组合大小动态算出）。

**为什么值得费这个劲**，三个原因：

1. **缓存友好**：批量遍历时同类数据在内存里紧挨着，CPU 缓存行命中率极高——这是 Mass 能跑万级单位的物理基础；
2. **批处理单位**：`ForEachEntityChunk` 每次把一整 Chunk 交给你的 lambda，一次循环几百个实体，把每实体的调度开销摊到接近零；
3. **并行单位**：不同 Chunk 可以安全地分给不同线程，互不冲突——这正是 `FMassChunkFragment`（Chunk 内共享、Chunk 间独立）这个粒度存在的意义。

**和第 1 节的关系**：实体加删 Fragment 时要在 Archetype 之间搬家，Chunk 里出现空洞要整理（Defrag）——这些搬移全是整块 memcpy。第 1 节的"平凡复制"约束，就是保证这种搬运合法的纪律。

后文速查表、`FMassChunkFragment`、`ForEachEntityChunk` 里出现的"Chunk"，指的都是它。

---

## 全文一句话总结

这个文件定义了 Mass 世界里"数据"的**五种粒度**。所有游戏数据结构（僚机的速度、血量、编队位置……）都必须从这五个基类之一继承。基类本身全是空壳——存在的唯一意义是"打标记"：内存管理器（`FMassEntityManager`）根据继承的基类决定数据被放进 Chunk 的哪一层。

## 五种元素速查表

| 基类 | 数据量 | 可变性 | 典型用途（僚机示例） |
|---|---|---|---|
| `FMassFragment` | 每实体 1 份 | 自由读写 | 位置/速度/血量/目标句柄 |
| `FMassTag` | 零数据 | — | 状态开关：`FGSWingmanTag` |
| `FMassChunkFragment` | 每 Chunk 1 份 | 自由读写 | 一批实体的聚合统计 |
| `FMassSharedFragment` | 跨实体共享 | 可变 | 同款舰船的渲染描述（ISM） |
| `FMassConstSharedFragment` | 跨实体共享 | 只读 | 兵种数值表（万机只存一份） |

记忆模型：同一"Fragment 组合"的实体连续存放在同一组 Chunk 里（内存连续 = 缓存友好 = 可按 Chunk 批量并行处理，这就是 Mass 快的根源）。

---

## 逐段精读

### 1. FMassFragment —— 每实体一份的核心数据

```cpp
// This is the base class for all lightweight fragments
// 译：所有"轻量 Fragment"的基类
USTRUCT()
struct FMassFragment
{
    GENERATED_BODY()
};
```

**解读：**

1. "lightweight" 不是修辞，是硬约束：Fragment 必须是 **trivially copyable**（可平凡复制，见下一段的报错宏），即能被安全 `memcpy` 的纯数据。因为实体在 Archetype 之间迁移、Chunk 整块搬移时引擎直接按内存拷贝，**不会调用构造/析构函数**。实践中意味着：不要放 `FString`、`TArray` 等带堆内存的成员；需要动态数据时放裸指针 / `TObjectPtr` / 固定大小数组。
2. 写法示例：

```cpp
USTRUCT()
struct FGSWingmanStateFragment : public FMassFragment
{
    GENERATED_BODY()
    FVector Velocity;   // 每架僚机独立一份
    float Health;
};
```

3. 查询侧通过 `EntityQuery.AddRequirement<FGSWingmanStateFragment>(EMassFragmentAccess::ReadWrite)` 声明访问意图；Processor 拿到的是一整 Chunk 的连续数组（`GetMutableFragmentView<T>`），一个循环处理上百实体。

### 2. 静态检查报错宏 —— 藏着 Fragment 的三条规则

```cpp
// these are the messages we'll print out when static checks whether a
// given type is a fragment fails
// 译：当静态检查发现"某个类型不是合法 Fragment"时打印的报错信息
#define _MASS_INVALID_FRAGMENT_CORE_MESSAGE "Make sure to inherit from FMassFragment or one of its child-types and ensure that the struct is trivially copyable, or opt out by specializing TMassFragmentTraits for this type and setting AuthorAcceptsItsNotTriviallyCopyable = true"
#define MASS_INVALID_FRAGMENT_MSG  "Given struct doesn't represent a valid fragment type." _MASS_INVALID_FRAGMENT_CORE_MESSAGE
#define MASS_INVALID_FRAGMENT_MSG_F  "Type %s is not a valid fragment type." _MASS_INVALID_FRAGMENT_CORE_MESSAGE
```

**解读——这条报错信息拆出三条规则：**

- **必须从对应基类继承**（inherit from FMassFragment or one of its child-types）；
- **必须可平凡复制**（trivially copyable）——原因见上一节；
- **存在逃生舱**：特化 `TMassFragmentTraits` 并设置 `AuthorAcceptsItsNotTriviallyCopyable = true`，可以强行让不可平凡复制的类型当 Fragment 用——但搬移时构造/析构不会被调用，极易产生悬垂堆指针。参数名里的 "Author Accepts"（作者自担风险）就是 Epic 的态度：除非完全清楚后果，别碰。

### 3. FMassTag —— 零字节的"有无开关"

```cpp
// This is the base class for types that will only be tested for presence/absence, i.e. Tags.
// Subclasses should never contain any member properties.
// 译：Tag 的基类——只用于测试"有/无"的类型。子类永远不要包含任何成员属性。
USTRUCT()
struct FMassTag
{
    GENERATED_BODY()
};
```

**解读：**

1. Tag 零字节存储，只在 Archetype 组合里占一个"有没有"的位。查询用 `AddTagRequirement<T>(Presence)` 过滤，比读 Fragment 再 `if` 判断快得多——命中筛选发生在 **Chunk 级**，整 Chunk 直接跳过或全收。
2. 正确用法：表达"类别归属"或"待办状态"，如 `FGSWingmanTag`（是僚机）、`FGSNeedNewTargetTag`（需要重新索敌，配合信号/观察者做事件驱动）。
3. 错误用法：当成枚举容器——想放数据的那一刻，它就该是 Fragment 了；"状态值多于一档"也别用多个 Tag 硬凑，放 Fragment 里存枚举。

### 4. FMassChunkFragment —— 每 Chunk 一份的组共享数据

```cpp
// （原文此处无注释）
USTRUCT()
struct FMassChunkFragment
{
    GENERATED_BODY()
};
```

**解读：**

- 一个 Chunk 是同组合实体的一段连续内存块，**容量随 Fragment 组合大小浮动**（组合越大每 Chunk 实体越少，见 `FMassArchetypeData::GetNumEntitiesPerChunk()`）。
- 用途：一批实体共享的临时聚合/缓存——这一 Chunk 的平均位置、批处理计数器、本批次的移动网格。
- 注意：同一个 Processor 的并行 Chunk 迭代里各 Chunk 互不可见，ChunkFragment 正是"Chunk 内共享、Chunk 间独立"的那个粒度。

### 5. FMassSharedFragment —— 跨实体共享的可变数据

```cpp
// （原文此处无注释）
USTRUCT()
struct FMassSharedFragment
{
    GENERATED_BODY()
};
```

**解读：**

- 引擎**按内容哈希去重**：N 个实体带等值的 SharedFragment 时，内存里只有一份实例。经典用法是渲染描述——1000 架同型号僚机共享同一个"网格+材质"描述，ISM 批量绘制靠它成立。
- "可变"是双刃剑：改一处，所有共享者同时生效。渲染参数这种"大家一起变"的场景正合适；每实体独立的数据绝对别放这。

### 6. FMassConstSharedFragment —— 跨实体共享的只读数据

```cpp
// （原文此处无注释）
USTRUCT()
struct FMassConstSharedFragment
{
    GENERATED_BODY()
};
```

**解读：**

- 最安全的共享：声明期赋值、运行期只读。兵种数值表（最大速度、加速度、规避半径）做成它，一万架僚机也只在内存里存一份。
- 细节：共享 Fragment 的**取值参与 Archetype 身份**（证据：`MassEntityTypes.h` 里的 `FMassArchetypeSharedFragmentValues`）——同 Fragment 组合但兵种参数不同的实体会落在不同 Archetype 里。这通常是好事（同兵种聚在一起，批量处理更顺），但设计 Fragment 切分时要有意识。

### 7. UE::Mass::IsA\<T\> —— 运行时类型归类工具

```cpp
// 【解读】给定一个 UScriptStruct*，判断它属于五类元素中的哪一类。
// 泛型版本直接返回 false，五个特化版本各自沿 UStruct 继承链检查。
namespace UE::Mass
{
    template<typename T>
    bool IsA(const UStruct* /*Struct*/)
    {
        return false;
    }

    template<>
    inline bool IsA<FMassFragment>(const UStruct* Struct)
    {
        return Struct && Struct->IsChildOf(FMassFragment::StaticStruct());
    }

    // ……FMassTag / FMassChunkFragment / FMassSharedFragment /
    //    FMassConstSharedFragment 四个特化同构，略
}
```

**解读：** 查询注册、静态检查、调试器都用它给类型归类。平时写业务代码用不到，读它只需记住：**五类元素的"身份"完全由继承链表达**，没有任何魔法。

---

## 收尾自测

- **Q1：什么时候用 SharedFragment？** —— 数据"跨实体相同、且适合一起变"用 SharedFragment（渲染描述）；"跨实体相同但永不改"用 ConstSharedFragment（数值表）；"每实体各一份"用 Fragment；"只有有无两态"用 Tag。
- **Q2：为什么 Fragment 禁止 FString/TArray 成员？** —— 平凡复制约束，Chunk 搬移是裸 memcpy，堆成员会双重释放/悬垂。
- **Q3：Tag 和"值为 bool 的 Fragment"怎么选？** —— Tag 零存储且 Chunk 级过滤；bool Fragment 每实体 1 字节还得读出来判断。纯开关必用 Tag。

**下一步** → `MassEntityHandle.h`（85 行）：实体句柄与代际号（SerialNumber）如何防止悬垂引用。
