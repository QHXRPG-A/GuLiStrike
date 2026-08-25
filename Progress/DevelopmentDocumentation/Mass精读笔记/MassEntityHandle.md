# 精读笔记：MassEntityHandle.h —— 实体句柄与代际号

> 原文件：`C:\Program Files\Epic Games\UE_5.7\Engine\Source\Runtime\MassEntity\Public\MassEntityHandle.h`（85 行）
> 配套实现（本篇一起读）：`Public/Private MassEntityManagerStorage.h/.cpp`（序号的存储与回收）、`Private/MassEntityManager.cpp`（对外校验 `IsEntityValid`）、`Private/MassEntityManagerConstants.h`（哨兵常量）
> 引擎版本：5.7.4 ｜ 阅读路线第 2 步

---

## 全文一句话总结

`FMassEntityHandle` 是一张 8 字节的"工牌"：`Index` 记你在存储里的槽位号，`SerialNumber` 记这个槽位现在是"第几代"。它不是指针，也不指向任何东西——想知道实体还在不在，得拿工牌**回管理器对账**：账本上这个槽位的代际号和你手里的一致，才算有效。实体销毁后槽位会被新实体复用，但代际号永远往前走，所以旧句柄一对账就对不上——**旧工牌自动作废**，这就是 ECS 不用指针也不怕悬垂引用的原因。

## 速查表

| 成员/方法 | 作用 |
|---|---|
| `int32 Index` | 槽位号；**0 是哨兵**，永远不代表真实实体 |
| `int32 SerialNumber` | 代际号；0 表示未设置 |
| `IsValid()` / `IsSet()` | 只查"不是全零"，**不证明实体存在**（见第 2 节的警告） |
| `AsNumber()` / `FromNumber()` | 与 `uint64` 互相重解释（靠 static_assert 保证 8 字节/8 对齐） |
| `GetTypeHash` | 现成的哈希函数，可直接当 `TMap` 键 |
| `operator<` | 只按 `Index` 排序，别当"创建先后"用 |

---

## 预备知识：句柄（Handle）vs 指针（Pointer）

指针记的是地址。对象一销毁，指针就成了悬垂指针；对象在内存里搬个家，所有指向它的指针都得跟着改。

句柄只记编号。编号背后是什么，要回到发号的机构查表才知道；对象怎么搬家、槽位怎么复用，持有者完全无感。

Mass 的实体数据住在 Chunk 里，随时整块 memcpy 搬迁，地址根本靠不住，所以只能走句柄这条路。发号的机构就是 `FMassEntityManager`，账本就是它的 Entity Storage。

---

## 逐段精读（头文件）

### 1. 结构定义：两个 int，仅此而已

```cpp
USTRUCT()
struct alignas(8) FMassEntityHandle
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
    int32 Index = 0;

    UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
    int32 SerialNumber = 0;
```

**解读：** 就两个 `int32`，共 8 字节。它是 USTRUCT（要进反射系统），但没有虚函数、构造析构全是默认的——所以它本身就是上一篇说的可平凡复制类型，**可以放心存进 Fragment**。僚机的目标引用就该是它，而不是 `AActor*`。

`alignas(8)` 不是随手写的：文件末尾两条 `static_assert` 锁死了 `sizeof == 8` 和 `alignof == 8`，给下面 `AsNumber()` 的裸重解释兜底。谁往这个结构里加成员，编译期直接报错。

### 2. IsValid 的"诚实警告"——全文件最值得划线的注释

```cpp
/** Note that this function is merely checking if Index and SerialNumber are set. There's no way
 *  to validate if these indicate a valid entity in an EntitySubsystem without asking the system. */
bool IsSet() const
{
    return Index != 0 && SerialNumber != 0;
}

inline bool IsValid() const
{
    return IsSet();
}
```

**解读：** 引擎自己把话说透了：这个函数只能证明"工牌上填过字"，证明不了"工牌对应的人还在职"。真想知道实体活着没有，必须回管理器对账。于是有效性分两级：

| 调用 | 回答的问题 | 代价 |
|---|---|---|
| `Handle.IsValid()` | 这张工牌填过字吗（非全零） | 纯本地判断，零成本 |
| `EntityManager.IsEntityValid(Handle)` | 这张工牌对得上账吗（实体还活着） | 要查账本 |

业务代码的纪律：**跨帧、跨系统持有的句柄，用之前过第二级**。第一级只够快速排空"这里压根没存过句柄"的情况。

### 3. AsNumber / FromNumber：句柄变 uint64

```cpp
uint64 AsNumber() const { return *reinterpret_cast<const uint64*>(this); }
static FMassEntityHandle FromNumber(uint64 Value) { ... }
```

