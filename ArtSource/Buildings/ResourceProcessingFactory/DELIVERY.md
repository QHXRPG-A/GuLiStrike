# 资源加工厂 v01 正式交付

资源已保存到 `/Game/GuLiStrike/Buildings/ResourceProcessingFactory/`。在 UE 放置 `Blueprints/BP_ResourceProcessingFactory`，使用 Scale 1、正面 +X。演示地图为 `Demo/LVL_RPF_Showcase`，点击 Play 展示关闭、循环开合和打开保持三个实例。

## 接口

| 接口 | 用法 |
| --- | --- |
| `OpenDoor()` / `CloseDoor()` | 默认各 3 秒，有缓动；可中途反向，同方向重复调用不重置。 |
| `DoorDuration` | 实例可编辑的完整行程秒数，最小按 0.05 秒处理。 |
| `GetDoorAlpha()` | 纯函数，返回 0–1 的只读进度。内部 DoorAlpha 为 private，外部不能写入。 |
| `OnDoorOpened` / `OnDoorClosed` | 每次抵达对应终点广播一次。 |
| `EntryPoint` / `UnloadPoint` | 可读取世界变换的场景组件；局部位置为 `(3000,0,120)`、`(-700,0,120)` cm。 |

Timeline 采样 Blender 制作的开门动画，关闭时反向采样以保持中途换向连续。独立开门、关门动画序列也均已交付。正式蓝图默认关闭，演示循环位于独立控制器；车辆逻辑、加工经济及建造菜单留待后续接入。

## 验收事实

- 主体 99,600 三角面、门 10,612 三角面，各 3 个材质槽；主体 Nanite，门 3 级 LOD。
- 宽 60m × 深 48m × 高 22.004m，额外 4mm 为屋顶标识。门洞名义约 36m × 14m，实际开门净空受门板厚度影响。
- 主体 9 个分段凸碰撞；门使用 `SK_RPF_Door_PhysicsAsset`，薄盒刚性跟随 `door_hinge`，无需物理模拟。
- 半开约为 Alpha 0.5 / 采样 1.5 秒。反向及重复调用前后采样位置相同，实际抵达终点的次数与完成事件次数一致。
- 全开后角色胶囊和 10×7×5m 车辆尺寸代理可进入内舱；关闭时命中门。地板、侧墙、后墙正常阻挡。DoorDuration 1.5 秒也已检查并恢复默认 3 秒。
- 重新启动源码版 UE 5.7.4 后，三个实例 Scale 1、默认关闭。两个蓝图编译、最终地图检查均为 0 错误 / 0 警告。Nanite / SkeletalMesh 材质使用标记保存后从磁盘重新加载确认。
- Blender 工程已重新打开检查，贴图已打包；保留 806 个主体构件、170 个门构件和两段独立动作。UE 根骨骼有导入单位换算节点，组合骨骼缩放 100，Actor / 组件缩放为 1。

## 预览与复现

- [Blender / UE 多视角对照](COMPARISON.md)。
- [UE 开关门短片](../../../outputs/resource-processing-factory-20260909/RPF_UE_Door_Preview.mp4)：真实 UE 渲染，1280×720、10 fps、9 秒，开关各 3 秒。
- [Blender 开关门短片](../../../outputs/resource-processing-factory-20260909/RPF_Door_Preview.mp4)。
- 详细记录：项目 `outputs/resource-processing-factory-20260909/ue_runtime_observation.json`、`ue_reload_report.json`、`ue_asset_readback.json`、`ue_capture_report.json`。
- 制作 / 导出：`Scripts/Blender/build_resource_processing_factory.py`。在独立 Blender 进程运行，保留有未保存编辑的窗口。
- UE 重建：项目 `Scripts/build_resource_processing_factory_ue.ps1`。先保存并关闭本项目 Editor；`-SkipImport` 仅生成蓝图与地图。本目录 `Scripts/` 提供对应交付副本。
- UE 截图：`Scripts/render_resource_processing_factory_ue_preview.py` 在已打开的演示地图中调用 `start()`；`Scripts/Blender/encode_resource_processing_factory_ue_video.py` 将实际截图编码为视频。
- [Open_RPF_Showcase.ps1](Open_RPF_Showcase.ps1) 可按已验证参数重新打开演示地图。

本次使用普通源码版 Editor `-ExecutePythonScript` 导入，避开同步 MCP 导入。资产保存阶段使用 NullRHI；地图阶段使用 DX12 离屏渲染。本机普通空关卡也出现 GPU 等待，使用进程参数 `-ini:Engine:[SystemSettings]:r.D3D12.AllowAsyncCompute=0` 后正常窗口和离屏渲染均通过。未改写项目渲染配置；未新增 C++ 或自动化测试文件。

动画分别导出 FBX 并共享同一骨架，按 [Epic UE 5.7 FBX 动画流程](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-animation-pipeline-in-unreal-engine?application_version=5.7) 接入。
