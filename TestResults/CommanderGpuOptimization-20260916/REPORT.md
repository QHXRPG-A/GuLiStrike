# GPU渲染降耗实施与对照（2026-09-16）

## 结果

最终采用 **B配置：软件VRS开启**。TSR已替换为FXAA，体积雾关闭，VSM与Lumen GI按确认参数降档。

下表均为各轮共同30秒窗口统计后，再取三轮中位数；GPUTime为CSV指标，P95为实际整帧时间。

| 场景 | A GPU ms | B GPU ms | C GPU ms | 最终较A下降 | A整帧P95 ms | 最终整帧P95 ms |
|---|---:|---:|---:|---:|---:|---:|
| 单客 | 9.204 | 6.334 | 7.364 | 31.2% | 14.464 | 13.212 |
| 双客 C1 | 23.318 | 17.344 | 16.255 | 25.6% | 32.334 | 25.011 |
| 双客 C2 | 22.223 | 18.405 | 16.581 | 17.2% | 30.739 | 26.199 |

本次A基线与此前报告的机器耗时状态不同；优化百分比仅以本轮重新采集的A计算，不能直接套用到先前5.65/14.4ms基线。各阶段并行，GPU、游戏线程、渲染线程耗时不能相加。

## 软件VRS决策

| 场景 | C较B GPU下降 | 整帧P95变化（正为恶化） | 满足约定条件 |
|---|---:|---:|---|
| 单客 | -16.25% | +1.33% | 否 |
| 双客 C1 | 6.28% | -3.91% | 是 |
| 双客 C2 | 9.91% | -8.42% | 是 |

判据：三个客户端组合的GPU平均耗时中位数均至少降低3%，且每组整帧P95恶化均不超过3%，才关闭软件VRS。没有根据单个异步事件的跨度推算节省。

## GPU分项与未解决热点

| 场景 | TSR A→B ms | 体积雾 A→B ms | Lumen反射 A→B ms | ShadowDepths A→B ms |
|---|---:|---:|---:|---:|
| 单客 | 1.164 → 0.000 | 0.423 → 0.000 | 0.527 → 0.197 | 1.136 → 1.167 |
| 双客 C1 | 3.016 → 0.000 | 1.235 → 0.000 | 1.258 → 0.511 | 2.853 → 3.092 |
| 双客 C2 | 2.872 → 0.000 | 1.182 → 0.000 | 1.203 → 0.507 | 2.684 → 3.203 |

**阴影深度没有降耗**：VSM目标偏置已生效，但单客略增、双客增加。降低分辨率没有消除几何提交、栅格化和缓存失效成本；本轮没有逐对象隔离，不能断言是哪类网格造成。此前场景审计发现1024个非Nanite地形组件，默认士兵不投影，后续应从地形/环境阴影及缓存更新继续归因。[前次归因](../CommanderGpuAnalysis-20260916/REPORT.md)

TSR、体积雾事件在B/C全部消失；反射分项下降，但这是整组改动后的结果，不能把它完全归因于单位材质。FXAA仍有自身成本。

## 线程等待与异步队列

| 场景 | A渲染线程 ms | 最终渲染线程 ms | A Visibility ms/帧 | 最终 Visibility ms/帧 |
|---|---:|---:|---:|---:|
| 单客 | 12.104 | 10.672 | 3.944 | 2.927 |
| 双客 C1 | 26.826 | 21.076 | 18.301 | 12.783 |
| 双客 C2 | 25.573 | 22.297 | 17.040 | 13.894 |

已有源码与trace归因确认：GPU遮挡查询未就绪 → 可见性工作线程等待GPU → 渲染线程等待可见性任务。本轮27份trace继续保留对应等待事件，B中Visibility缩短，但仍占双客每端约12.8～13.9ms/帧，不能作为可直接删除的CPU计算。

| 场景/配置 | LumenScreenProbeGather异步跨度 ms/次 | ContrastAdaptiveShading异步跨度 ms/次 |
|---|---:|---:|
| 单客/A | 2.707 | 1.920 |
| 单客/B | 2.263 | 1.708 |
| 单客/C | 2.518 | 0.000 |
| 双客 C1/A | 6.413 | 4.426 |
| 双客 C1/B | 5.680 | 4.002 |
| 双客 C1/C | 5.421 | 0.000 |
| 双客 C2/A | 6.098 | 4.135 |
| 双客 C2/B | 5.853 | 4.388 |
| 双客 C2/C | 5.536 | 0.000 |

