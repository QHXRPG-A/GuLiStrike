# 客户端 CPU 增量维护与 10Hz 刷新：实施与验证

日期：2026-09-15～16。工作项：WORK-20260915-004。

## 结论

五条客户端路径已实现，源码版 Editor、Game Development 构建成功，现有四份测试文件覆盖的 42 项自动化全部通过。**性能验收未通过**：前后各六轮都保留了完整证据，但实际移动人数未达到既有门槛，不能据此宣称达成 1200 人持续移动性能目标。

本轮保持工作区原有服务端模拟、姿态协议、插值行为、画质和显示距离。源码差异以开工时保存的工作区副本为基准，不包含原有其他任务的修改。

## 实现

| 路径 | 最终行为 |
|---|---|
| 名册 | Replicator 本地 TMap 保存状态值；FastArray 接收结束合并增删改通知；FindSoldierState 为哈希查询，姿态验证用 ContainsSoldier；代次变化重建一次 |
| 实例池 | 增删、兵种迁移按 ID 合并；失败分配独立 10Hz 重试；死亡保留身份，真正移除清理映射；变换数组持久复用，按变化槽位提交 |
| ISM | 普通变换和颜色走原生实例更新，bMarkRenderStateDirty=false；结构变化使用引擎接口；位置插值、实例位置提交保持每帧 |
| Mass | 位置每 0.1 秒采样；离散状态及时提交；一批数据一个安全延迟设置命令；句柄及本地存活令牌隔离旧命令；保留未激活实体的最新状态 |
| 小地图 | UMG 宿主与两个 SInvalidationPanel 子层；动态采样 10Hz；地形/动态层分别失效；选择、死亡、阵营及表现位移事件更新 |
| 血条 | 选中和受击提示活动集合；槽位回收复用；每帧只跟随活动条；空闲关闭 Tick，独立 0.1 秒维护依赖和过期项；共享 Actor 血条使用同样机制 |

低频调度不补跑卡顿积压。快路径没有新增 100ms 姿态等待。SnapshotRevision 仍不代表全部复制数据到齐。新增缓存、事件和令牌均不参与网络复制。

实施中修正了两个实际问题：预留但尚未创建的 Mass 实体不能交给原有按 chunk 销毁的命令，因此销毁批次在安全边界过滤有效句柄后使用句柄批量接口；快照中的普通朝向不能单独触发可靠位移更新，新的位移代次才携带该朝向。二者均有回归覆盖。

## 构建与自动化

- 引擎：`D:/UnrealEngine-5.7`。
- 最后构建：GuLiStrikeEditor Win64 Development，退出码 0，见 [editor-build-9.log](editor-build-9.log)；GuLiStrike Win64 Development，退出码 0，见 [game-build-6.log](game-build-6.log)。并发上限 3。
- 引擎、项目及 GuLiFlightNavigation 插件的 BuildId 一致：`26d441ba-b96a-4e7b-b104-c2c6cd3e663c`，见 [final-buildids.json](final-buildids.json)。
- 源码版 UnrealEditor-Cmd：42 成功、0 失败、0 未运行，见 [Automation-5/index.json](Automation-5/index.json)。
- 新用例仅追加到既有网络、表现性能、血条、小地图四份测试文件。覆盖值缓存/交换删除/同数量换人/代次、槽位迁移及失败重试、调度卡顿、预留实体和过期命令、活动条休眠、缓存通知及原生 ISM 脏标志。
- 初次页面文件不足、期间链接占用等失败日志保留。最终成功日志与最后源码哈希才是构建依据。

六轮修改后采集使用 Editor build 8 / Game build 5；之后**只拆分了小地图两个计时标签**，未改变绘制或调度逻辑，重新完成了上述双目标构建和 42 项测试。对照 [after-source-manifest.json](after-source-manifest.json) 与 [final-source-manifest.json](final-source-manifest.json)。六轮旧标签 `GuLiCommanderMiniMap_TerrainPaint` 实际包含两个缓存层的绘制；以下均按合计解释，不能当作地形单层次数。

