# 扫荡者去线稿、地面爆炸缩放与关卡亮度调整

本目录记录 2026-09-17 用户最新三项修改，覆盖此前 StylePass 的扫荡者线稿状态。

## 当前 UE 结果

- 扫荡者仍使用 `/Game/Commander/Units/Tactical/Cel/Sweeper/Meshes/SM_Sweeper_Cel`，静态与骨骼模型均重导入。删除外轮廓几何，主体材质不再读取内部 LineMask；保留弧形侧甲、实际接缝、轮胎结构、配色及三档明暗。静态网格现在只有一个材质槽、一个绘制 section。
- 战争机器继续使用 `/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Cel`，内部线稿和外轮廓保留；本轮没有重导入或保存战争机器资产。
- `/Game/GuLiStrike/FX/CombatExplosions/NS_GroundDestruction_03` 的所有火球、烟团、放射细节和地面薄环，渲染尺寸/位移统一乘 **0.6**。原模拟轨迹、粒子数量、寿命和消散节奏不改。僚机摧毁与轰炸资产没有修改。
- `/Game/Maps/LVL_CommanderMassPrototype` 保存了明暗修正。取消全局 Gamma 0.54、Contrast 1.39 和 SceneColorTint 0.614583 的压暗；恢复中性色温，调整 Film Toe/Slope、暗角与 AO，天空光下半球改为少量蓝灰填充。太阳强度、固定曝光上下限与曝光补偿仍为原值，没有修改其他关卡和全局渲染配置。

环境后处理也会改善僚机爆炸的显示亮度，但其资源、范围和播放节奏保持原配置。

## 面数与源文件

| 扫荡者 | LOD0 | LOD1 | LOD2 | LOD3 |
|---|---:|---:|---:|---:|
| 去线稿后 UE | 5,456 | 1,363 | 901 | 220 |
| 之前含外轮廓 | 10,912 | 2,727 | 900 | 220 |

Blender 主体 5,482 三角面，导入器剔除退化面后 UE 为 5,456。主体几何未重建，四轮和机枪骨骼、UV 和枪口保持。制作文件为 `Models/Sweeper_NoInk.blend`，静态/骨骼导出为 `Models/SM_Sweeper_NoInk.fbx` 与 `SK_Sweeper_NoInk.fbx`。用户原先未保存的 Blender 交互文件没有覆盖。

## 视觉证据

- `Before_models.png` / `After_models.png`：同关卡、同相机的两台单位对照，扫荡者在两组中均为新无描边模型。
- `Before_peak.png` / `After_peak.png`：原地面爆炸与新版 0.6 爆炸，同时展示环境后处理变化。
- `*_expansion.png`、`*_smoke.png`、`*_tail.png`：0.55、1.5、4 秒阶段。
- `*_Viewport_*.png`：实际编辑器视口截图；截图任务异步，因此文件名对应请求阶段，不能用其精确测量特效时间。

阶段图由 UE SceneCapture 最终颜色输出到 sRGB 目标；没有外部调色。预览模型与特效临时放在真实关卡地面，结束后全部删除，未保存这些临时演员。源发射器保留随机性，因此这些图用于视觉检查，不是像素精确尺寸或性能对照。

## 实现与验证

- `sweeper_import.json`、`sweeper_final.json`：重导入、材质编译、LOD、挂点以及单材质槽读回；`Models/sweeper_no_ink.json` 确认主体几何和对称性保持、外轮廓三角面为 0。
- `ground_scale.json`：十个发射器的渲染位置/尺寸绑定均改为独立 Presentation 属性，`User.PresentationScale=0.6`；Niagara 编译成功，零错误、零警告。组件缩放仍为 1，`User.Area_Scale` 及地面/空中共用的 ReferenceExplosionScale 没有调整。
- `level_lighting_adjustment.json`、`level_saved.json`：关卡值读回、地图保存成功且不再脏；开工前已有的三个无关 Toon 材质仍保持未保存状态。保存桥接请求曾超过 30 秒，但后续完成日志和结果文件确认保存成功，没有重复保存。
- `saved_readback.json`：源码 UE5.7 独立进程从磁盘重新加载关卡、模型、材质和 Niagara，十二项检查全部通过；扫荡者只有 BaseColor 贴图、战争机器仍有 LineMask/Contour、地面十个发射器均使用缩放后的属性，僚机未增加缩放参数。该进程未保存资产，已退出。
- `syntax_check.json`：本轮制作与导入 Python 脚本语法通过。没有修改 C++ 或自动化测试文件，未重复原生构建。本轮不宣称完成此前机械动作、真实僚机攻击或性能采样计划。

## 重做与回退

- 重导入脚本 `Scripts/import_cel_model_assets.py` 已把 Sweeper 定为 NoInk，WarMachine 保持 Cel+Ink。`Scripts/import_sweeper_no_ink_live.py` 仅处理扫荡者，并在 Slate tick 执行以避开桥接递归等待。
- 扫荡者旧版位于 `/Game/Commander/Units/Tactical/Cel/Sweeper/Rollback`，包括 `SM_Sweeper_Cel_WithInk`、`SK_Sweeper_Cel_WithInk`、`M_Sweeper_Cel_WithInk`；回退网格的主体已引用回退材质，不受新材质去线稿影响。
- 地面旧尺寸副本为 `/Game/GuLiStrike/FX/CombatExplosions/Rollback/NS_GroundDestruction_03_Before060`。也可把新系统 `User.PresentationScale` 改回 1 恢复渲染尺寸。`Scripts/scale_ground_destruction_visual.py` 重复执行不会再乘一次 0.6；原 Revision 2 部署保留当前项目副本。
- `level_lighting_rollback.json` 保留调整前的完整 PostProcessSettings 和天空光下半球颜色；需回退时只恢复其中指定关卡的两个对象并保存该关卡。调整脚本为 `Scripts/adjust_commander_level_readability.py`，它只应用属性，保存作为独立步骤。
