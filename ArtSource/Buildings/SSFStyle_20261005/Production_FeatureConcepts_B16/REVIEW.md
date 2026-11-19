# 六座 SSF 建筑 · A5 贴图局部对位 B16

在 B15 的基础上重烘焙六张表面贴图。按分件使用 A5 的笔画与 B13 已确认的模型边约束局部变换，改善导轨、环板、装甲折边附近的纹理对位。可靠约束不足的区域沿用 B14 的对位。红蓝共用每栋贴图和 UV。

[Blender 工程](SSF_A5_RegisteredDetail_B16.blend) · [红蓝战术总览](Previews/SSF_Overview_B16.png) · [贴图与对位回读](B16_Registered_Detail_Manifest.json)

| 建筑 | 边缘采样平均偏移 | 同机位对照 | 表面贴图 |
|---|---:|---|---|
| 空军基地 | 2.66 → 1.31 px | [对照图](Previews/AirBase_B16.png) | [PNG](Textures/AirBase_A5_SurfaceDetail.png) |
| 克隆中心 | 2.53 → 1.57 px | [对照图](Previews/CloningCenter_B16.png) | [PNG](Textures/CloningCenter_A5_SurfaceDetail.png) |
| 指挥中心 | 3.30 → 1.71 px | [对照图](Previews/CommandCenter_B16.png) | [PNG](Textures/CommandCenter_A5_SurfaceDetail.png) |
| 军工厂 | 3.35 → 1.39 px | [对照图](Previews/MilitaryFactory_B16.png) | [PNG](Textures/MilitaryFactory_A5_SurfaceDetail.png) |
| 反应堆 | 3.15 → 2.02 px | [对照图](Previews/Reactor_B16.png) | [PNG](Textures/Reactor_A5_SurfaceDetail.png) |
| 战略中心 | 2.43 → 1.34 px | [对照图](Previews/StrategyCenter_B16.png) | [PNG](Textures/StrategyCenter_A5_SurfaceDetail.png) |

表内数值基于 850×630 模型参考机位，统计了各局部变换中未用于拟合的 1,213 个边缘采样点。保存后回读核对了原 LOD0 网格、原 UV、B15 功能色贴图、模型边线稿及 40%／68%／100% 三档明暗。A5 未展示的面沿用此前涂装，几何差异以原 LOD0 为准。视觉对位待 Blender 审核。
