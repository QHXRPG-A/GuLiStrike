# 约200单位移动交火：Client1性能截帧

2026-10-10。包含单位移动的正式补采已完成，主要瓶颈是游戏线程。30秒内整帧平均 **40.61 ms，约24.62 FPS**；GPU平均 **16.31 ms**。Client1场景UI更新与绘制约 **1.6 ms CPU**，描边捕获在单帧GPU分析中约 **2.22 ms**。长帧明显集中在持续交火的枪口Niagara和战斗表现更新。

## 本轮负载与边界

- 源码引擎 `D:/UnrealEngine-5.7`，运行版本 `5.7.4-0+UE5`，地图 `/Game/Maps/LVL_CommanderMassPrototype`。同进程专服、Client1、Client2及编辑器；Client1为1505×1115，Client2为640×484。质量档3，硬件光追、Lumen及虚拟阴影保留当前设置。
- 接续原权威生成约200单位的场景，补采前双方的正常客户端移动任务均被服务器接收。移动、寻路、避障、碰撞、网络姿态与开火保持启用；不发送停止命令、不关闭单位移动Tick。正常战斗停步、到达及死亡保留。
- 有效窗口开始196个存活单位、106个处于移动状态；结束195个存活单位、65个处于移动状态。这是窗口两端的原生快照，不是移动数量的逐帧平均。双方实例位置回读有实际变化。
- Client1两端分别179、178个单位中心投影在视口内；敌方地面单位中心均为79个。场景UI准备的脚环230个、活动血条128→132个，包含视口外来源，不能把准备数当作实际绘制数。数量遵循用户“差不多”的要求；本次不是精确100个可见敌方描边的固定负载。
- 为稳定人口，仅临时暂停建筑生产与据点夺取，生成单位使用运行时血量50000；武器伤害、射程、射速、生产资源和优化开关保持当前配置。之前的停止指令不作为本轮移动负载；本轮的有效移动目标在同一片可达导航区域。
- 10秒热身、30秒CSV与CPU/GPU Trace。窗口内不截图、盘点对象或写资源。结束后另采原生ProfileGPU及场景PNG；它们不是和CSV某一帧同步的图像。
- 每客户端30秒收到10488条射击事件、播放10600次枪口反馈、接纳7452次目录命中，均走现有权威战斗路径；事件计数有其独立语义，不能互相当作丢失比例。命中采用1个共享批量组件，单次回退0。窗口末每客户端847条飞行数据记录、0个飞行Actor；未叠加额外500枚测试生产器。

## 整帧结果

| 指标 | 平均 | P95 | 样本 |
|---|---:|---:|---:|
| 整帧 | 40.61 ms | 57.05 ms | 741 |
| GameThread CSV | 40.62 ms | 55.71 ms | 741 |
| GPU CSV | 16.31 ms | 17.54 ms | 741 |
| 物理内存 | 10176 MB | 10882 MB | 741 |

按QPC排除开始、结束桥接命令后，CPU分析有738个完整引擎帧；`FEngineLoop::Tick`平均40.54 ms。此范围与CSV的GameThread计时定义不同。整帧和GPU数字包含同进程两个客户端及服务端相关工作，不能直接作为Client1单独运行成本。

## 场景UI和窗口开销

| CPU范围 | 平均 | P95 | 含义 |
|---|---:|---:|---|
| Client1场景UI原生Update | 0.293 ms | 0.489 ms | 含描边CPU提交及来源更新 |
| Client1场景UI原生Paint | 1.293 ms | 1.534 ms | 多个内部叶节点按同帧聚合 |
| Client1完整Widget Tick＋Paint | 1.626 ms | 1.960 ms | 包含外层包装，按同帧合计后计算P95 |
| Client1窗口DrawWindow | 2.020 ms | 2.447 ms | 包含上述游戏UI与其他HUD |
| Client2窗口DrawWindow | 1.893 ms | 2.397 ms | 第二客户端游戏窗口 |
| 编辑器窗口DrawWindow | 1.237 ms | 1.864 ms | 本轮独立编辑器窗口 |
| 全窗口Prepass | 2.126 ms | 2.506 ms | 本轮无法继续按窗口归属 |
| 全窗口Slate Tick | 8.353 ms | 10.056 ms | 包含Prepass和三个窗口DrawWindow |
| LocalPlayer Slate Operations | 2.847 ms | 3.187 ms | 在主Slate Tick之外，遍历游戏视口路径及处理本地玩家Slate操作 |

这些是嵌套范围，不能把各行相加。原生Scope的P95按有活动的帧计算；Update/Paint覆盖740帧。完整Widget与窗口表使用738个完整引擎帧，包括相同帧中的多个子调用。血条准备另有双客户端合计约0.16 ms，未混入场景Widget自身耗时。

