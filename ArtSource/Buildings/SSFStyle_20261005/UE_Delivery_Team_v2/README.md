# SSF 蓝红建筑正式 UE 资源 · B_v2

已按用户“导入至ue作为正式资源”放行当前实际 `SSF_TeamPalette_B_v2`，保存并独立重载 **48个新增资源**：12个骨骼建筑网格（各三档LOD）、24个材质实例、12张2K基础色图集。

| 阵营 | 正式目录 | 配色 |
|---|---|---|
| 蓝方 | `/Game/GuLiStrike/Buildings/SSFStylized/Blue` | 图1橙 / 蓝灰 / 奶油白 |
| 红方 | `/Game/GuLiStrike/Buildings/SSFStylized/Red` | 图2莓红 / 浅粉 / 橙 |

每队含 AirBase、CloningCenter、CommandCenter、MilitaryFactory、Reactor、StrategyCenter，分别有 `Meshes` / `Materials` / `Textures` 子目录，网格名 `SK_Blue_<Building>` / `SK_Red_<Building>`。

**原正式B_v1与商城包保留，暂不接入游戏。** 两队复用此前正式目录中已验证的6套兼容骨架、6个物理资产、21个建筑动画，以及共用三档/描边/显示母材质和各建筑线稿遮罩。无人机、平台、灯具与无人机第22个动画保持。因此使用两队资源时继续保留原 `SSFStylized` 的共用依赖目录；两队不再依赖原商城包。

## 实际 UE 外观与使用

[蓝红两队实际UE总览](Sheets/Blue_Red_UE_Overview.png) · [蓝方](Sheets/Blue_UE_Overview.png) · [红方](Sheets/Red_UE_Overview.png)

三档明暗、独立内部线稿、近中档真实描边壳、远档细线淡出、半透明标识和 `Base Color` / `Team Color` 接口保持。实际基础配色通过4套UV中的线性RGB传输到UE三档材质，2K基础色图集也随正式资源入库；这不是自动PBR导入。

屏幕尺寸为 `1.0 / 0.10 / 0.035`。全部12模型有原生2048px的三个实际LOD镜头，以及35m/25°、300–700m/55°和1500m/55°自动LOD资源预览，共84张。预览在未保存的临时Entry世界完成，不代表实战地图或实战帧率。

## 核对结果

- 独立进程重载48个资源，引用均落在正式 `SSFStylized` 组内及Engine/Script/ACL等允许依赖，原商城资源引用为0。
- 36档存储网格由UE导出回读，几何最大差 `4.86280396e-06 m`、权重最大差 `6.19888306e-06`；三档面数、4UV中的配色/淡出/队色数据和法线保持。
- 21原正式动画 × 2队 × 3LOD × 起/中/末，共378组实际组件对照；相对已保存B_v1实际组件，最大位置 / 缩放 / 旋转差为 `[0.0, 0.0, 0.0]`，单位cm / 无量纲 / °。动画本身未修改。
- 来源Blender SHA256 `5cee0988de1a4201cd9eb60de9cbd30ccddd108090b99b3e86f50c4bf2d584b6`，147个冻结审核文件仍保持，用户具体版本放行见[决定](../approval_B_v2_import_20261007.json)。

原[24项本体差额（含平台/无人机）](../Production_B_v1/Budget_Exceptions.md)及预算内描边壳的局部覆盖差异继续披露。两队建筑分别继承6建筑×3档的本体差额，本轮未重新减面、增加预算或开展玩法/联机/性能测量。

[正式机器清单](formal_delivery.json) · [保存重载/姿态/截图](ue_validation.json) · [存储网格回读](ue_mesh_readback.json) · [助手读图](visual_qa.json) · [原生图片哈希](preview_inventory.json) · [冻结源保持](frozen_source_validation.json)
