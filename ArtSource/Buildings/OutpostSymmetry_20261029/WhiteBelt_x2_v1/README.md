# 据点白色腰部灯带与双倍发光

用户在[局内原图](User_WhiteBelt_Request.png)中框出腰部横灯带，要求改成白光，并将所有发光亮度设为当时的2倍。原图SHA256为`a827d271de859cc0c0993e7e82572b34be64e7e98412e175678203b7acf921c3`。

本页保留该阶段事实；用户后续要求[光柱单独再翻倍](../CoreBoost_x2_v1/README.md)，当前光柱参数见新交付。

- **腰部横向灯带改白**：只覆盖源类别4/5在局部高度±50厘米内的两层灯带。实际腰带约±21厘米，支架与尖端的其他灯槽在3米以外，继续使用原色。
- **所有模型发光强度×2**：柱面4.4、细亮线12.0、青绿/蓝色灯槽5.6/8.0；相对[HDR首版](../GlowSpin_v1/material-delivery.json)逐项均为2.0倍。写入绝对参数，重复应用不叠加。中央光柱的白／己方蓝／敌方红与色度保护保持。
- 60秒自转、8秒浮动、4.5～5.5米净空保留。原Bloom约1.4、外壳、线稿、轮廓、4824三角面及一个材质槽保持；不增加灯光或粒子组件。
- 共享HLSL与原位更新、正式导入脚本同步。局部位置经`PreSkinnedPosition→VertexInterpolator`提供给像素材质，跟随模型浮动/旋转。初次直接使用顶点位置被像素编译器拒绝，修复插值连接后编译通过并保存；没有保存失败版本。

## 技术证据

[逐项2倍读回](change-readback.json) · [材质保存与编译](material-delivery.json) · [更新前图](material-before.json) · [最终图](material-after.json) · [更新前HLSL](surface-before.hlsl) · [最终HLSL](surface.hlsl)

材质诊断成功且错误列表为空；网格结构前后相同，49个编辑器预览引用同一正式材质。资产为`/Game/GuLiStrike/Buildings/OutpostSymmetric/Materials/M_OutpostSymmetric_Toon`。本轮只改材质与生成脚本，无需再次编译C++或重启。

## 当前局内图

[中立白 R4C4](../../../../Artifacts/OutpostWhiteBeltx2_20261029/Outpost_R4C4_InGame.png) · [己方蓝 R1C4](../../../../Artifacts/OutpostWhiteBeltx2_20261029/Outpost_R1C4_InGame.png) · [敌方红 R7C4](../../../../Artifacts/OutpostWhiteBeltx2_20261029/Outpost_R7C4_InGame.png)

沿用本会话局内截图和读图授权，正常指挥官PIE中横灯带为白，蓝/红光柱仍可区分；模型持续正常运动，未改归属或隐藏场景单位/设施。三帧净空约5.30/4.69/4.55米，逐帧姿态和CPD见`Outpost_*_capture.json`。本轮没有重复源码转速或性能验证，60秒周期在当前原生设置回读，实际6°/秒证据保留于上一阶段。

[清理及当前配置](session-cleanup.json)确认本轮相机、HUD与角色偏好恢复，PIE和辅助进程结束，编辑器留在`/Game/Maps/LVL_CommanderMassPrototype`且无脏包。未新增自动化测试，玩家最终亮度、自转与近远景观感仍待反馈。

[需求](../../../../Progress/RequirementDocument/20261028-据点巨物尺寸与轻微浮动.md) · [开发与玩家入口](../../../../Progress/DevelopmentDocumentation/20261028-据点巨物尺寸与轻微浮动.md)
