# UE 网络：从基础到 GuLiStrike 项目实现

- 源码核对日期：2026-08-31，UE 5.7；已同步公共 Battle 框架提取，士兵协议仍为版本 3。
- 读者：已掌握 C++；正文按约 5000–8000 字组织，十章循序阅读。
- 依据：当前工作区源码，包括实施前已有修改；不以旧设计文档代替现状。
- 验证边界：章节描述以当前源码为依据；此前编译和联机结果见公共框架实现归档；本轮仅做文档静态核对，不能将流程图视为实测证据。

## 阅读目录

| 章节 | 要回答的问题 |
|---|---|
| [00-理论篇-网络同步概念理解](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/00-理论篇-网络同步概念理解.md) | 连接、RPC、复制条件、组件同步的引擎概念如何串起来？（Jerish 原作 UE 5.7 源码改编） |
| [01-UE网络模型与对象职责](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/01-UE网络模型与对象职责.md) | 公共战局与角色玩法怎样分工？ |
| [02-所有权与RPC](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/02-所有权与RPC.md) | RPC 怎样找到接收者？ |
| [03-属性复制与RepNotify](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/03-属性复制与RepNotify.md) | 复制状态如何驱动本地通知？ |
| [04-项目协议与序列化](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/04-项目协议与序列化.md) | 请求、ACK 与各类序号是什么？ |
| [05-登录分配与初始同步](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/05-登录分配与初始同步.md) | 公共握手和士兵握手为何分开？ |
| [06-选兵与移动请求全过程](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/06-选兵与移动请求全过程.md) | 重试、去重和部分成功如何衔接？ |
| [07-士兵状态与姿态发送](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/07-士兵状态与姿态发送.md) | 谁捕获、谁分块、谁发送？ |
| [08-客户端重建与平滑](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/08-客户端重建与平滑.md) | 收到样本后怎样成为画面？ |
| [09-GM调参与跨模块复制](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/09-GM调参与跨模块复制.md) | 调参何时才能对客户端生效？ |
| [10-联机验证与故障定位](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/10-联机验证与故障定位.md) | 怎样区分逻辑问题与链路问题？ |

## 使用方法

每章按照学习目标、UE 概念、项目调用链与源码、易错点、练习与答案展开。C++ 代码块取自本次核对的文件，保留相关语句；流程图和协议数值例子是解释，不是可独立编译的 Demo。链接指向本机源码文件，以同时列出的函数名定位实现。

五幅图概括真实调用关系，省略细节可顺链接回源码。建议先读[00-理论篇](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/00-理论篇-网络同步概念理解.md)建立引擎侧的通用概念（连接所有权、RPC 执行端、复制条件、组件同步，全部对照 UE 5.7 引擎源码），再按 01–04 建立项目术语，读 05–08 串起公共连接与指挥官链路，最后读调参与验证。每章末尾三题均附当前实现下的答案。

## 阅读边界

本教材覆盖公共 Battle 框架、Commander 双端链路、Ground 占位和飞船移动/装配/开火；不讨论引擎底层、第三方插件、编辑器自动化通信或本地存档。Ground 没有武器；飞船沿用旧 NPC 命中行为，尚无飞船伤害、护盾或完整死亡链。

阅读旧名称时注意：Commander GameMode/PlayerState/GameState 是公共类的兼容派生；默认网络子对象仍叫 CommanderNetSync，但只有一个对象。旧 `IsSyncReady()` 仍表示士兵流就绪，公共 `IsConnectionReady()` 不依赖 Mass。`ServerAcknowledgeFacts` 仍是占位接口。

“可靠投递”“业务接令”“状态收敛”“画面已动”“最终抵达”是不同结论。项目实现由源码支撑，验收门槛和实际成绩分开列示；第 10 章及[公共框架实现归档](D:/UE5.7/test1/Progress/Archive/20260831-公共战局框架与三类角色接入.md)明确记录通过、失败和未验证项。

基础概念已对照 [Epic UE 5.7 RPC](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine?application_version=5.7)、[所有权](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-owner-and-owning-connection-in-unreal-engine?application_version=5.7)、[属性复制](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicate-actor-properties-in-unreal-engine?application_version=5.7)核对；执行顺序页的 5.7 参数地址未取到正文；可读页面标为 5.8，检索缓存仍标为 5.7，仅作通用顺序规则的补充参考，不作为 5.7 专属行为的证据。

进一步阅读：[Mass 阅读目录](D:/UE5.7/test1/Progress/DevelopmentDocumentation/Mass精读笔记/README.md)串起元素、Handle、Archetype、Query 与 [Authority 导读](D:/UE5.7/test1/Progress/DevelopmentDocumentation/Mass精读笔记/GuLiBattleAuthoritySubsystem.md)。两套文档共用当前源码基线；[本次文档修订归档](D:/UE5.7/test1/Progress/Archive/20260831-网络教材与Mass精读笔记同步修订.md)只记录文档核对。
