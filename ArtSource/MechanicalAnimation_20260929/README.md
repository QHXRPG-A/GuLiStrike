# Mass 无骨骼机械动画

重防号与扫荡者采用静态 ISM + 刚性部件 WPO。正式运行不创建骨骼组件或动画蓝图；共享表现代码仍批量更新每单位的姿态数据，并非完全没有 CPU 更新。

## 资源与制作入口

- `WarMachine/WarMachine_RigidEditable.blend`、`Sweeper/Sweeper_RigidEditable.blend`：正式静态制作源，无 Armature、蒙皮与动画序列。
- `Scripts/Blender/export_mass_rigid_editable.py`：日常重导出入口；只依赖上述静态源。
- `Scripts/Blender/export_mass_rigid_animation.py`：仅用于从已认可历史模型迁移；重防号 v6 与扫荡者 NoInk 的原始外观保留。
- `Scripts/import_mass_rigid_animation.py`：在编辑器导入 `/Game/Commander/Units/Tactical/Cel/{WarMachine|Sweeper}/Meshes/SM_{unit}_Rigid`，保留原材质、碰撞、FX 插槽，写入静态枢轴与轮半径插槽。
- `Scripts/build_mass_rigid_materials.py`：生成共享 `/Game/Commander/Units/MechanicalAnimation/MF_GuLiRigidMechanical`、派生的主体/描边材质，并为受击、传送、残骸接入同一变换。
- `Scripts/Materials/GuLiRigidMechanical.hlsl`：可审查的顶点变换源。
- `Config/DefaultGame.ini` 的 `GuLiMechanicalAnimation.WarMachine` / `.Sweeper`：共享动画参数。CPU 几何以模型静态插槽为源。

原 `SM_*_Cel`、`SK_*_Cel` 作为历史资产保留；正式 Soldiers 表引用 `SM_*_Rigid`。原资产有未保存修改，因此本次创建派生资产而未覆盖它。

## 数据协议

UV0 保持原贴图；全精度 UV1 存局部枢轴 XY，UV2 存 Z 与整数部件编号。导出脚本补偿 FBX 的 V 翻转。旋转轴由部件协议解码：上半身 Z、枪组 UE 正俯仰、轮组 Y、悬浮盘由行进方向得到水平轴。

重防号：0 下半身、1 上半身、2/3 左右枪组、4/5 左右双枪管、6–9 四盘。扫荡者：0 主体、2 机枪、6–9 四轮。三角形不能跨部件。

`PerInstanceCustomData` 共 23 个 float：0 为受击时间；1–11 当前姿态；12–22 前帧姿态。每帧 11 项依次为上身相对 yaw、左右 pitch、左右后坐距离、盘倾斜轴角 XY、四轮角度。角度为弧度；GPU 后坐距离为未缩放网格厘米，CPU 后坐距离为最终世界厘米（35 cm / 0.2 = 175 cm），缩放只应用一次。PreviousFrameSwitch 使用前帧数据计算速度。

枪口与导弹口在 CPU 上按同序变换（后坐→俯仰→上身 yaw→单位 root）。射击事件同时区分世界时间与 Mass 模拟时间。客户端按姿态回放时间触发后坐与闪光。指定 NS_Flash_1 已由现有 VFX 注册表 ID 5（MachineGunImpact）引用，直接复用该资源；枪口 Glow/Flash 等发射器为 local space。

该实现参考 [Epic Pivot Painter 的枢轴/顶点数据思路](https://dev.epicgames.com/documentation/unreal-engine/pivot-painter-tool-in-unreal-engine)，是程序化 WPO，不是逐帧 VAT 贴图，也不是直接使用 Epic PP2 的专有编码。

## 验证与限制

`verification.json` 是检查状态入口。`ue-import.json`、`data-import.json`、`material-diagnostics.json`、`lod-verification.json`、`scene-delivery.json` 为证据；`Previews/` 为实际 UE 静态 ISM 动作姿态图。导出的渲染 LOD0–3 均保留三组 UV，没有非整数部件或跨部件三角形。低 LOD 允许简化小枪管；不会用空网格隐藏整个单位。

`ue-import.json` 中高阶 LOD 的 source UV 数为 0，是 UE 自动减面 LOD 没有独立 Source MeshDescription；实际 RenderData 导出回读均为 3，见 `lod-verification.json`。

用户批准后，源码版UE5.7的`GuLiStrikeEditor Win64 Development`已构建成功，8份模块清单BuildId一致，证据见`native-build-report.json`。编辑器尚未启动加载，新自动化契约尚未运行。预览图不能证明战斗、网络、传送、死亡与复用已通过。现有 Mass 地图配置混编总数 500；最大相机臂长 36000 cm。最大视距实际可见数与 Game/Draw/GPU 开销尚未采样，不能填为通过或零开销。

指挥官HUD源码新增离地高度和画面中心地面距离，位于FPS/RTT下方、10Hz更新、米/一位小数。文案资产回读见`camera-text-import.json`；`MassRigid_CameraDistance`为地图内采样说明。此读数已通过C++构建，但当前没有实机HUD或性能采样结果。测试地图本身遵循既有Git忽略规则，已保存的本地布置可由`Scripts/author_mass_rigid_scene.py`重建。
