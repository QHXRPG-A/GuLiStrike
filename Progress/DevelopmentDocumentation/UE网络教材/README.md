# UE 网络：从基础到 GuLiStrike 项目实现

- 源码核对日期：2026-08-31，UE 5.7，协议版本 3。
- 读者：已掌握 C++；正文按约 5000–8000 字组织，十章循序阅读。
- 依据：当前工作区源码，包括实施前已有修改；不以旧设计文档代替现状。
- 本次验证：仅注释/排版的静态一致性及文档核对；没有编译、PIE 或网络模拟实测。

## 阅读目录

| 章节 | 要回答的问题 |
|---|---|
| [01-UE网络模型与对象职责](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/01-UE网络模型与对象职责.md) | 哪些对象在哪一端？ |
| [02-所有权与RPC](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/02-所有权与RPC.md) | RPC 怎样找到接收者？ |
| [03-属性复制与RepNotify](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/03-属性复制与RepNotify.md) | 复制状态如何驱动本地通知？ |
| [04-项目协议与序列化](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/04-项目协议与序列化.md) | 请求、ACK 与各类序号是什么？ |
| [05-登录分配与初始同步](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/05-登录分配与初始同步.md) | 何时允许下令和接收姿态？ |
| [06-选兵与移动请求全过程](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/06-选兵与移动请求全过程.md) | 重试、去重和部分成功如何衔接？ |
| [07-士兵状态与姿态发送](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/07-士兵状态与姿态发送.md) | 谁捕获、谁分块、谁发送？ |
| [08-客户端重建与平滑](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/08-客户端重建与平滑.md) | 收到样本后怎样成为画面？ |
| [09-GM调参与跨模块复制](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/09-GM调参与跨模块复制.md) | 调参何时才能对客户端生效？ |
| [10-联机验证与故障定位](D:/UE5.7/test1/Progress/DevelopmentDocumentation/UE网络教材/10-联机验证与故障定位.md) | 怎样区分逻辑问题与链路问题？ |

## 使用方法

每章按照学习目标、UE 概念、项目调用链与源码、易错点、练习与答案展开。代码块直接摘自本次核对的文件，只保留相关语句，不是新增实现或可独立编译的 Demo。链接指向本机源码行；后续代码增删导致行号漂移时，以同时列出的函数名为准。

五幅图概括真实调用关系，省略细节可顺链接回源码。先读 01–04 建立术语，再读 05–08 串起指挥官链路，最后读调参与验证。每章末尾三题均附当前实现下的答案。

## 阅读边界

本教材覆盖项目自有 Commander 双端链路及飞船 GM 状态；不讨论引擎网络底层、第三方插件、编辑器自动化通信或本地存档。不新增运行时 API，也不将占位的 ServerAcknowledgeFacts 描述为已经完成的事实流。

“可靠投递”“业务接令”“状态收敛”“画面已动”“最终抵达”是不同结论。凡称项目行为，均为源码推导；凡涉及网络门槛，均是已有代码要求，而非本次实测成绩。

基础概念已对照 [Epic UE 5.7 RPC](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine?application_version=5.7)、[所有权](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-owner-and-owning-connection-in-unreal-engine?application_version=5.7)、[属性复制](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicate-actor-properties-in-unreal-engine?application_version=5.7)核对；执行顺序页面的版本参数地址未能打开，采用检索结果标为 UE 5.7 的官方正文，出处已在第 02 章标明。

进一步阅读：[Authority 精读笔记](D:/UE5.7/test1/Progress/DevelopmentDocumentation/Mass精读笔记/GuLiBattleAuthoritySubsystem.md)。