**解读：** 把两个 int 的位模式直接当一个 `uint64` 用。用途是"匿名传递"——有些数据通道和调试工具不想引入 Mass 的头文件，就传这个裸数字。敢这么 `reinterpret_cast`，靠的是那两条 `static_assert`：8 字节、8 对齐，位模式怎么搬都无损。自家业务代码别学这招，老老实实用类型明确的句柄。

### 4. 杂项：哈希与排序

```cpp
friend uint32 GetTypeHash(const FMassEntityHandle Entity)
{
    return HashCombine(Entity.Index, Entity.SerialNumber);
}
bool operator<(const FMassEntityHandle Other) const { return Index < Other.Index; }
```

**解读：** `GetTypeHash` 把两个字段揉在一起，所以 `TMap<FMassEntityHandle, X>` 开箱即用——僚机系统里"队长句柄 → 成员列表"的映射就这么建。`operator<` 故意只比 `Index`：它的用途是排序时把同槽位相关的数据聚在一起（对缓存局部性有利），不代表创建先后，注释里也专门提醒了这一点。

---

## 实现篇（配套 cpp）：代际号是怎么发、怎么作废的

### 5. 哨兵：烧掉第 0 号槽

```cpp
// MassEntityManagerConstants.h
namespace UE::Mass::Private
{
    // Index 0 is a sentinel for an Empty/Unset EntityHandle
    constexpr int32 InvalidEntityIndex = 0;
};

// MassEntityManagerStorage.cpp —— 单线程存储初始化
void FSingleThreadedEntityStorage::Initialize(const FMassEntityManager_InitParams_SingleThreaded&)
{
    // Index 0 is reserved so we can treat that index as an invalid entity handle
    const FMassEntityHandle SentinelEntity = AcquireOne();
    check(SentinelEntity.Index == UE::Mass::Private::InvalidEntityIndex);
}
```

**解读：** 初始化时故意把第一个实体领走扔掉。妙处在时序：全局序号计数器从 0 开始 `fetch_add`，第一次调用恰好返回 0——这一手**同时烧掉了 index 0 和 serial 0**。从此全零句柄（也就是默认构造出来的句柄）永远不会对应任何真实实体，`IsSet()` 的判零逻辑才能成立。一行 `AcquireOne()` 藏了两层设计。

### 6. 发牌：AcquireOne——每次都发全新序号

```cpp
FMassEntityHandle FSingleThreadedEntityStorage::AcquireOne()
{
    const int32 SerialNumber = GenerateSerialNumber();   // 全局原子计数器 fetch_add(1)
    const int32 Index = (EntityFreeIndexList.Num() > 0)
        ? EntityFreeIndexList.Pop(EAllowShrinking::No)   // 槽位回收复用
        : Entities.Add();                                 // 或开新槽
    Entities[Index].SerialNumber = SerialNumber;
    ...
}
```

**解读：** 注意这里的不对称：**槽位回收复用，序号永不复用**。序号来自全局原子计数器，只增不减——源码注释说得很直白：序号只要保证唯一就行，连 AutoRTFM 事务失败回滚都不用退号。

后果：槽位 5 的旧实体销毁后，新实体搬进槽位 5，会领到一个更大的新号。此时还攥着旧句柄 `{5, 旧号}` 的代码，一对账立刻 mismatch。**旧句柄自动作废，防悬垂的全部秘密就这一行。**

### 7. 三态生命周期：Free / Reserved / Created

```cpp
IEntityStorageInterface::EEntityState FSingleThreadedEntityStorage::GetEntityState(int32 Index) const
{
    const uint32 CurrentSerialNumber = Entities[Index].SerialNumber;
    if (CurrentSerialNumber != 0)
    {
        return Entities[Index].CurrentArchetype.Get()
            ? EEntityState::Created
            : EEntityState::Reserved;
    }
    return EEntityState::Free;
}
```

**解读：** 实体一生三个状态：`Free`（槽位空着，序号为 0）→ `Reserved`（领了号，还没配 Fragment 组合）→ `Created`（挂上 Archetype，数据落进 Chunk）。

故意拆成两步，是为了配合分帧和多线程：先占号，后落座。对外校验也跟着分两档——`IsValidHandle` 只查号，`IsEntityActive` 还要求已落座。

### 8. 回收：Release——先对账，再销证据

```cpp
int32 FSingleThreadedEntityStorage::Release(TConstArrayView<FMassEntityHandle> Handles)
{
    int DeallocateCount = 0;
    for (const FMassEntityHandle& Handle : Handles)
    {
        FEntityData& EntityData = Entities[Handle.Index];
        if (EntityData.SerialNumber == Handle.SerialNumber)   // 对账
        {
            EntityData.Reset();                    // Archetype 置空、SerialNumber = 0
            EntityFreeIndexList.Add(Handle.Index); // 槽位进回收列表
            ++DeallocateCount;
        }
    }
    return DeallocateCount;
}
```

**解读：** 释放前先对账，号对不上的直接跳过。所以同一个句柄销毁两次，第二次是静默 no-op——**防重复释放是白送的**。

