# 六座 SSF 建筑 · A5 导轨模型边对位 B13

Blender 场景 01–06 逐栋显示 A5 原图和同机位红蓝 LOD0。重开工程时按 `Z` 选择“渲染”，场景属性 `SSF Stylization` 设为 1。 [Blender 工程](SSF_A5_ModelEdgeInk_B13.blend) · [红蓝总览](Previews/SSF_Overview_B13.png)

[逐笔对照页](EdgeReview/index.html) 默认显示 13 条待确认笔画。琥珀线标出 A5 原图笔画，青线标出实际落线的模型边，虚线标出参考笔画在模型上的估算位置。打开“显示局部原网格”可同时查看模型的折边与其他网格边，含遮挡处。取消“仅看未确认项”可核对全部 170 笔。模型局部取自 A5 底模的原正交机位，模型摆位回读误差在 850×630 坐标下小于 0.1 像素。

空军基地左侧弧形压条的两条纵边分别沿原网格接续。四道横纹按参考图在两侧纵边间的相对宽度，落到同层的四道原折边。近端导轨采用分件独立对位，确认两道横折边，线条起止限制在槽宽内。

| 建筑 | 有对应原边的参考笔画 | 同机位审核 |
|---|---:|---|
| 空军基地 | 25／32 | [对照图](Previews/AirBase_B13.png) |
| 克隆中心 | 36／37 | [对照图](Previews/CloningCenter_B13.png) |
| 指挥中心 | 28／29 | [对照图](Previews/CommandCenter_B13.png) |
| 军工厂 | 26／29 | [对照图](Previews/MilitaryFactory_B13.png) |
| 反应堆 | 21／21 | [对照图](Previews/Reactor_B13.png) |
| 战略中心 | 21／22 | [对照图](Previews/StrategyCenter_B13.png) |

回读核对了 838 个原网格边段、28 条连续走线和 6 条按局部宽度截取的横线。157 条参考笔画有对应原边，其中部分只对应一段。其余 13 条逐笔保留差异记录。战略中心上段交叉撑的另一笔经原顶点复查，附近反向实体斜边位于下段。模型仍为六份 LOD0，共 24,746 个三角面。逐笔外观对位待 Blender 审核。 [逐笔复核](edge_correspondence.json) · [模型边回读](B13_Edge_Ink_Manifest.json)
