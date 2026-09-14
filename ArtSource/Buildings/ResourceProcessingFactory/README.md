# 资源加工厂 v01

2026-09-09：Blender 模型、PBR 材质、开关门动画及 UE 正式资源已完成。正式蓝图、随骨骼运动的门碰撞、演示地图、反向播放和通行已现场验收。详见 [完整交付说明](DELIVERY.md)。

## 打开与预览

- 工程：[ResourceProcessingFactory_v01.blend](ResourceProcessingFactory_v01.blend)，默认场景 `GS_RPF_Authoring`，贴图已打包。
- 按空格播放／暂停 `Door_Preview_Cycle`；时间轴 1–271 帧、30 fps，包含关闭、开启、停留和关闭。
- 独立动作 `Door_Open`、`Door_Close` 各为 1–91 帧、3 秒；导出 FBX 仅包含对应独立动作。
- 源构件分为 `GS_RPF_01_EditableBody`（806 件）和 `GS_RPF_02_EditableDoor`（170 件）。隐藏的 Export 集合用于 UE 导出，Studio 集合仅用于展示。

## 资产规格

- 宽 60m × 深 48m × 高 22.004m；最后 4mm 为屋顶标识。原点在占地中心地面，Blender 正面 +X。
- 主体 99,600 三角面，门 10,612 三角面；各 3 个材质槽，合计 6 个。
- 4K 装甲贴图、2K 内舱贴图：BaseColor、NormalGL／NormalDX、ORM、EmissiveMask。装甲顶点色 Alpha 控制迷彩覆盖；Details 顶点色 RGB 和 Alpha 分别控制颜色与发光强度。
- 骨架 `RPF_Rig`：`root`、`door_hinge`；门刚性权重，无软变形。门轴 Blender 坐标 `(11.5, 0, 15.15)m`，向外上翻 90°。
- 六张用户参考保存在 `References/`；静态主体、骨骼门和两段动画 FBX 保存在 `Exports/`。

## UE 当前事实

已由独立源码版 UE 5.7.4 Editor 导入主体、门、两段 3 秒动画及材质。回读主体尺寸 `4800 × 6000 × 2200.4cm`，9 个分段凸碰撞，门 3 个 LOD。该进程已退出。

资源根为 `/Game/GuLiStrike/Buildings/ResourceProcessingFactory/`。正式蓝图 `Blueprints/BP_ResourceProcessingFactory` 提供 OpenDoor、CloseDoor、GetDoorAlpha、DoorDuration、两个完成事件和 EntryPoint / UnloadPoint。`Demo/LVL_RPF_Showcase` 展示关闭、循环及开启保持实例。蓝图编译和最终地图检查均为 0 错误 / 0 警告；门物理资产及材质使用标记已保存并在重新加载后确认。

## 再生成与证据

- [Blender 制作脚本](../../../Scripts/Blender/build_resource_processing_factory.py)：应在独立 Blender 进程运行，避免覆盖当前编辑。实际脚本位于项目 `Scripts/Blender/`。
- UE 导入脚本：项目 `Scripts/import_resource_processing_factory.py`，须由普通源码版 Editor `-ExecutePythonScript` 执行。
- 蓝图脚本：项目 `Scripts/build_resource_processing_factory_blueprints.py`；完整入口为 `Scripts/build_resource_processing_factory_ue.ps1`。制作和导入脚本副本保存在本目录 `Scripts/`。
- 预览：项目 `outputs/resource-processing-factory-20260909/`，包含关闭、开启、正面、顶部、背面、内舱图、动画短片及导入回读报告。
- 详细规格：[asset_manifest.json](asset_manifest.json)。
