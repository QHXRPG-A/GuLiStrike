# 据点HDR发光与60秒自转

用户明确要求增强光柱、亮线与灯槽HDR自发光、保留白／阵营色并避免曝白，利用现有Bloom；随后指定自转“60秒一圈”。沿用已批准B_v1几何及本会话编译/重启、局内截图与读图授权。

本页保留HDR首版的参数和证据。用户随后要求[腰部灯带改白、全部发光强度×2](../WhiteBelt_x2_v1/README.md)，当前材质与局内图见该增量。

- 全状态绕竖直轴**60秒一圈（6°/秒）**，与原**8秒浮动**共用同步时钟和生命周期相位。保持底部**4.5～5.5米**净空；归属改变不重置相位，模型绕居中位置转动，原地面锚点、碰撞、占领和运输规则保持。
- 原位更新`M_OutpostSymmetric_Toon`的发光分支；柱面HDR峰值2.2、细亮线6.0、青绿/蓝色灯槽2.8/4.0。发光色归一后使用1.35色度曲线保留饱和度，白／己方蓝／敌方红仍由原CPD接口切换；外壳分档、线稿和轮廓保留。
- 现有启用PostProcessVolume的Bloom约1.4保持；无需新增Niagara、灯光组件或模型。网格仍为**4824三角面、一个材质槽**，Blender源与冻结批准版本不变。
- `Scripts/Models/outpost_surface_style.py`为共享HLSL来源，正式导入与原位材质更新共用，避免重新导入恢复旧亮度。60秒参数及验证输出由原生表现设置统一提供，编辑器场景说明同步读取。

## 核对

- [源码Editor构建](native-build.json)：`GuLiStrikeEditor Win64 Development`成功，30.98秒、退出码0、BuildId匹配，新代码已加载。
- [材质保存及诊断](material-delivery.json)：编译成功、无错误；[最终HLSL](surface.hlsl)、[更新前HLSL](surface-before.hlsl)、[导出图](material-after.json)。几何、材质槽、网格引用前后不变，49预览引用同一正式材质。
- [场景交付](scene-delivery.json)：`/Game/Maps/LVL_CommanderMassPrototype`的49预览保存，读取原生自转周期60秒，尺寸与净空误差保持原精度。预览为静态均值姿态且仅编辑器显示，实际动画在正常局内运行。
- [实际运动读回](motion-readback.json)：49据点两次样本间隔14.4055秒，全部**6.0°/秒、60秒一圈**；Actor位置/旋转与地面碰撞固定。[第一帧数据](motion-first.json)、[第二帧数据](motion-second.json)。采样净空4.90～5.44米。
- [会话清理](session-cleanup.json)：相机、HUD和角色偏好恢复，本轮PIE与逻辑/协调进程正常退出；对应Map留在编辑器，保存包无脏项。

## 本轮实际局内图

[中立白 R4C4](../../../../Artifacts/OutpostGlowSpin20261029/Outpost_R4C4_InGame.png) · [己方蓝 R1C4](../../../../Artifacts/OutpostGlowSpin20261029/Outpost_R1C4_InGame.png) · [敌方红 R7C4](../../../../Artifacts/OutpostGlowSpin20261029/Outpost_R7C4_InGame.png)

截图来自本轮正常指挥官PIE的真实据点，三色可区分，细亮线与灯槽形成柔和亮边。相机选择朝向核心开口的方位，拍摄间模型持续浮动与旋转；只临时调整相机及收起HUD，没有隐藏单位或设施、没有修改归属。截图净空约5.46/5.39/4.59米，逐帧参数见对应`Outpost_*_capture.json`。画面背景仍是实际场景。

未新增自动化测试或进行性能验收。玩家从对应Map正常以指挥官进入，验收三色、发光强度、自转节奏和近远景；实际换主、点选运输及最终美术观感待反馈。助手数据和截图检查不算玩家通过。

[需求与玩家入口](../../../../Progress/RequirementDocument/20261028-据点巨物尺寸与轻微浮动.md) · [开发记录](../../../../Progress/DevelopmentDocumentation/20261028-据点巨物尺寸与轻微浮动.md) · [初次模型接入](../UE_Delivery_v1/README.md)
