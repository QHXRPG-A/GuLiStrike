# 战争机器悬浮盘连接审核版 v1

此为保留的中间版本。用户追加了支架与节点连接的红框反馈，叉耳版本见 [v2](../WarMachine_LegClevis_v2/README.md)，最新外下倾30°候选见 [v3](../WarMachine_Slope30_v3/README.md)。该反馈不记为 v1 美术通过。

按用户确认的“圆环上方承接”修改四处腿末连接；先审核模型，确认后才重绘“增加射速”和“极速机动”。本目录是 Blender 审核候选，尚未导入 UE，也未调用生图。

## 审核入口

- [Blender 成品与对照场景](WarMachine_HubSeat_Review.blend)：默认展示完整新模型；顶部 Scene 下拉选择 `Connection_Before_After` 可看同视角三维前后对照。
- [可编辑分件源](WarMachine_HubSeat_Editable.blend)：247 个命名分件，保留倒角、法线和独立纯色材质。
- 下方所有图均为真实 Blender 网格渲染，没有经过生图或图片修饰。局部特写隐藏其他部件以便看清连接，完整造型见整机图。

## 前后对照

![左侧修改前，右侧修改后](Previews/Connection_Before_After.png)

## 新模型

![整机三分之四视图](Previews/After_Hero.png)

| 连接特写 | 修改前 | 修改后 |
|---|---|---|
| 三分之四 | [旧连接](Previews/Before_Connection_Oblique.png) | [新连接](Previews/After_Connection_Oblique.png) |
| 侧视 | [旧连接](Previews/Before_Connection_Side.png) | [新连接](Previews/After_Connection_Side.png) |
| 俯视 | [旧连接](Previews/Before_Connection_Top.png) | [新连接](Previews/After_Connection_Top.png) |

![连接侧视](Previews/After_Connection_Side.png)

![连接俯视](Previews/After_Connection_Top.png)

## 实际改动

- 四处节点平移到原盘心上方，装甲、白色套环和黄色指示灯整体跟随；重新连接最后一段支架，机身侧关节不动。
- 在每个腿末底部增加一只浅圆形承接底座，使其底面贴合原中央圆帽的上表面；原中央圆环、圆帽与盘壳没有改动。底座作为腿末的组成部分，沿用蓝灰色与规则倒角。
- 原 243 个分件中的 28 个连接相关分件被调整，新增 4 个底座，总计 247 个分件。其余 215 个分件的网格、面材质编号、变换和修改器参数保持一致，其中包含全部 56 个悬浮盘分件。
- 原模型、旧成品、原始 v7 prompt 均保留。实际生产脚本更新为同一连接构造，旧脚本保存在 `SourceSnapshot/war_machine_reference_before.py`。

## 几何与版本核对

[几何报告](geometry-check.json)记录四处底座与盘心的接触高度、中心误差、盘壳相交检查、对称误差与外形尺寸；[受保护文件核对](protected-files-check.json)记录旧源与 prompt 的 SHA-256。

- 四处垂直间隙均为 0；最大 XY 中心误差约 0.000006 米（浮点误差）。
- 修改的支架和腿末组件与悬浮盘外表面相交计数为 0；底座与圆帽之间为预期接触面。
- 整机包围盒保持一致，最大坐标差为 0；左右对称误差约 0.000004 米。
- 武器分件未变化，原炮口/导弹挂点数值保留；没有改变骨骼与玩法。
- 审核版使用既有三档明暗、结构线遮罩和轮廓壳制作流程，重新计算新结构的线稿。轮廓壳为显示用副本，不进入上述基础网格接触检测。

上述仅证明本轮几何与版本核对结果，不能替代用户对造型的审核。助手模型视觉检查另见最终交付记录，用户审核尚待反馈。

## 审核后两张卡的处理

模型审核通过后，制作独立 UE 截图预览副本，重新取得整体、双联炮和悬浮盘三张参考，沿用 v7 相机、曝光及材质。每次输入新的整体图和对应局部图。正式战斗网格、既有卡牌材质与抽卡流程本阶段不替换。

直接读取原 `FireRate.txt` 与 `HighSpeed.txt`，文字逐字不变；prompt 路径及哈希见保护文件报告。内置 image_gen 的原始结果直接展示给用户，助手不读取评价或自动修订。增加导弹伤害沿用当前 v7 图片，此选择不自动记录为美术终验通过。

## 可复现入口

- [连接构造](../../../Scripts/Blender/war_machine_reference.py)
- [修订与实际网格预览脚本](../../../Scripts/Blender/revise_warmachine_hub_seat.py)
- [原始分件源](../Production_Handbuilt/WarMachine_Handbuilt_Editable.blend)
- [原始模型报告](../Production_Handbuilt/WarMachine_handbuilt_report.json)

脚本的 `prepare` 阶段从原分件源读取，仅替换目标连接并保存新路径；`render` 阶段制作前后两套显示副本、同机位渲染和三维对照场景，不导出 FBX 或操作 UE。
