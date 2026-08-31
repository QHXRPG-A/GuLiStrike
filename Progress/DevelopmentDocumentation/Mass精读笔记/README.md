# Mass 精读笔记

- 源码核对日期：2026-08-31；本机 UE 5.7.4（CL 51494982）。
- 面向已掌握 C++ 的读者：用当前 GuLiStrike 士兵实现理解 Mass，不假设所有玩法都必须改成 Mass。
- 阅读基线是当前工作区，包括已有未提交修改。真实源码摘录、示意代码和历史验证分开标明。

## 阅读顺序

| 顺序 | 笔记 | 要回答的问题 |
|---|---|---|
| 1 | [MassEntityElementTypes](./MassEntityElementTypes.md) | Fragment、Tag 与 Shared 数据分别归谁拥有？ |
| 2 | [MassEntityHandle](./MassEntityHandle.md) | 为什么本地句柄不能当网络 SoldierId？ |
| 3 | [MassArchetypeTypes](./MassArchetypeTypes.md) | 组成、共享值、死亡与调参迁移如何关联？ |
| 4 | [MassEntityQuery 与 ExecutionContext](./MassEntityQuery与ExecutionContext.md) | 避让捕获如何按 Chunk 读写数据？ |
| 5 | [GuLiBattleAuthoritySubsystem](./GuLiBattleAuthoritySubsystem.md) | 选兵、移动、固定步和网络快照如何串起来？ |

想先看玩法全貌，可以先读第 5 篇的流程图，再按 1–4 补齐数据概念。

## 与网络教材衔接

[UE 网络教材](../UE网络教材/README.md)解释跨端所有权、RPC、属性复制与客户端重建；本目录解释士兵数据在本机如何组织和访问。

- [第 05 章](../UE网络教材/05-登录分配与初始同步.md)：公共 BattleReady 与 SoldierStreamReady 的两道门。
- [第 07 章](../UE网络教材/07-士兵状态与姿态发送.md)：Authority 捕获、发布组件分摊、NetSync 真正发送。
- [第 08 章](../UE网络教材/08-客户端重建与平滑.md)：名册驱动镜像、样本求值及换战局清理。
- [第 09 章](../UE网络教材/09-GM调参与跨模块复制.md)：已提交速度发布，以及独立的飞船配置屏障。

## 当前实现边界

公共 Battle 框架管理玩家 GUID、阵营、席位、连接和 Pawn 复活。Commander 发布组件启用士兵模块后，Authority 才在专用导航就绪时整批创建 500 兵；以 30Hz 固定步模拟，发布器目标每三步捕获一次。纯公共战局不会自动生成士兵。

服务器出生组成是 13 个 Fragment、ServerAuthority 与 Even 两个 Tag、两类 ConstShared；调参可换到 Odd。客户端镜像仍只有 Transform、Identity、Health 和镜像 Tag。玩家身份、SoldierId 与本地 Entity Handle 是三个不同层次；Ground/Air 移动不走士兵 Query。

## 验证与历史

本轮只做文档和源码静态核对，没有重新编译或运行 UE。此前的 [公共框架归档](../../Archive/20260831-公共战局框架与三类角色接入.md)记录 50/50 现有测试通过，但 NetworkGate 仍有一次未标记硬跳变；这不等于每个 Mass API 有专项测试。

[原型阶段归档](../../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)保留旧运行记录，其临时日志已清理。[本次修订归档](../../Archive/20260831-网络教材与Mass精读笔记同步修订.md)记录文档改动和核对结果。
