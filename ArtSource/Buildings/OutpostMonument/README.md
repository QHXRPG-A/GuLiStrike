# 据点巨构模型 v01

- 日期：2026-09-05
- 状态：Blender 第一版视觉模型；2026-09-05 已接入 UE 建造目录并替换旧据点占位资源。
- 用户方向：参考 8 张清水混凝土纪念塔图片，按“巨物”规格制作。
- 本版尺寸假设：高 300m；实际外包尺寸约 71.031 × 66.198 × 300m。300m 是本次制作初值，并非用户指定的最终数值。

## 文件

| 路径 | 用途 |
|---|---|
| [OutpostMonument_v01.blend](OutpostMonument_v01.blend) | 可编辑工程，贴图已打包；默认打开巨构展示场景 |
| [Exports/SM_OutpostMonument_v01.fbx](Exports/SM_OutpostMonument_v01.fbx) | 单个静态网格，材质纹理从相邻 Textures 目录引用 |
| [Exports/SM_OutpostMonument_v01.glb](Exports/SM_OutpostMonument_v01.glb) | 单网格便携预览，嵌入 PBR 贴图 |
| [asset_manifest.json](asset_manifest.json) | 尺寸、拓扑、文件与导入检查结果 |
| `Textures/T_OutpostConcrete_BaseColor.png` | 2048×2048，4m 平铺混凝土底色 |
| `Textures/T_OutpostConcrete_Roughness.png` | 2048×2048，粗糙度数据 |
| `Textures/T_OutpostConcrete_NormalGL.png` | 2048×2048，Blender/glTF 用 OpenGL 切线法线 |
| `Textures/T_OutpostConcrete_NormalDX.png` | 2048×2048，绿通道翻转的 DirectX 版本，供 UE 材质接入 |
| `References/Reference_01.png`～`Reference_08.png` | 用户本次提供的原始参考，按消息顺序保留 |

## Blender 场景组织

- `Scene`：原有默认场景保留。
- `GS_OutpostMonument_v01`：展示与编辑场景。
  - `GS_01_Editable_Architecture`：21 个独立构件，保留倒角与法线修改器。
  - `GS_90_Presentation_Only`：地面和 1.8m 比例人形，不进入模型导出。
  - `GS_91_Cameras_Lighting`：整体、反面、地面仰视、局部和轴测相机，以及展示灯光。
- `GS_Outpost_Export_v01`：合并、倒角求值、三角化后的导出网格；修改造型应回到源构件。

## 模型规格与检查

- 源构件 21 个，源顶点 3,212。
- 导出网格 13,552 顶点、27,020 三角面、1 个混凝土材质槽。
- 所有构件由闭合截面拉伸；导出网格的非流形边与零面积面均为 0。
- 构件彼此允许穿插，保留模块独立性；本版不是布尔合并的单一实体，也未制作破坏结构。
- 原点为地面高度 `(0,0,0)`，Blender 米制，导出对象 Scale 为 `(1,1,1)`。
- `UV0_Concrete4m` 沿竖向面展开，按 4m 平铺贴图；`UV1_Lightmap` 为独立打包的次级 UV。尚未在 UE 烘焙光照。
- FBX 重新导入 Blender：1 个网格，尺寸和地面原点一致，2 个 UV 通道均保留。
- GLB 文件检查：1 个场景、1 个网格节点、3 张嵌入贴图；无地面、人物、默认方块、灯光或相机。

## 预览

- [整体](../../../outputs/outpost-monument-20260905/01_hero.png)
- [背面](../../../outputs/outpost-monument-20260905/02_reverse.png)
- [地面仰视](../../../outputs/outpost-monument-20260905/03_ground.png)
- [腰带与弧面近景](../../../outputs/outpost-monument-20260905/04_detail.png)

预览在项目 `outputs/` 下，属于本地生成附件；可以从工程相机重新渲染。

## 再生成

在新的 Blender 场景环境中依次运行：

1. `Scripts/Blender/build_outpost_monument.py`：生成贴图、构件和初始展示场景。检测到同名模型场景时会停止，避免覆盖正在调整的模型。
2. `Scripts/Blender/finish_outpost_monument.py`：设置展示灯光、渲染四个视角、创建导出场景并导出 FBX/GLB、保存打包贴图的工程。

脚本里的 `HEIGHT_M=300.0` 控制初始几何比例；展示相机和灯光使用当前 300m 尺度的数值，改尺度时需一并调整。完成工程已有导出场景，再导出时应选中该场景网格，或在确认旧导出副本可替换后重新建立它。

## UE 接入状态

游戏继续引用 `/Game/GuLiStrike/Buildings/Meshes/SM_OutpostPlaceholder`，该资源内部已经替换为巨构几何和 `M_OutpostConcrete`。保留旧名称是为了兼容既有引用；原 20m 方块另存到 `/Game/GuLiStrike/Buildings/Legacy/SM_OutpostCube20m_PreMonument`。

独立 FBX 导入源为 `/Game/GuLiStrike/Buildings/Meshes/Source/SM_OutpostMonument_v01`。正式接入脚本是 `Scripts/import_outpost_monument.py`，应通过普通源码版 Editor 的 `-ExecutePythonScript` 执行，不能从 MCP 任务图回调中直接同步导入。`Scripts/build_building_assets.py` 已接入这个入口，不会再生成占位方块。

- 重新打开 UE 后实测 `7103.0864258 × 6619.8051758 × 30000cm`，27,020 三角面、24,890 渲染顶点、1 Section / 材质槽、2 UV，BuildScale 1。
- 底色使用 sRGB；粗糙度为线性灰度数据，`NormalDX` 使用 Normalmap 压缩且不再翻绿通道。UV0 保留 4m 平铺，UV1 为光照 UV；未进行烘焙光照验收。
- 目录 CollisionExtent 更新为 `(3551.5432129,3309.9025879,15000)`cm，VisualOffset 为 `(10.5893555,-252.5859375,0)`cm。编辑器按这些值在平地展示，底部与地面误差为 0cm。
- 现有整块 Box 阻挡范围扩大到巨构体积；可见深缝不自动等于可通行空间。
- 旧资产验收仍有 20m 方块断言，本次未修改或扩充这些测试；B→3 操作、多人放置和普通角色阻挡未重新进行完整 PIE 验收。
- [导入与替换归档](../../../Progress/Archive/20260905-据点巨构替换占位资源.md)
