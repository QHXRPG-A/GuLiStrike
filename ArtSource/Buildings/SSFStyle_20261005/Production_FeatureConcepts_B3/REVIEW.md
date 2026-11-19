# 六座 SSF 建筑 · Blender 材质 B3

01–06 场景上方是 A5 原图，下方是对应的可编辑红蓝模型。Scene Properties → Custom Properties 的 SSF Stylization 设为 0 可看纯材质，设为 1 可看内部线稿、外轮廓和三档明暗。 [Blender 工程](SSF_A5_Materials_B3.blend) · [六座总览](Previews/SSF_Overview_B3.png)

B3 将分档光移到侧面。按单栋场景相机方向对几何表面采样，暗档约占 15%–22%，亮部与中间档在六座建筑上都有覆盖。最终模式由三档决定明暗，金属与玻璃使用各自粗糙度的独立反光。回读确认 60 个纯材质节点图、分件和内嵌原图与 B2 一致。

| 建筑 | 配色与纯材质 | 线稿和三档 |
|---|---|---|
| 空军基地 | [同机位对照](PureMaterials/AirBase_Materials.png) | [同机位对照](Previews/AirBase_B3.png) |
| 克隆中心 | [同机位对照](PureMaterials/CloningCenter_Materials.png) | [同机位对照](Previews/CloningCenter_B3.png) |
| 指挥中心 | [同机位对照](PureMaterials/CommandCenter_Materials.png) | [同机位对照](Previews/CommandCenter_B3.png) |
| 军工厂 | [同机位对照](PureMaterials/MilitaryFactory_Materials.png) | [同机位对照](Previews/MilitaryFactory_B3.png) |
| 反应堆 | [同机位对照](PureMaterials/Reactor_Materials.png) | [同机位对照](Previews/Reactor_B3.png) |
| 战略中心 | [同机位对照](PureMaterials/StrategyCenter_Materials.png) | [同机位对照](Previews/StrategyCenter_B3.png) |

A5 的局部五金及舱内细节按原模型分件做了材质还原。配色和材质的最终外观与线稿强度待用户逐栋核验。 [材质回读与分档覆盖记录](B3_Material_Manifest.json)
