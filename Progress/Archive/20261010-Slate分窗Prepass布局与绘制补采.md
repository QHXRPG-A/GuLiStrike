---
schema: guli-progress/v1
id: ARC-20261010-007
work_id: ''
kind: archive
role: root
title: Slate分窗Prepass布局与绘制补采
areas: [performance, ui]
categories: [art, performance]
status: recorded
verification: partial
created: '2026-10-10'
updated: '2026-10-10'
summary: 源码版Live Coding加载编辑器诊断后完成双客户端30秒分窗补采。编辑器主窗口扣除Client1视口为2.141ms，Client1与Client2视口UI为1.053与1.138ms，Slate合计4.791ms；编辑器占Slate约45%，占本次GT约8%。
next_action: 本轮补采已完成；后续UI优化须分别处理布局失效、Paint和逻辑更新，并按具体改动重新测量，不据本轮动态场景推出打包版帧率。
relations:
  related: [ARC-20261010-003, ARC-20261010-005]
status_note: 用户要求开PIE补采并明确允许本次编译与加载诊断代码。完成根窗口和游戏视口Prepass计时、逐帧互斥归属、关闭回调参照及恢复状态核对；没有应用生产UI优化、恢复整版FPS矩阵或运行额外600单位500弹丸入口。新PIE负载随玩法变化，结果不能当作优化收益。
---

# Slate分窗Prepass布局与绘制补采

本增量补齐[前一轮绘制归属记录](20261010-Slate物理窗口与客户端游戏UI耗时拆分.md)中缺失的每窗口布局计时。旧记录保留原事实，本轮使用新启动的PIE采样，不把旧Trace与新场景当作严格性能配对。

用户先要求“开PIE补采”，再明确回复“允许本次编译并加载诊断代码”。使用当前 `/Game/Maps/LVL_CommanderMassPrototype` 及既有同进程专服双客户端设置。Client1 嵌于主编辑器窗口，视口1505×1115；Client2为独立PIE窗口，视口640×484。实际身份为 GameInstance_4 / GameInstance_5，按World、SWindow、SViewport及Tick/Paint的真实UObject路径关联，不根据窗口遍历顺序猜测。

## 变更和加载

| 文件 | 本轮变更 |
|---|---|
| [GuLiSlatePrepassDiagnostics.cpp](../../Source/GuLiStrikeEditor/Private/GuLiSlatePrepassDiagnostics.cpp) | 编辑器模块中的默认关闭诊断。显式Start绑定引擎已有BeginWidgetPrepass/EndWidgetPrepass，记录窗口及游戏视口根耗时；Stop和EndPIE解除回调并保存JSON。无引擎源码修改。 |
| [GuLiStrikeEditor.Build.cs](../../Source/GuLiStrikeEditor/GuLiStrikeEditor.Build.cs) | 添加私有Slate/SlateCore依赖。 |
| [补采和离线分析目录](../../outputs/performance/20261010-slate-prepass-live/report.md) | capture、export、analyze、构建核对、逐帧分类和完整报告。采样窗口内不截图、不盘点对象、不写资源。 |

源码引擎 `D:/UnrealEngine-5.7`，目标 `GuLiStrikeEditor Win64 Development`，正在运行的源码Editor PID21532。通过 `LiveCoding.Compile` 编译新增诊断CPP，UBT记录 `Result: Succeeded`，Live Coding补丁链接和创建成功。编译经编辑器发起，宿主未独立取得子进程数字退出码；证据是UBT成功状态、补丁记录和原生命令的实际执行，未声称已完成普通DLL完整重建。

