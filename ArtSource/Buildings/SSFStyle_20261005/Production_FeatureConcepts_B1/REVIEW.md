# 六座 SSF 建筑 · Blender 材质 B1

打开 Blender 的场景下拉菜单。总览包含六座建筑的红蓝版本，01–06 场景上方是对应的 A5 原图，下方是可编辑的模型。每组左蓝右红。 [打开 Blender 工程](SSF_A5_Materials_B1.blend) · [总览图](Previews/SSF_Overview_B1.png)

在任一场景的 Scene Properties → Custom Properties 中，把 SSF Stylization 设为 0 可查看纯材质，设为 1 可查看内部线稿、轮廓线和三档明暗。工程保存时使用 1。

| 建筑 | 已落实的材质重点 | 对照 |
|---|---|---|
| 空军基地 | 暖砂色分瓣舱门、浅色导轨、深色舱缝与小面积琥珀标记 | [A5 与 Blender](Previews/AirBase_B1.png) |
| 克隆中心 | 五个青绿舱窗、石墨色软管、浅色锁扣及两侧队色设备罩 | [A5 与 Blender](Previews/CloningCenter_B1.png) |
| 指挥中心 | 上层观察玻璃、浅色环缘与下层独立状态灯 | [A5 与 Blender](Previews/CommandCenter_B1.png) |
| 军工厂 | 四段暖砂卷门、深色弯管、浅色管箍及琥珀护角 | [A5 与 Blender](Previews/MilitaryFactory_B1.png) |
| 反应堆 | 三组三片散热栅、浅色壳板、深色锁体及琥珀握柄 | [A5 与 Blender](Previews/Reactor_B1.png) |
| 战略中心 | 深色阶梯阵列、逐片浅色边框、回转座与队色机箱 | [A5 与 Blender](Previews/StrategyCenter_B1.png) |

正式 LOD0 提供网格、蒙皮权重、骨骼与原 UV。A5 里的局部五金和舱内细节按现有结构通过材质处理。红蓝共用网格和固定材质，队色使用各自的材质槽覆盖。

保存后回读了 12 个模型、61 个带线稿和三档控制的材质、两档场景切换值以及六张内嵌 A5 原图。原图与成品的外观一致性由用户在 Blender 中核验。 [材质与回读清单](B1_Material_Manifest.json)