以上是事件跨度，包含重叠、调度和竞争的影响；不等于独占硬件忙时，不能跨队列相加。原生GPU Frame范围还包含队列空闲，因此未用它替代CSV GPUTime。Lumen GI保持异步执行。

事件检查：`{"optimized_no_tsr": true, "optimized_no_volumetric_fog": true, "C_no_CAS": true}`。

## 负载与帧预算

| 轮次 | 客户端 | 移动人数P05 | GPU均值 ms | 整帧P95 ms | 负载达标 | 帧预算达标 |
|---|---|---:|---:|---:|---|---|
| A-1-n1200-c1 | client1 | 1200 | 8.991 | 15.502 | True | True |
| A-1-n1200-c2 | client1 | 1199 | 23.318 | 32.334 | True | True |
| A-1-n1200-c2 | client2 | 1199 | 22.223 | 30.739 | True | True |
| A-2-n1200-c1 | client1 | 1200 | 9.204 | 14.464 | True | True |
| A-2-n1200-c2 | client1 | 1200 | 24.075 | 33.381 | True | False |
| A-2-n1200-c2 | client2 | 1199 | 22.732 | 30.891 | True | True |
| A-3-n1200-c1 | client1 | 1199 | 9.506 | 14.415 | True | True |
| A-3-n1200-c2 | client1 | 1200 | 23.064 | 31.241 | True | True |
| A-3-n1200-c2 | client2 | 1200 | 21.699 | 29.082 | True | True |
| B-1-n1200-c1 | client1 | 1200 | 6.289 | 13.003 | True | True |
| B-1-n1200-c2 | client1 | 1200 | 17.344 | 25.011 | True | True |
| B-1-n1200-c2 | client2 | 1200 | 18.405 | 26.199 | True | True |
| B-2-n1200-c1 | client1 | 1198 | 6.334 | 13.212 | True | True |
| B-2-n1200-c2 | client1 | 1200 | 17.847 | 25.240 | True | True |
| B-2-n1200-c2 | client2 | 1200 | 19.070 | 26.922 | True | True |
| B-3-n1200-c1 | client1 | 1199 | 7.526 | 13.384 | True | True |
| B-3-n1200-c2 | client1 | 1198 | 7.893 | 15.329 | True | True |
| B-3-n1200-c2 | client2 | 1199 | 7.972 | 16.127 | True | True |
| C-1-n1200-c1 | client1 | 1200 | 7.369 | 13.413 | True | True |
| C-1-n1200-c2 | client1 | 1199 | 16.255 | 24.033 | True | True |
| C-1-n1200-c2 | client2 | 1200 | 17.303 | 25.665 | True | True |
| C-2-n1200-c1 | client1 | 1200 | 6.191 | 13.016 | True | True |
| C-2-n1200-c2 | client1 | 1200 | 17.450 | 25.741 | True | True |
| C-2-n1200-c2 | client2 | 1199 | 16.581 | 23.994 | True | True |
| C-3-n1200-c1 | client1 | 1200 | 7.364 | 13.388 | True | True |
| C-3-n1200-c2 | client1 | 1199 | 7.887 | 17.555 | True | True |
| C-3-n1200-c2 | client2 | 1199 | 8.034 | 18.325 | True | True |

18轮服务端零丢步：True。负载门槛沿用存活1200、移动人数P05≥1140；单客P95≤16.67ms、双客≤33.33ms。A的原始超预算轮次照实保留。

B/C第三轮双客GPU约7.9～8.0ms，明显低于前两轮约16.3～19.1ms，存在较大轮间波动，未剔除任何轮次。完整40秒CSV中的DrawCalls仍约613、绘制图元仍约286.5万/帧、GPUScene实例仍约1.07万，与此前轮次接近，不支持因大量对象消失而变快的解释。没有GPU时钟/全机竞争的连续记录，不能确定波动来源；百分比为本轮约定中位数结果，不保证跨时段重复得到同幅度收益。[绘制量核对](draw-counts.json)

## 实现