本轮编辑器窗口没有嵌入Client1；用真实GameInstance_10/11及Widget路径标记两个客户端，余下窗口为编辑器。全窗口Slate约占GT的21%，其中包含游戏UI，不能全归为编辑器UI。此前Live Coding加载的逐窗口Prepass回调未在本次重启会话中加载，未套用旧会话的布局归属数字。

## 持续交火与移动

| CPU范围 | 平均/引擎帧 | 范围与解释 |
|---|---:|---|
| 战斗表现集中更新 | 5.333 ms | 两客户端合计，含部分Niagara子调用 |
| 机枪枪口简化资源 | 3.607 ms自身耗时 | 两客户端GT上的系统相关调用，含更新、同步及提交；不能再与父范围直接相加 |
| 原生Client1飞行表现Scope | 2.745 ms，活动帧P95 7.879 ms | 主要集中更新路径，包含相应子调用 |
| Client1单位呈现 | 0.439 ms，P95 0.557 ms | 网络样本求值与呈现更新 |
| 服务器预测避障查询 | 0.252 ms，P95 0.436 ms | World=UEDPIE_0；移动负载包含在内 |
| 服务器避障建网格 | 0.069 ms | 与查询、求解分开计量 |
| 服务器避障求解 | 0.013 ms | 不重复加到总处理器Scope |
| 纯客户端弹丸预测 | 0.089 ms | 两客户端GT合计 |
| CharacterMovement | 2.114 ms包含子调用 | 三个游戏World合计，代表帧99次；不能全部算给Mass单位或服务器 |

代表帧以完整GT引擎Tick选取：中位帧38.61 ms、P95帧57.05 ms、最差帧69.69 ms。最差帧中，双客户端战斗表现集中更新 **26.32 ms**，枪口系统自身耗时 **13.87 ms**。其中枪口约9.84 ms发生在集中表现更新路径、2.79 ms在帧尾Niagara提交、1.21 ms在Tick完成任务路径。部分范围嵌套，不能叠加。证据支持下一步优先细分枪口系统调用与表现更新的长帧路径；本轮未修改这些逻辑。

## GPU单帧

原生ProfileGPU报告Frame76972，其队列内部标记Frame76936，存在分析反馈延迟。Graphics跨度16.829 ms、Compute跨度2.510 ms，两队列可以并行，不能求和作为GPU帧时。

| GPU范围 | 截帧耗时 |
|---|---:|
| Client1描边SceneCapture | 2.224 ms |
| Client2描边SceneCapture | 2.306 ms |
| 全窗口Slate渲染 | 0.733 ms |
| Client1窗口Slate渲染 | 0.576 ms |
| 编辑器窗口Slate渲染 | 0.083 ms |

描边通过单独捕获敌方Mesh生成纹理，GPU成本不能用约0.17 ms的CPU捕获提交时间代替。捕获组件添加了临时World标签，避免把两个客户端混为同一捕获。本轮ProfileGPU阈值0.2%，没有可分辨的Ribbon事件行，不作为Ribbon开销已消失的证据。

## 可复查证据与恢复

- [CSV结果](result.json)、[CSV原始帧](frames.csv)、[完整Trace](session.utrace)、[CPU统计](insights-windowed/summary.json)。
- [窗口逐帧分析](slate-analysis.json)、[逐帧窗口数据](slate-per-frame.json)、[Widget身份](widget-identities.json)。
- [代表帧分析](frame-analysis.json)，[最差帧全部事件](selected-frames/worst-events.csv)，[枪口父路径](muzzle-parent-chains.json)。
- [ProfileGPU原始日志](gpu-profile-frame.log)、[GPU解析](gpu-analysis.json)。
- [Client1场景像素](client1.png)来自指定Client1原生Viewport ReadPixels，未包含Slate叠层。原生SHOWUI的全局截图请求实际捕获[Client2含UI窗口](client2-with-ui.png)，蓝方HUD身份已视觉核对；不将该图标为Client1。
- [开始负载](preflight-counts.json)、[结束负载](postflight-counts.json)、[移动接收回读](local-move-ack.json)、[优化开关和原生Scope](counters-after.json)。
- 测试PIE已结束，[恢复回读](cleanup.json)确认World数0，CaptureSeconds恢复20、TeamUnitCap恢复300，移动诊断关闭。GPU采集参数已在脚本finally恢复；临时血量、生产暂停、移动订单、镜头和标签随PIE销毁。

没有改动或编译生产源码，没有切换正式特效资源，没有保存本次运行时测试摆放。原有四项和三项优化保持当前正式设置。早期准备失败、人口漂移及只有部分移动的旧窗口均不作为本轮结论；旧42.55 ms与本轮40.61 ms也不能解读为优化收益。单项及整版三轮配对FPS对照继续按用户安排暂缓。
