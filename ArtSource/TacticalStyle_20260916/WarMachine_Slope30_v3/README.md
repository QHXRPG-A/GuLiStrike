# 重防号支架外下倾 30° · v3 审核候选

此为保留的历史候选。用户继续要求前腿与后腿等长，最新见[v4](../WarMachine_EqualLegs_v4/README.md)，该反馈不记为v3通过。

按用户“太平了，点倾斜，30°”及后续明确的“支架向外下倾、悬浮盘保持水平”修改。四根支架真实三维轴线均向外下倾 30°；四个盘体及腿末盘心承接组件保持水平和原位。当前先审核模型，通过后才重绘两张卡。

## 文件与查看

- [Blender 审核版](WarMachine_Slope30_Review.blend)：已在当前 Blender 打开，默认完整模型侧视相机。中键旋转检查，数字键盘 0 返回相机；Scene 下拉 `Side_Before_After` 查看整机侧视对照，`Connection_Before_After` 查看连接对照。
- [可编辑分件源](WarMachine_Slope30_Editable.blend)：279 个命名分件及原修改器。
- [完整三分之四视图](Previews/After_Hero.png) · [正视](Previews/After_Full_Front.png) · [侧视](Previews/After_Full_Side.png)。
- [连接斜视](Previews/After_Connection_Oblique.png) · [沿支架平面的侧视](Previews/After_Connection_Side.png) · [俯视](Previews/After_Connection_Top.png)。局部图隐藏机身便于检查，不代表整机缺件。

![左 v2，右 v3](Previews/Side_Before_After.png)

![灰模侧视对照](Previews/Side_Before_After_Clay.png)

![完整新模型](Previews/After_Hero.png)

## 几何与装配

- 以原水平面为基准计算真实支架轴线角度，四处实测均为 30°。整机侧视中的投影角度会随支架在水平面内的展开方向变化；连接侧视相机垂直于支架所在竖直面，可直接看下倾角。
- 56 个悬浮盘分件的网格、位置、直径、厚度与原样一致；60 个盘心承接、脚节点、叉耳、销轴分件保持原样。四个盘仍在原共同水平面。
- 为形成向下支撑姿态，机身、武器及挂点整体上移 1.198634 建模单位；前后身体侧关节分别上移 1.363693 / 1.033575 单位，补偿前后支架水平跨度不同。整机宽度与长度不变，高度约增加 22.1%。这是新站姿变化，不是缩放武器或盘体。
- 四根支架重新构造，窄端继续夹入叉耳，以实际轴眼和横销连接；机身侧关节随高度调整，保留原几何形状。
- 279 分件、21372 个评估三角形，左右对称误差约 0.000004 米；四处承接接触高度间隙为 0，支架与盘壳无检测到的表面相交，12 处轴孔同轴且径向间隙为正。静态检查未覆盖关节活动范围；本版没有骨骼动画。

[几何记录](geometry-check.json) · [旧文件与文字 prompt 保护检查](protected-files-check.json) · [Blender 保存回读](live-readback.json) · [交付清单](delivery-manifest.json)。旧 v1/v2 模型与正式资产保留。

助手已检查实际模型的整机、侧视/正视、连接侧视/俯视及灰模对照；修正了最初俯视预览裁到盘边的问题。上述均为 Blender 网格直接渲染，不是生图。用户模型美术审核仍待反馈，没有将数据检查记为用户通过。

## 复现与后续

[实际生产脚本](../../../Scripts/Blender/war_machine_reference.py)中的 `SUSPENSION_DOWN_DEGREES` 控制下倾角；固定原盘心位置求解支架起点与身体侧关节高度。[本轮制作脚本](../../../Scripts/Blender/revise_warmachine_slope30.py)的 `prepare` 生成分件及检查，`render` 生成对照图，`reframe` 只调整俯视预览取景。[通用渲染](../../../Scripts/Blender/revise_warmachine_hub_seat.py)保留原 v1/v2 默认参数。`SourceSnapshot` 保存本轮修改前脚本；[用户标注](References/User_30Degree_Feedback.png)原样保留。

按用户已定顺序停在模型审核：模型确认后，再获取新版完整/双联炮/悬浮盘 UE 截图，使用 v7 `FireRate.txt`、`HighSpeed.txt` 原文字生成“增加射速”和“极速机动”。文字哈希本轮未变；后续生成结果直接交给用户，助手不读生成图。导弹伤害图沿用现有版本。

本阶段没有导出 FBX、导入 UE、运行游戏、编译 C++ 或生成新卡牌。