- `Config/DefaultEngine.ini`：FXAA、关闭体积雾、VSM偏置0/0/1/2、Lumen探针32、辐射缓存16、更新因子64/128；保留GI异步及阴影覆盖/页池。
- `GuLiUnitRenderPolicy`：共享项目材质映射与CDO缓存，无逐士兵MID；关闭目标网格的光追、距离场、动态间接贡献及反射/天空捕获可见性。
- 士兵ISM、僚机Owner/Remote初始化与池复用、矿车/工程车子Actor创建及重建接入。传送缓存先恢复再换身体；受击仍用叠加材质；残骸继承来源单位的反射排除状态。
- 4个项目父材质、9个僚机MIC，12条原材质映射；原始网格及共享导入材质不修改。新目录显式加入Cook范围。
- 玩家主飞船未接入该身体材质策略。粗糙度下限0.8避开当前0.4阈值的专用Lumen反射射线；粗糙GI近似及屏幕空间倒影仍可能存在。[Epic说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine?application_version=5.7)

## 构建、回归与画面

- 源码引擎：`D:/UnrealEngine-5.7`。Editor/Game Development最终构建均成功；引擎、项目及5个涉及插件的7份模块BuildId全部一致。
- 原有37项回归：32项成功、2项成功带提示、3项失败。修改前源码实际重新编译后，同一套用例及顺序复现完全相同的3项失败和错误信息：[回归对照](regression-comparison.json)。失败分别涉及建筑定义ID、战斗事件包大小、僚机攻击通道；不标记为全通过。未新增自动化用例。
- 修改前源码对照完成后，恢复本轮实现并重新编译Editor/Game；9份采样时源码/配置的内容哈希与最终交付一致。构建日志中的实际编译动作是对照依据，早期因文件时间戳导致的Up-to-date尝试已作废，不作为证据。
- [材质图审计](material-graph-audit.json)：4个父材质粗糙度下限0.8，其余输入保持；9个MIC参数保持。
- [士兵运行时组件](runtime-materials.json)、[车辆初建/重建检查](vehicle-materials.json)：两类车辆各8个网格的身体材质正确，采集器发光槽保留，5项反射排除均关闭。[玩家主飞船材质](player-materials.json)仍为原资产。
- [最终编辑器状态](editor-final.json)：原关卡与镜头，无PIE、无临时预览Actor、无脏资产/地图，最终配置为B。
- 同镜头环境：[修改前](scene-before.png) / [修改后](scene-after.png)。单位陈列复现原材质/原参数并保持镜头与变换一致：[原参数](units-before.png) / [优化后](units-after.png)。单位陈列是外观检查，非压测帧。

### 画面检查边界

- 已观察到：身体金属高光减弱，基础色与发光仍在；FXAA下细轮廓锯齿更明显，降精度阴影边缘更粗。环境间接照明仍存在，没有观察到整片照明丢失。
- 远距离小单位连续平移/旋转期间抽取4张画面：[1](units-motion-far-1.png)、[2](units-motion-far-2.png)、[3](units-motion-far-3.png)、[4](units-motion-far-4.png)。抽样中单位及投影均未整体消失，细边缘随角度变化明显。
- 高分辨率截图会打断正常渲染，四张抽样不能证明无时域闪烁，也未覆盖快速转镜头下Lumen收敛或长时间阴影稳定性。动态画质验证记为partial，后续实机游玩验收仍需关注。[抽样记录](motion-preview.json)

## 口径与边界

- 源码Development Editor `-game`，未做Cook/Shipping性能结论。地图、固定镜头、1080p/100%、关VSync/动态分辨率、Epic预设、客户端不限帧/服务器60FPS，与原流程一致。
- 每组1200单位，单客/双客各三轮，客户端40秒、服务端50秒；共同30秒窗口；临时MaxAgents=2048，join-first，8秒折返。B/C按轮次交替，采集无后台编辑器、构建或trace导出。
- CSV与ticker行数一致，并验证零帧偏移的帧时间差最小；以同一ticker的QPC窗口选择CSV行。完整40秒指标同时保留在JSON中。
- 这是整组质量调整的收益，不能分配成每个开关的独立收益，也不能据固定镜头短采样保证全场景或长时间表现。

## 证据与回滚

- [全部帧/GPU比较](comparison.json) · [27份trace队列统计](trace-comparison.json) · [运行设置核验](runtime-validation.json)
- [材质变体清单](material-variants.json) · [配置/代码补丁](implementation.patch) · [回滚说明](ROLLBACK.md)
- [最终Editor构建](editor-build-final.log) · [最终Game构建](game-build-final.log) · [模块BuildId](buildids.json)
- [已有回归](Automation/index.json) · [原源码失败对照](Automation-original-source/index.json)
- [开发记录](../../Progress/DevelopmentDocumentation/20260916-GPU渲染降耗.md) · [实施归档](../../Progress/Archive/20260916-GPU渲染降耗实施与三组对照.md)
