# 战争机器前方方块水平 · v6

根据用户红框，将两个前方方块节点调回水平。节点外壳、橙色装甲、白色套环和黄色灯面一起回正；连接叉耳保持v5下倾，四根主支架等长及30°下倾不变，四个悬浮盘维持原位置、同高和水平。

[Blender审核版](WarMachine_LevelNodes_Review.blend)展示完整模型，[可编辑分件源](WarMachine_LevelNodes_Editable.blend)保留279分件。默认正视相机，中键旋转检查，数字键盘0回相机。[最终整机预览](Previews/After_Hero.png) · [正视](Previews/After_Full_Front.png) · [侧视](Previews/After_Full_Side.png) · [连接斜视](Previews/After_Connection_Oblique.png)。

本轮仅14个前方节点及承接座分件发生变化，其余265件逐一保持一致。前方8个方块/套环网格的面法线与水平/竖直轴对齐；四盘高度差在浮点误差范围，主支架轴线变化0，孔销同轴、盘心贴合，腿盘无检测到表面相交。前方承接座相应改为上下水平。

按用户要求，助手未打开或查看任何本轮预览图片；仅做几何数值检查，画面和装配效果由用户核验。未运行UE、卡牌生图或原生编译。

[几何检查](geometry-check.json) · [保存回读](live-readback.json) · [旧文件保护](protected-files-check.json) · [交付清单](delivery-manifest.json)。[用户标注](References/User_LevelBlocks_Feedback.png)原样保存，旧版[v5](../WarMachine_LevelFeet_v5/README.md)及原始资产保留。

[生产逻辑](../../../Scripts/Blender/war_machine_reference.py)已同步；[v6入口](../../../Scripts/Blender/revise_warmachine_level_nodes.py)的 `prepare` 生成和核对分件，`render` 仅输出最终文件。`SourceSnapshot`保留修改前及本版脚本。用户确认模型后，才继续用新模型截图和v7原文字prompt重绘射速、机动两张卡。

## 后续使用授权 · 2026-09-28

用户明确“开始第二个任务”，要求原文字prompt配更改后的图像重绘射速与机动。据此本版已用于独立UE截图副本及[两图重绘v8](../../UI/WarMachineTarotCards/ModelComic_v8/README.md)。该授权仅涵盖本次参考与两图制作，不记为正式战斗模型替换或卡图美术终验。上文保留模型制作阶段事实，源blend未改。