## 采集条件

使用 [run_capture.ps1](run_capture.ps1) 复用现有压测：1200 人，单客/双客各三轮，固定镜头，1080p Epic，屏幕百分比 100%，VSync 关闭，客户端不限帧，8 秒折返移动，先连接再按 100 人批次生成；客户端采集 40 秒，服务端 50 秒。每轮取所有进程共同覆盖的 QPC 30 秒窗口。前后测量期间均未构建或导出 Insights。

原参数首轮触发当前工程车避障工作的 GroundCrowd MaxAgents=1024 断言，该轮无效。本轮所有正式前后采集均用相同启动参数临时覆盖 MaxAgents=2048，未修改项目配置。不能与早期不同条件的历史压测直接拼接。

源码引擎的运行时生成 trace。源码安装未提供 Insights 程序，离线导出使用 Launcher 5.7 的 UnrealInsights **只读分析器**；没有用 Launcher 构建或运行项目验收。导出脚本按同一窗口过滤，并去除了 GameThread 导出中重复的纯 GPU 统计行。

### 整帧结果

下表为三轮各自统计结果的中位数，单位 ms。不是合并所有帧后重新计算的分位数。

| 场景 | 修改前平均 / P95 / P99 | 修改后平均 / P95 / P99 | 实测移动人数 P05：前 → 后 |
|---|---|---|---|
| 单客 | 10.981 / 14.562 / 15.852 | 10.427 / 14.238 / 16.122 | 601～900 → 0 |
| 双客 Client 1 | 18.364 / 22.256 / 23.803 | 18.244 / 22.075 / 23.557 | 860～908 → 832～902 |
| 双客 Client 2 | 18.413 / 22.290 / 23.892 | 18.221 / 22.064 / 23.849 | 855～905 → 802～904 |

人数均为 1200，但所有客户端共同窗口的实际移动人数 P05 都低于 1140。虽然观测 P95 低于单客 16.67ms / 双客 33.33ms，**负载验收不成立，收益也不能按整帧数字直接归因**。单客修改后更多静止帧尤其不能算作优化收益。

各轮明细和窗口：[frames-comparison.csv](frames-comparison.csv)、[comparison.json](comparison.json)。

### CPU 路径与调用次数

以下取双客 Client 1 三轮的中位数，以较接近的实测移动负载观察机制。耗时为 GameThread scope 的 inclusive 总时间除以窗口帧数，单位 ms/帧；父子计时存在包含关系，不能相加。

| 路径 | 修改前 ms/帧 | 修改后 ms/帧 | 30 秒内调用次数：前 → 后 |
|---|---:|---:|---|
| 表现主循环 | 1.31654 | 0.56874 | 1616～1649 → 1624～1666，每帧保留 |
| 其中：插值 | 未独立埋点 | 0.35172 | 修改后 1624～1666 |
| 其中：变化实例提交 | 未独立埋点 | 0.15520 | 修改后 1624～1666 |
| 名册值缓存增量应用 | 未独立埋点 | 0.00064 | 修改后 28～32 个接收批次 |
| 正常窗口全量池校准 / 结构增量 | 旧逻辑在每帧主循环内 | 0 / 0 | 修改后 0 / 0；结构未变，不受普通命令状态通知影响 |
| Mass 延迟命令执行 | 0.21571 | 0.02927 | 1616～1649 → 300 |
| Mass 位置采样 / 提交 / 执行 | 未独立埋点 | 0.02609 / 0.02603 / 0.02913 | 修改后三者各 300；采样包含提交，勿相加 |
| 小地图 UMG Paint（含原绘制） | 0.45449 | 0.08819 | 每帧轻量宿主保留，子层合计仅 294～300 次 |
| 小地图动态采样 | 0.02421（旧 Widget 计时） | 0.03745 | 前后均 300；主要收益来自绘制缓存 |
| 空闲血条实例更新 | 0.13184 | 0 | 1616～1649 → 0 |
| 血条独立维护 | 未独立埋点 | 0.00163 | 修改后 300 |
| DeferredRenderUpdates | 0.53524 | 0.42638 | 仍为每帧，不能宣称该阶段消失 |
| 全场景 Recreate | 0.23514 | 0.00073 | 3143～3206 → 67～75 |

