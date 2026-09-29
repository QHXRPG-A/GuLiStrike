# 重防号前端连接下倾、四盘同高 · v5

保留的历史候选。用户继续要求两个方块水平，最新见[v6](../WarMachine_LevelNodes_v6/README.md)，该反馈不记为v5通过。

用户要求将红框内两个前端连接件向下倾斜，让四个悬浮盘在同一水平；同时明确“只管制作，不需要查看过程图片，制作完之后我来核验”。本轮未读取过程或成品图片，不做助手视觉通过判断。

[Blender审核版](WarMachine_LevelFeet_Review.blend)为当前成品，[可编辑源](WarMachine_LevelFeet_Editable.blend)保留279个命名零件及修改器。审核版默认正视相机，中键旋转，数字键盘0返回相机。成品图片直接供用户查看：[整机](Previews/After_Hero.png) · [正视](Previews/After_Full_Front.png) · [侧视](Previews/After_Full_Side.png) · [连接斜视](Previews/After_Connection_Oblique.png)。

## 实施

- 保留v4四根等长主支架及其30°下倾角，机身、武器、机身侧关节、销轴中心和后脚不变。
- 前方叉耳和脚节点绕现有横销向下倾约17.07°。装甲、白色套环与黄色灯跟随，横销仍与轴孔同轴。
- 前盘保持自身水平，降至后盘所在平面；前盘横向位置只随下倾后的节点少量收回，直径、厚度和装甲形状保留。
- 盘心承接座改为底面水平、上表面随节点倾斜的实心过渡座，承接下倾的节点。角度受装甲下缘与盘心的数值间隙约束，避免直接旋转后穿入盘心。

## 数值核对与边界

[几何记录](geometry-check.json)确认主支架起点、终点未变，四根长度差约0.000002米；四盘高度差约0.0000005米，盘心承接间隙在浮点误差范围；十二孔同轴且径向间隙为正，腿部与盘壳未检测到表面相交。重建部件封闭、朝外，左右对称。279分件、21292评估三角形。

这是数值检查，不代表画面或用户美术验收通过。助手没有打开图片、截图或做图像分析；渲染文件仅作为最终交付提供给用户。没有新增骨骼或验证关节活动范围。

[当前Blender保存回读](live-readback.json) · [旧文件保护](protected-files-check.json) · [交付清单](delivery-manifest.json)。[用户红框](References/User_FrontConnectorTilt_Feedback.png)原样保存；v4及更早模型保留。[生产代码](../../../Scripts/Blender/war_machine_reference.py)、[本轮版本入口](../../../Scripts/Blender/revise_warmachine_level_feet.py)和脚本快照可复现模型，`prepare`生成模型及数值检查，`render`仅输出最终预览。

模型由用户核验。通过后才继续以新模型截图和v7原文字prompt重绘射速、机动两张卡；生成图同样直接交用户审核。导弹伤害沿用已有图。本轮没有UE导入、游戏运行、C++编译或卡牌生图。
