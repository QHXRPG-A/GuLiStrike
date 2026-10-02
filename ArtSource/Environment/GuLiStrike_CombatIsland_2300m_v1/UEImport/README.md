# 2.3 公里战斗海岛 UE 导入

已保存关卡：`/Game/Maps/LVL_CombatIsland_2300m_v1`。编辑器停留在此关卡的海岛总览视角。

## 资源位置

| 内容 | 位置 |
|---|---|
| UE 关卡 | `Content/Maps/LVL_CombatIsland_2300m_v1.umap` |
| 六分区地形材质、海面材质与 LayerInfo | `Content/GuLiStrike/Environment/CombatIsland_2300m_v1/` |
| Gaea 高度图、六张分区遮罩及原布局记录 | `GaeaExports/` |
| UE 高度图、六张权重图、同时导入用的权重数据 | `LandscapeInputs/` |
| 保存重开、数据读回、碰撞与 UE 截图 | `Evidence/` |
| 来源哈希、缩放、分区顺序与物理采样点 | [import_manifest.json](import_manifest.json) |

原始可编辑 Gaea 工程仍位于 `C:/Users/a/Documents/Gaea/MCP/Projects/GuLiStrike_CombatIsland_2300m_v1/GuLiStrike_CombatIsland_2300m_v1.terrain`。本目录保留 UE 重导入所需的导出与布局资料；完整 Gaea 节点工程和生成遮罩仍由该 MCP 工程目录管理。

## 尺寸与坐标

- Landscape：4081×4081 个顶点，16×16 组件，每组件 255×255 四边形、1 个 Section。
- 全图 2300×2300 米，世界 X/Y 范围 −115000 至 +115000 厘米；北方为世界 +Y，东方为 +X。
- XY 缩放 `230000/4080 = 56.372549019607845`。
- Z 缩放 `14000*128/65535 = 27.344167238879987`；Actor Z 为 `5000.106813153277` 厘米。
- 物理高度 `world_cm = (u16-32768)/128*ZScale + ActorZ`，保留海底 −20 米、最高点 120 米、海面 0 米。
- Gaea 图片首行是北方，UE Landscape 首行对应最小 Y；高度图和全部六张权重同步上下翻转。4081 高度样本保留原始 16 位数值。
- 六张分区遮罩从 4096 双线性缩放至 4081，归一化后转为 8 位权重，所有顶点的六通道总和为 255。

## 编辑与观察

地形 Actor：`Landscape_CombatIsland_2300m_v1`，六个可绘制 Target Layer 为 `FlatGround`、`Hills`、`Plateau`、`Rock`、`Beach`、`Water`。材质采用六种可调色块与固定方向三档明暗，供地形轮廓与分区查看。

海面 Actor：`CombatIsland_SeaLevel_0m`，Z=0，`NoCollision`。海底碰撞由 Landscape 提供。西南 `CombatIsland_PlayerStart_SW` 位于集结区上方。关卡使用已有 `GuLiCommanderGameMode`；本次完成地形资产导入，玩法导航、资源点、植被与建筑布置不在本次交付范围。

![UE 海岛总览](Evidence/02_overview.png)

其他观察视角：[俯视](Evidence/01_topdown.png)、[西南集结区，约 −55° 战术视角](Evidence/03_commander_SW.png)、[东侧高台与峡谷](Evidence/04_east_canyon.png)、[西北山脊](Evidence/05_northwest_ridge.png)。相机参数见 [camera_presets.json](camera_presets.json)，其旋转按 `pitch/yaw/roll` 命名。

## 验证与重导入

[delivery_validation.json](Evidence/delivery_validation.json) 记录最终验证：保存后重新打开关卡；16 位高度图与输入逐像素相同；全部六张 UE 导回权重与输入相同；256 个 Landscape 碰撞组件存在；集结区、路线控制点、山峰和海底共 24 个编辑器碰撞采样点命中 Landscape。编辑器碰撞检查未启动 PIE。

导入脚本使用现有 `LandscapeService` 与 `GuLiLandscapeAuthoringLibrary`。高度图经 UE 原生导入；六层权重通过交错文件一次写入，避免逐层导入的权重重平衡。所有引用为项目内资产；地图使用 Engine 默认模板的天空与照明及 Engine Plane 网格，无新增第三方运行时依赖。

从项目根目录执行以下命令。先确认当前打开的是上述海岛关卡且没有其他未保存资产；脚本会检查关卡与资产所有权。

```powershell
python Scripts/prepare_combat_island_ue_sources.py
python Scripts/run_combat_island_import.py import_height
python Scripts/run_combat_island_import.py import_weights
python Scripts/run_combat_island_import.py save
python Scripts/run_combat_island_import.py reopen
python Scripts/run_combat_island_import.py verify
python Scripts/run_combat_island_import.py collision_checks
python Scripts/verify_combat_island_ue_delivery.py
python Scripts/run_combat_island_import.py view:02_overview
python Scripts/run_combat_island_import.py save
```

`setup`、`materials`、`create_terrain`、`scene` 是首次创建阶段，会拒绝覆盖已有同名产物。已有工程只使用上面的重导入阶段。UE Python 桥为本机现有 `127.0.0.1:12029`，所有 UE 操作按顺序调用。

## 2026-10-02 会话验收

用户明确要求本会话文档全部标记通过，当前交付按此消息验收。阶段测试范围和源数据记录保留；全图连通认证暂关，运行时两点可达预检保持。R2C2 同格平移25米后30米落地区最大采样坡度2.643°，已重新烘焙保存。

LandscapeInputs中的r16和u8打包缓冲已清理，可按上述数据准备脚本从源PNG重建；Gaea导出高度及六权重、可编辑工程和相对输入继续保留。
