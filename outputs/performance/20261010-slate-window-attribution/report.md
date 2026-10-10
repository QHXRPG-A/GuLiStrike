# Slate窗口与游戏UI耗时拆分

本轮复用此前Client1实战PIE的30秒Trace，按**902个完整引擎帧**重新归属窗口和游戏UI。没有重启PIE、修改代码、资源、质量、镜头或游戏入口。开始本轮分析时，实时编辑器Python桥返回PIE World为空，当前PIE已经结束；本轮结果对应原采样时刻。

## 结论

不能认定“大部分Slate耗时都是编辑器UI”。已经归属的编辑器主窗口其余绘制平均1.138ms，约占Slate总量21.6%；两个客户端游戏UI合计1.655ms，约占31.5%。最大的未归属项是1.962ms的Prepass，占37.3%；原Trace只记录全部窗口的布局总时长，没有每窗口Prepass边界，不能将这部分直接记为编辑器。

## 互不重叠的拆分

| 分类 | 平均ms/引擎帧 | 独立P95 ms | 平均占Slate比例 |
|---|---:|---:|---:|
| 编辑器主窗口其余绘制（扣除Client1视口，含窗口处理） | 1.138 | 1.497 | 21.6% |
| Client1游戏UI更新与绘制 | 0.965 | 1.552 | 18.4% |
| Client2游戏UI更新与绘制 | 0.690 | 1.164 | 13.1% |
| 全部窗口布局预计算Prepass（尚未分窗） | 1.962 | 2.184 | 37.3% |
| 游戏视口、窗口壳及其余公共Slate处理 | 0.500 | 0.641 | 9.5% |
| Slate合计 | 5.255 | 6.595 | 100% |

每帧均验证上述五项之和等于该帧`Slate::Tick (Time and Widgets)`，且无负项。各列P95来自各自分布，不能相加。这里是CPU游戏线程成本，不是GPU UI成本或整个3D视口渲染时间。

原报告5.262ms使用窗内计时器汇总，并包含一个边界Slate调用；本轮严格保留902完整引擎帧得到5.255ms。约0.007ms差异来自边界筛选，不代表优化收益。

## 实际物理窗口与身份

采样时实际上有两个Slate绘制窗口：

| 物理窗口 | 窗口绘制平均ms | 其中游戏视口平均ms | 其中游戏UI平均ms |
|---|---:|---:|---:|
| 编辑器主窗口内嵌Client1 | 2.141 | 1.003 | 0.965 |
| 独立Client2 PIE窗口 | 0.773 | 0.715 | 0.690 |

主窗口绘制中实际包含Client1，不能将其整个2.141ms都称为编辑器UI。Client2的0.773ms同样包含窗口壳和视口处理。

归属依据为每个`Slate::DrawWindow`的真实子树：主窗口有`SLevelEditor [LevelEditor.cpp(245)]`，游戏控件路径带`:GameInstance_1.`；独立窗口有`SPIEViewport [PlayLevel.cpp(3383)]`，游戏控件路径带`:GameInstance_2.`。902帧都验证每个窗口只含一个游戏实例，按内容归属，不按调用顺序、耗时大小或帧数猜测。

`GameInstance_1`与Client1、`GameInstance_2`与Client2还通过原先按World保存的场景UI计时交叉核对：Client1原生Update/Paint为0.294/0.406ms，对应实例包装Tick/Paint为0.301/0.420ms；Client2原生为0.312/0.140ms，对应实例包装为0.323/0.151ms。包装Scope略宽，不能据差值判断退化。UI控件实例身份能拆窗口，不改变原World Tick仍使用客户端A/B的边界。

## 游戏UI内部

Client1完整游戏UI平均0.965ms，其中场景UI包装Tick/Paint为0.301/0.420ms，Commander HUD Paint为0.221ms。Client2完整游戏UI平均0.690ms，其中场景UI包装Tick/Paint为0.323/0.151ms，Commander HUD Paint为0.194ms。

小地图Paint为Client1 0.060ms、Client2 0.059ms，已经嵌套在Commander HUD Paint内，不另加。`Paint: Game UI`包含该次绘制中的控件Tick及所有HUD子控件，不是仅场景UI，也不是只有GPU提交。

## 未能补齐的布局分窗

UE5.7源码`FSlateApplication::DrawPrepass`在遍历窗口前只有一个`Slate::Prepass` Scope；`PrepassWindowAndChildren`内没有按窗口命名的CPU Scope。原捕获仅启用了cpu/gpu/frame/bookmark，没有可用的Slate窗口WidgetId与完整布局层级证据，因此此后不能从总量重建每窗口布局时间。

完整归属这1.962ms需要在同场景PIE中补采能覆盖每窗口Prepass的诊断标记，并验证窗口身份。当前未执行这项补采，不能把编辑器窗口总绘制、两个游戏UI与未知布局简单混算，也不能保证单客户端Standalone按比例节省全部Slate时间。

## 证据与复现

- 来源：[原采样报告](../20261010-client1-live-frame-analysis/report.md)、[原Trace](../20261010-client1-live-frame-analysis/session.utrace)。
- 导出：[export.py](export.py)、[命令](export-commands.txt)、[选定窗口Scope事件](slate-events.csv)、[来源信息](source.json)。UnrealInsights仅作为离线分析器使用，没有运行Launcher编辑器或生成新的游戏样本。
- 分析：[analyze.py](analyze.py)、[每帧互斥拆分](frames.csv)、[完整摘要及身份例子](summary.json)、[报告脚本](build_report.py)。
- 复现：依次执行`python export.py`、`python analyze.py`、`python build_report.py`；均只读原Trace并写此输出目录，不操作PIE或游戏资源。

本轮完成窗口绘制与游戏UI归属；每窗口Prepass仍未测量。原三项优化的玩家验收和暂缓FPS配对矩阵状态不变。
