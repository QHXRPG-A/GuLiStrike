# 重防号前后等长支架 · v4 审核候选

此为保留的历史候选。用户继续要求前端连接下倾、四盘同高，最新见[v5](../WarMachine_LevelFeet_v5/README.md)；该反馈不记为v4通过。

用户指出v3前两条腿过长，要求与后两条等长，并允许四个悬浮盘不在同一高度。本版保留30°向外下倾和每个盘自身水平，前支架缩短29.5569%，前盘随完整脚节点向内、向上平移。机身、武器、机身侧关节和后腿位置保持v3原样。

[Blender审核版](WarMachine_EqualLegs_Review.blend)已在当前Blender打开，默认整机三分之四视角。中键旋转、数字键盘0回相机；Scene下拉 `Side_Before_After` 查看整机侧视对照，`Connection_Before_After` 查看连接对照。[可编辑分件源](WarMachine_EqualLegs_Editable.blend)保留279个分件及修改器。

![最新整机](Previews/After_Hero.png)

![左v3，右v4](Previews/Side_Before_After.png)

[灰模对照](Previews/Side_Before_After_Clay.png) · [正视](Previews/After_Full_Front.png) · [侧视](Previews/After_Full_Side.png) · [连接侧视](Previews/After_Connection_Side.png) · [连接俯视](Previews/After_Connection_Top.png)。均为真实Blender网格渲染；局部预览隐藏机身便于观察，不代表缺件。

## 修改与检查

- 后支架作为尺寸基准，四处从支架起点至销轴中心的真实长度一致，最大长度差约0.000002米；四根向外下倾角均为30°。整机侧视中的投影长度可能因前后支架水平展开方向不同而不同。
- 两根前支架改变形状；58个前盘及节点、叉耳、销轴零件只做刚性平移；其余219个分件逐一几何一致。总计279分件、21372个评估三角形。
- 前左盘在建模坐标中移动 `(-0.415910, -0.392368, +0.330118)`，前右盘左右镜像。前后盘高度不同，每个盘保持水平，尺寸、厚度、装甲均不变。身体高度与宽度保持v3；前缘随前盘收回，整机占地长度略缩短。
- 四处盘心接触高度间隙0，左右对称；十二个轴孔与横销同轴、间隙为正。脚部与盘壳、移动后前盘与机身均未检测到表面相交。未添加骨骼或验证动态关节范围。
- 助手检查实际整机、正侧视、局部侧视/俯视、彩色和灰模对照。用户模型美术审核待反馈，不能用几何检查替代用户决定。

[几何检查](geometry-check.json) · [旧文件保护](protected-files-check.json) · [当前Blender回读](live-readback.json) · [交付清单](delivery-manifest.json)。原生产模型、v3文件和v7两份文字prompt哈希均未变。

生产逻辑位于[war_machine_reference.py](../../../Scripts/Blender/war_machine_reference.py)，`REAR_SUPPORT_LENGTH`取后支架长度，`hover_foot_offset()`求前盘偏移；[制作入口](../../../Scripts/Blender/revise_warmachine_equal_legs.py)使用 `prepare` 生成模型和检查、`render` 输出对照。`SourceSnapshot`保存修改前及本版脚本；[用户标注](References/User_EqualFrontLegs_Feedback.png)原样保留。[v3](../WarMachine_Slope30_v3/README.md)是未通过的历史候选。

继续遵循用户“先审模型再出图”：模型确认后才用新版UE整体/局部截图与v7原文字prompt重绘“增加射速”“极速机动”，原始生成图由用户审核。导弹伤害图沿用既有版本。本轮未导入UE、运行游戏、编译C++或调用生图。