`RebuildLocalInstances` 保留了原函数和计时名称；新实现正常运行时执行插值与变化实例提交，不代表每帧重建实例池。全场景残余 Recreate 还包含引擎/场景其他组件；本轮普通士兵/光圈/血条变换和颜色通路不再显式要求完整代理重建。

九份修改后客户端窗口中，Mass 常规位置采样/批次均为 300 次，10Hz；小地图采样均为 300 次；空闲血条逐帧更新、正常窗口池全量校准均为 0。源码测试同时检查立即事件不被周期门限阻塞，以及卡顿后不补跑。

### 内存

修改后 1200 人场景显式容器内存的跨轮高水位：

| 缓存 | 字节 |
|---|---:|
| 名册状态值缓存 | 202188 |
| 表现实例及 Mass 镜像相关容器 | 620844 |
| 小地图数据容器 | 69680 |
| 空闲血条数据容器 | 64 |
| 上述高水位相加 | 892776（约 872KiB） |

这些计数只覆盖埋点列出的容器分配，不是全部客户端缓存：不含全部嵌套分配、引擎 ISM/Slate 缓存绘制数据、GPU 内存。各项高水位也未必同时出现。基线没有对应细分计数，不能声称这是净增加量。

共同窗口进程采样（三轮均值的中位数）：

| 场景 | CPU 核等效，前 → 后 | Working Set MiB，前 → 后 | Private MiB，前 → 后 |
|---|---|---|---|
| 单客 | 5.67 → 5.10 | 5821.30 → 5784.90 | 11618.76 → 11513.54 |
| 双客 Client 1 | 3.41 → 3.44 | 5846.67 → 5826.08 | 11601.87 → 11582.28 |
| 双客 Client 2 | 3.43 → 3.33 | 5812.13 → 5750.31 | 11560.49 → 11565.35 |

进程数值包含引擎全部工作，也受帧率、异步任务及实际移动负载影响；不能用它代替上述路径计时。

## 鼠标与界面回归

- 复用原有 Slate PointerQA → PlayerInput → 控制器 → 网络命令路径，未注入 OS 鼠标按钮。蓝方客户端选中 313 号，三次右键分别被服务端接受；实际姿态包含前进、反向及停止，最后 2 秒单帧最大变化 0.009cm。
- 该次 PIE 首次超过 1cm 的表现移动发生在首次鼠标事件后约 345.697ms；最大帧间隔 62.574ms、最大单帧位移 203.346cm。它是当前场景的观测值，没有在本轮新增响应时间通过阈值。
- 旧捕获脚本返回失败，因为它把 `BeginPredictedMove` 的 trace 事件当成派发证明；工作区原有控制器明确关闭位移预测，仍保留命令反馈和权威插值。三次移动的服务端接受记录与姿态变化已另外保留，不能把脚本失败改写为脚本通过。
- 首次尝试选中敌方成员被正常拒绝，未计入蓝方移动结果。
- 独立运行血条：初始 Tick 关闭且 0 槽；选中 20 人后 Tick 开启、20 槽；伤害后 HUD 为 1950/2000；取消选择后受击提示继续，过期后 Tick 再关闭；再次选择并杀死 5 人后为 15 人、1500/1500。槽位容量保留供复用，空闲不继续逐兵更新。
- 小地图截图已检查地形、阵营标记、选择及血条；约 90° 朝向下隐藏/重新显示正常，显示后的点击把镜头移到地图中心附近，中心取像素存在约一个像素的换算误差。
- 复用现有 StartPIE 的两种窗口尺寸：原生输入快照分别为 936×706、1280×964，UI 缩放分别为 0.65305、0.89238；两种尺寸均检查截图及地图中心点击。PIE 内 `r.SetRes` 未改变窗口，因此没有把该命令的提交当成 DPI 验证，实际依据是重新启动的第二种尺寸。
- 最后一次短时 PIE 已正常结束，编辑器仍停留在原地图；没有脏关卡或资产包，见 [editor-restored-final.json](editor-restored-final.json)。