`Reset()` 把序号清零而不是加一，因为单线程模式下这个槽位下次被领走时会拿到全新的全局号，没必要原地自增。这也是下一节并发模式的做法差异所在。

### 9. 并发存储：另一种序号策略——30 位"原地自增"代际

```cpp
// MassEntityManagerStorage.h —— 并发存储的每槽位数据（位域）
struct FEntityData
{
    static constexpr int MaxGenerationBits = 30;
    TSharedPtr<FMassArchetypeData> CurrentArchetype;
    uint32 GenerationId : MaxGenerationBits = 0;  // 本槽位现在是第几代实体
    uint32 bIsAllocated : 1 = 0;                  // 1 = 非空闲
};

// MassEntityManagerStorage.cpp —— 并发释放：代际号自增
int32 FConcurrentEntityStorage::ForceRelease(TConstArrayView<FMassEntityHandle> Handles)
{
    for (const FMassEntityHandle& Handle : Handles)
    {
        FEntityData& EntityData = LookupEntity(Handle.Index);
        ++EntityData.GenerationId;   // ← 旧句柄当场作废
        EntityData.bIsAllocated = 0;
        EntityData.CurrentArchetype.Reset();
    }
    ...
}
```

**解读：** 并发存储换了一套完全相反的思路——每个槽位自己数代数，释放时原地 `++GenerationId`，槽位复用时沿用已自增的代，旧句柄当场作废。为什么不能用全局计数器？多线程抢同一个原子变量会互相打架，改成每槽位自记代数就互不干扰；反正语义只需要"这个槽位的号变过"，不需要全局唯一。

| | 单线程存储 | 并发存储（编辑器构建可用） |
|---|---|---|
| 序号来源 | 全局单调原子计数器 | 每槽位 30 位代际号，释放时自增 |
| 回收时 | 清零，下次领号发新全局号 | `++GenerationId`，槽位复用沿用新代 |
| 槽位结构 | `TChunkedArray` 连续数组 | 页式内存（每页 65536 实体，上限 10 亿），配 FreeListMutex |
| 回绕风险 | 无（int32 用不完） | 单槽位约 10 亿次复用后代际回绕 |

对外接口完全没变：`IsValidHandle` 依旧是"index 有效 && 账本号 == 手里号"。读引擎源码时多留意这种"**接口同、策略异**"的分层——上层语义稳定，下层实现随便换，这是很典型的抽象设计。

### 10. 终极对账：管理器级 IsEntityValid

```cpp
bool FMassEntityManager::IsEntityValid(FMassEntityHandle Entity) const
{
    return (Entity.Index != UE::Mass::Private::InvalidEntityIndex)
        && GetEntityStorageInterface().IsValidIndex(Entity.Index)
        && (GetEntityStorageInterface().GetSerialNumber(Entity.Index) == Entity.SerialNumber);
}
```

**解读：** 三连检：index 不是哨兵 → 在账本范围内 → 账本上的号和你手里的一致。第 2 节说的第二级校验，落点就是这里。日常代码不必直接摸 `FMassEntityManager`，走 `UMassEntitySubsystem` 的封装即可。

---

## 设计启示（落到僚机系统）

1. **Fragment 里存目标引用用 `FMassEntityHandle`**，不用指针、不存 Actor 引用——8 字节、可平凡复制，随 Chunk 搬家无感。
2. **句柄不会主动通知你目标死了**。索敌、开火前先 `IsEntityValid` 过一遍。需要"目标死亡回调"语义的，去看 `MassEntityRelations.h`（关系系统，带生命周期通知）——僚机的队长-成员编队关系正是它的典型场景。
3. **别缓存"校验通过"这个结论**。延迟命令和并发下，实体可能在你两次使用之间被释放——用时对账，用完即弃。
4. 句柄当 `TMap` 键直接用（`GetTypeHash` 现成）；按 `Index` 排序能让批量访问更贴近 Chunk 布局。

## 收尾自测

- **Q1：为什么 Index 回收复用、SerialNumber 永不复用？** 复用槽位省内存；序号保持唯一，旧句柄一对账必 mismatch，等效自动作废。防悬垂的关键在序号不复用。
- **Q2：`Handle.IsValid()` 返回 true，实体一定存在吗？** 不一定，它只排除全零。真校验要 `EntityManager.IsEntityValid()`。
- **Q3：同一个句柄销毁两次会怎样？** 第二次对不上号，被静默跳过，不会双重释放。
- **Q4：为什么 `{0,0}` 一定无效？** 初始化时哨兵实体吃掉了 index 0 和 serial 0，此后真实体两者都 ≥ 1。

**下一步** → `MassArchetypeTypes.h`（474 行，扫读）：Archetype 的完整数据结构——Fragment 组合如何决定实体的"户籍"，Chunk 如何组织。