引擎与项目模块清单BuildId一致：`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。Live Coding输出过 `.voltbl` 与新编译单元contributions诊断，随后成功创建补丁。烟雾采集及正式补采都实际记录了四个预期根目标，计时配对错误为0。诊断源码SHA256为 `82CEFC1C96B3DF5E02A1C970B541C29E70AA731BE069ED563F9C089536CCC197`。证据见[构建与加载核对](../../outputs/performance/20261010-slate-prepass-live/build/verified.json)。

## 分窗结果

诊断开启窗口有效采样30秒，1105个完整引擎帧。每帧验证根布局范围嵌套关系、两个实际DrawWindow与游戏实例归属，以及互斥分类之和等于Slate Scope。表中均为GameThread上的CPU时间，GPU UI栅格化另属GPU指标。每行P95独立计算，不能相加。

| 互不重叠的分类 | 平均ms/引擎帧 | P95 ms | 占Slate |
|---|---:|---:|---:|
| 编辑器主窗口，扣除Client1游戏视口 | 2.141 | 2.577 | 44.7% |
| Client1游戏视口UI布局、更新和绘制 | 1.053 | 1.522 | 22.0% |
| Client2游戏视口UI布局、更新和绘制 | 1.138 | 1.561 | 23.8% |
| 窗口壳、视口和其余公共Slate处理 | 0.459 | 0.590 | 9.6% |
| Slate合计 | 4.791 | 5.888 | 100% |

编辑器部分拆为布局1.018ms、其余绘制1.123ms；Client1为布局0.428ms、更新/绘制0.625ms；Client2为布局0.515ms、更新/绘制0.623ms。客户端布局范围包含少量视口壳，不声称是纯UMG布局。

主窗口布局根1.446ms中嵌入Client1的0.428ms，先扣除后归属编辑器；Client2窗口根0.533ms中嵌入其视口0.515ms。全窗口Prepass合计2.002ms，根目标之外的失效/遍历等剩余约0.023ms。嵌套范围没有重复相加；Game UI Paint内的Scene UI、HUD、小地图子Scope也只用作明细。

两个游戏视口各测得1105次Prepass，与完整帧数一致；每帧亦有一次对应的Game UI Paint。本窗口的实际频率约36.8次/秒，随引擎帧率变化。降低业务逻辑更新频率不会自动降低Slate布局和绘制频率。

因此，编辑器部分占本轮Slate约45%，两个客户端UI合计约46%；编辑器部分占诊断窗口GT约7.9%，不能认定“大部分整帧开销都是编辑器UI”。整帧多数时间仍在Slate范围之外。本轮没有实施UI降频或缓存优化。

## 参照与验证边界

- 编译前保留30秒诊断关闭基线：CSV整帧34.504ms。仅用于可追溯，不用于计算优化收益。
- 开启回调采30秒：CSV整帧27.138ms/P95 32.470ms，GT27.127ms，GPU10.612ms；完整Trace帧1105。
- 关闭回调后另采30秒：CSV整帧26.122ms/P95 32.126ms，GT26.119ms，GPU10.975ms；完整Trace帧1147，Slate4.980ms，Prepass1.910ms。
- 两份相邻采样的Prepass均值差0.0925ms，仅作为诊断影响检查；真实玩法负载与Actor数量仍在变化，不能把差值全部认定为回调成本。没有从整帧差值宣称优化收益。
- 三份有效采样的相机、视口及采前采后CVar记录相符。Actor数量和World时间变化有记录，未冻结玩法或恢复代码。此场景不是600移动单位和500弹丸压力对照，未执行整版三轮FPS矩阵或打包版验证。
- 本轮静态检查及实际CPP编译通过；脚本AST检查通过，逐帧分类恒等验证通过。新增诊断默认关闭，专服及游戏Runtime模块不增加Slate委托。
- 首次with-probe只完成采样准备，因UE自动写UTF-16 JSON造成解码错误，立即停止回调。脚本补齐BOM识别和异常清理后有效补采完成；失败准备目录无有效Trace/CSV，排除性能结论。
- 收尾核对Trace关闭、三个World计数均停止、`r.GPUCsvStatsEnabled`恢复为0、三个PIE World均未暂停。PIE留给用户继续操作。没有改动或保存地图、资产，无新增玩法验收场景需求。

采样/身份、逐帧明细、原生回调数据、Trace、CSV和日志集中于[完整报告](../../outputs/performance/20261010-slate-prepass-live/report.md)，控制变量和构建证据见[audit.json](../../outputs/performance/20261010-slate-prepass-live/audit.json)，收尾见[恢复状态](../../outputs/performance/20261010-slate-prepass-live/restored-live-state.json)。离线使用Launcher的UnrealInsights分析源码Editor Trace，未运行Launcher Editor，也未用其作为构建证据。
