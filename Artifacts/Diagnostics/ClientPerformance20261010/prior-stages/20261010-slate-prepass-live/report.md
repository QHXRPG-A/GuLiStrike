# 双客户端 PIE 的 Slate 分窗布局与绘制补采

已完成用户批准的源码版 UE5.7 `GuLiStrikeEditor Win64 Development` Live Coding 编译、加载和补采。编辑器主窗口扣除 Client1 游戏视口后占 Slate 约 **45%**，两个客户端 UI 合计约 **46%**。编辑器部分约占本次 GT **7.9%**；本轮结果不支持“大部分整帧开销来自编辑器 UI”。

使用 `/Game/Maps/LVL_CommanderMassPrototype`，同进程一专服两客户端，PIE 保持运行。Client1 嵌入主窗口，视口 1505×1115；Client2 独立窗口，视口 640×484。三个有效采样窗口各 30 秒，采样期间不截屏、不盘点对象、不写资产。未启动额外 600 移动单位/500 弹丸入口，也没有调整生产优化、质量、相机或地图。所有窗口前后记录的相机、视口及 CVar 相符，Actor 数量随真实玩法变化，不能用三份整帧值计算优化收益。

分窗诊断窗口有 **1105 个完整引擎帧**。每帧两个游戏视口各有一次 Prepass；每帧每窗均有一次 DrawWindow 和 Game UI Paint。实际布局与绘制随引擎帧执行，该窗口约 36.8 次/秒，并无独立固定 30/60 Hz 调度。

| 互不重叠的归属 | 平均 ms/引擎帧 | P95 ms | 占 Slate |
|---|---:|---:|---:|
| 编辑器主窗口，扣除 Client1 游戏视口 | 2.141 | 2.577 | 44.7% |
| Client1 游戏视口 UI 布局、更新和绘制 | 1.053 | 1.522 | 22.0% |
| Client2 游戏视口 UI 布局、更新和绘制 | 1.138 | 1.561 | 23.8% |
| 窗口壳、视口和其余公共 Slate 处理 | 0.459 | 0.590 | 9.6% |
| Slate 合计 | 4.791 | 5.888 | 100.0% |

| 范围 | 布局 Prepass ms | 更新/绘制 ms | 合计 ms |
|---|---:|---:|---:|
| 编辑器主窗口其余部分 | 1.018 | 1.123 | 2.141 |
| Client1 游戏视口 | 0.428 | 0.625 | 1.053 |
| Client2 游戏视口 | 0.515 | 0.623 | 1.138 |

窗口根布局：主窗口 1.446 ms，包含 Client1 0.428 ms；Client2 窗口 0.533 ms，包含其视口 0.515 ms。全局 Prepass 2.002 ms，根窗口外的失效/遍历等残余 0.023 ms。嵌套视口耗时从其窗口扣除，未再次相加。Game UI Paint 中还包含 UObject Tick、HUD、场景 UI、小地图等包装范围，子节点明细也未加入总和。客户端布局范围包含少量视口壳，不把它声称为纯 UMG 布局。

## 参照与诊断影响

关闭诊断回调后另采 30 秒：1147 个完整引擎帧，Slate 平均 4.980 ms，Prepass 1.910 ms；CSV 整帧 26.122 ms（P95 32.126），GT 26.119 ms，GPU 10.975 ms。开启诊断的 CSV 整帧 27.138 ms（P95 32.470），GT 27.127 ms，GPU 10.612 ms。编译前的 30 秒整帧 34.504 ms 也保留，仅用于场景追溯。

带回调与关闭回调的 Prepass 均值差约 **0.0925 ms**。真实玩法负载仍在变化，该差值包含运行波动，不能直接认定全部是回调开销，更不能解释为优化收益。诊断调用累计 6,000,626 次，开始/结束配对错误为 0；诊断已关闭并解除全部 Prepass 委托，GPU CSV 开关已恢复，Trace 与每 World 计数采集均已停止。

每行 P95 独立计算，不能相加得到总 P95。表中都是 GT 上的 CPU Scope，不含 GPU 的 UI 栅格化。关闭编辑器或另一个客户端后的运行帧率不能直接从这些数字推出。

## 编译与加载证据

- 构建引擎与正在运行的 Editor：`D:/UnrealEngine-5.7`，PID 21532。
- 仅编译新增编辑器诊断 CPP，UBT `Result: Succeeded`；Live Coding 记录补丁链接及创建成功，原生 Start/Stop 命令已经实际执行。通过编辑器发起的编译没有由宿主 shell 收集独立退出码，未把补丁加载记录声称为完整普通 DLL 重建。
- 引擎与项目 BuildId 均为 `dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。
- Live Coding 曾输出 `.voltbl` 与新编译单元 contributions 诊断，随后成功创建补丁。命令、四个根目标和计时配对的实测核对通过。
- 新代码默认关闭，仅在 `GuLiStrikeEditor` 中存在，无引擎源码修改。源文件 SHA256 为 `82CEFC1C96B3DF5E02A1C970B541C29E70AA731BE069ED563F9C089536CCC197`。

## 结论的适用范围

游戏 UI 降低逻辑 Tick 频率只能覆盖其更新部分，布局与 Paint 本次仍每帧执行。若后续要减少这部分成本，需要减少不必要的布局失效、绘制工作或使用已验证的缓存，并保留动态内容跟随。编辑器窗口本身的布局/绘制也需要单独处理。当前没有应用新的 UI 优化，也没有验证打包版帧率。

## 证据与重现

- [逐帧分窗明细](probe-r1/window-frames.csv)、[分窗统计与目标身份](probe-r1/window-summary.json)、[原生 Prepass 数据](probe-r1/prepass.json)。
- [分窗 Trace](probe-r1/session.utrace)、[关闭回调 Trace](control-r1/session.utrace)、[控制变量和构建核对](audit.json)、[加载证据](build/verified.json)。
- `capture.py` 通过现有 Python 桥发起原生计数、CPU/GPU Trace 和 CSV；`export.py` 仅在采样结束后离线导出；`analyze.py` 逐帧验证互斥分类之和等于 Slate。
- 离线分析使用 Launcher `UnrealInsights.exe`；它没有启动 Launcher Editor，也不作为源码引擎构建证据。
- 首次 `with-probe` 只完成采样准备，因 UE JSON 自动选 UTF-16 引发解码失败；回调已立即关闭，脚本补充 BOM 识别和异常清理。该目录无有效 Trace/CSV，已排除。随后有效 `probe-r1` 与 `control-r1` 均完成。
