# 六座 SSF 建筑 · B22 描线加密加粗

[UE 战术总览](Previews/SSF_B22_UE_RTS_Overview.png) · [指挥中心与军工厂近景](Previews/SSF_B22_UE_Command_Factory.png) · [Blender 工程](SSF_A5_StructureInk_B22.blend)

内线从 838 段增加到 1,787 段。原有线宽多数增至 1.65 倍，窄面根据实际宽度限幅。外轮廓壳偏移增至 2 倍。线色采用色库的 `#150017`，边缘使用窄幅抗锯齿。新增线落在主要面板的折边、分件边缘与观察环实际折边。

| 建筑 | B21 内线 | B22 内线 | 红蓝对照 |
|---|---:|---:|---|
| 空军基地 | 128 | 241 | [空军基地](Previews/AirBase_B22.png) |
| 克隆中心 | 156 | 336 | [克隆中心](Previews/CloningCenter_B22.png) |
| 指挥中心 | 111 | 314 | [指挥中心](Previews/CommandCenter_B22.png) |
| 军工厂 | 76 | 168 | [军工厂](Previews/MilitaryFactory_B22.png) |
| 反应堆 | 127 | 273 | [反应堆](Previews/Reactor_B22.png) |
| 战略中心 | 240 | 455 | [战略中心](Previews/StrategyCenter_B22.png) |

B21 的组件纯色、红蓝分区和 40%／68%／100% 三档明暗继续生效。每队 LOD0 为 24,746 三角面。主体顶点、UV 和骨骼权重已核对，轮廓壳顶点位置随本轮线宽调整。[描线和网格清单](B22_Structure_Ink_Manifest.json)

本会话已完成。交付时在 UE 的 `LVL_CommanderMassPrototype` 布置了十二座临时预览，回读覆盖六个网格、材质、摆位、线稿数据和截图文件。B22 的记录覆盖 Blender 成品及 UE 临时预览。

## 重建 UE 预览

用 Blender 打开 B22 工程，在后台运行 [导出脚本](build_structure_ink.py)，传入 `-- --export-preview <TEMP>/GuLiStrike-SSF-B22-scene`。该命令从已保存的工程重建六份预览 FBX 和对应清单。随后在打开 `LVL_CommanderMassPrototype` 的编辑器中执行 [摆位脚本](stage_structure_ink.py)。[十二座摆位](UE_Preview_Layout.json)