证据：[mouse-observations.json](mouse-observations.json)、[服务端接受及中断日志摘录](mouse-ack-and-ui-crash-excerpt.log)、[原有鼠标脚本清单](../../outputs/commander-cpu-20260916/mouse-blue-client/manifest.json)、[血条选中截图](../../outputs/commander-cpu-20260916/standalone-selected.png)、[小地图重新显示截图](../../outputs/commander-cpu-20260916/short-ready-reshown.png)。

DPI 证据：[第二种窗口原生快照](../../outputs/commander-cpu-20260916/dpi-new-window.json)、[显示截图](../../outputs/commander-cpu-20260916/dpi-new-window.png)、[点击结果](ui-dpi-map-click.json)。

Python 对 FGeometry 的取值在本环境返回零尺寸，父容器也如此，因此没有把 Python 几何探针作为布局验证依据；原生 HUD QA 的 blocking_geometry_ready、原生输入快照及实际 Slate 点击和截图作为界面证据。

## 未通过项与范围外问题

1. **持续移动负载门槛未通过，整体性能验收未通过。** 修改后单客三轮服务端掉步分别 95、70、20；首轮服务端 95 帧中 `Exclusive/GameThread/NavigationBuild` 平均 487.168ms/帧。双客修改后三轮掉步 0、0、1。服务端导航负担是观测到的瓶颈，不将整帧差异全部归因于客户端改动，也未在本轮修改服务端导航。
2. 两轮早期修改后尝试 `after-capacity-1-n1200-c1`、`after-capacity-r1-n1200-c1` 因本轮当时错误的朝向比较触发 79904 字节可靠 bunch 超限而无效；已修复并补回归，正式 final 六轮均完成。不得将该错误归为已有工程问题或把失败轮纳入结果。
3. 两次较长 PIE 界面运行被已有建筑入口坡道地面断言中断：`GuLiGroundAccessRampComponent.cpp:37`，`Building access ramp needs a ground surface at its entrance`。相关源文件本轮未改，见 [editor-ui-acceptance.log](editor-ui-acceptance.log)、[editor-ui-retry.log](editor-ui-retry.log)。短窗口补验用于完成本轮界面检查，不宣称长时间稳定性通过。
4. 旧鼠标捕获脚本的预测派发判据与当前工作区行为不一致，结果保留为失败/不适用，未扩大四份已授权测试文件之外的原生测试范围。

## 复现与源码边界

- [baseline-source-manifest.json](baseline-source-manifest.json)：开工工作区原始哈希及副本；[final-source-manifest.json](final-source-manifest.json)：最终源码。
- [implementation.diff](implementation.diff)、[changed-files.json](changed-files.json)：相对开工副本的本轮改动。
- [export_traces.py](export_traces.py)、[analyze.py](analyze.py)：只读导出/分析工具；`comparison.json` 保留所有选定 scope 的次数、Hz、inclusive/exclusive 耗时、每次和每帧耗时。
- Config/DefaultEngine.ini、Config/DefaultGame.ini、Scripts/run_commander_move_stress.ps1 与开工哈希一致。原有其他工作区修改保留。
- Progress索引构建/核对及全量检查退出码均0，0错误、12条既有文档体积/任务数提示，见 [progress-check.json](progress-check.json)。

后续应先恢复符合既有门槛的持续移动负载，再以相同条件复测整体性能；同时单独跟进建筑入口坡道断言。本轮开发记录保持 verification 阶段，不标记 done。
