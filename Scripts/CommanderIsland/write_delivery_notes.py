"""Write current source and handoff notes from the retained authoring evidence."""
import json
from pathlib import Path

REPO=Path(__file__).resolve().parents[2]
NAME='GuLiStrike_CommanderIsland_1800m_v1'
MASTER=Path.home()/'Documents/Gaea/MCP/Projects'/NAME
FILES=REPO/'ArtSource/Environment'/NAME
FINAL=MASTER.parent.parent/'Builds'/NAME/'final'
validation=json.loads((FINAL/'validation.json').read_text(encoding='utf-8'))
bake=json.loads((FILES/'EditorEvidence/author_layout_bake.json').read_text(encoding='utf-8'))['result']
assert validation['passed'] and bake['success']
scene=json.loads((FILES/'EditorEvidence/saved_scene_entities.json').read_text(encoding='utf-8'))
import_check=json.loads((FILES/'UEImport/Evidence/delivery_validation.json').read_text(encoding='utf-8'))
assert scene['success'] and import_check['success']
routes={r['name']:r for r in validation['routes']}
source=f'''# GuLiStrike · 1800 米指挥官海岛 v1

在 Gaea 中打开同目录的 `{NAME}.terrain`，完整保留 `inputs/` 和 `scripts/`。工程独立派生自 2300 米海岛，原工程和原 UE 海岛地图保留。

## 尺寸、节点和编码

全图 1800×1800 米，北方 +Y、东方 +X，主陆地约 1600×1600 米，外围约 100 米海域。Gaea Width=1800、Height=140、Ratio=140/1800，种子 20261001，1024 预览、4096 正式构建。工程有 39 个节点、33 条连接、14 个相对路径 File 节点、75 张源 PNG；地形生成、Combine 和局部侵蚀通过布局遮罩组合，平台和通路受侵蚀保护。

世界高度为 `u16/65535×140−20` 米，海平面 0 米，高度范围 −20～120 米。Gaea 原生最低点对应世界 −20 米，海平面归一化值 1/7。4081² 兼容高度保留同一物理范围，采样间距 1800/4080 米；R16 为小端无头 uint16。UE 导入时高度与六类权重一起翻转，六层归一化后每顶点权重和为 255。

## 可编辑布局与验收

`layout.json` 保存 49 个实际据点锚点、各自逻辑格中心和 53 条有效路线。平台至少 30×30 米；基地 Y±740 米，集合点 Y±690 米。中央据点位于 (100,110) 米，直接接入主路；北方据点位于 (72.4286,733.8571) 米，南方据点位于其镜像。

`terrain_grading_pads/routes` 保留生成高度时使用的完整整平布局，`pads/outposts/routes` 保存最终玩法布局。中央和南方据点的新锚点位于已有平地内，元数据调整不改动已验收高度。

4096 原生构建 Success；全分辨率检查只有一个主陆地分量。陆地坡度不超过 15° 的比例为 {validation['gentle_land_fraction']*100:.2f}%。49 据点及两端基地均接入可行走地面网。中央、西侧、东侧完整走廊最大坡度分别为 {routes['Central']['maximum_slope_degrees']:.3f}°、{routes['West']['maximum_slope_degrees']:.3f}°、{routes['East']['maximum_slope_degrees']:.3f}°，全部通路最大 {max(r['maximum_slope_degrees'] for r in validation['routes']):.3f}°；平台坡度均为 0°。

这些记录证明地形源数据。UE 导入、导航与玩家场景的独立证据在项目内交付目录。

## 输出与重建

默认输出目录为 `../../Builds/{NAME}/final/`，包含 4096² 16 位高度、4081² PNG/R16、六类 `mask_*.png`、俯视/坡度/路线图、原生构建报告、`validation.json` 和 `delivery-checks.json`。项目内交付另复制输出至 `UEImport/GaeaExports/`。

```powershell
& './scripts/Rebuild-Gaea.ps1' -Stage final
```

`-Stage draft` 构建 1024 草稿；修改源布局参数后加 `-RefreshLayout`。脚本执行原生高度构建、数据分析、六类遮罩同步和最终构建核对。只修改 Gaea 节点时无需刷新源布局。依赖已配置的本机 Gaea MCP、Node.js、Python NumPy/Pillow；脚本按所在目录配置独立输出，不修改 2300 米来源工程。

固定提示词：“自然非对称的战斗海岛，宽阔平原连接低丘、高台和峡谷，外围具有沙滩、浅湾及岩石陡岸；主岛连续，主要路线宽阔平缓，地形轮廓清楚。”来源见 `REFERENCES.md`。
'''
(MASTER/'README.md').write_text(source,encoding='utf-8')

handoff=f'''# 指挥官 1800 米海岛与 7×7 据点交付

指定关卡：`/Game/Maps/LVL_CommanderMassPrototype`。保留 `GuLiCommanderGameMode` 和指挥官玩法入口；全图 1800×1800 米、主陆地约 1600×1600 米、海平面 0 米、高度 −20～120 米，49 个据点，各格 257.142857 米。

## 文件位置

- [相对路径 Gaea 工程](Source/Projects/{NAME}/{NAME}.terrain)：75 张源 PNG、布局和独立重建脚本随目录保留。
- [UE 导入记录](UEImport/import_manifest.json)：4081 高度、六类权重、实际比例及高度偏移。
- [地形源检查](UEImport/GaeaExports/validation.json)与[文件校验](UEImport/GaeaExports/delivery-checks.json)：4096 原生构建及地形规则。
- [预算重采样](budget_resampling.json)、[49 点身份迁移](marker_migration.json)、[道路预留](road_clearance_regions.json)、[分配前矿簇容量检查](resource_capacity.json)。最终预算以重采样记录和成功烘焙为准。
- [资源烘焙](EditorEvidence/author_layout_bake.json)：49 领土、200 蓝矿簇、40 红矿簇、6240 节点、500/500 出生槽。
- [原生编译](EditorEvidence/native_final_build.json)：源码 UE5.7，`GuLiStrikeEditor Win64 Development`，用户授权编译及重启。
- [地形导入精度](UEImport/Evidence/delivery_validation.json)、[保存场景实体](EditorEvidence/saved_scene_entities.json)记录独立的编辑器核对范围。
- [俯视](UEImport/Evidence/01_topdown.png)、[全图斜视](UEImport/Evidence/02_overview.png)、[东侧峡谷](UEImport/Evidence/04_east_canyon.png)、[三色指挥官视角](UEImport/Evidence/07_outpost_three_colors.png)。三色图使用已批准候选的静态姿态，不是运行时动画证据。

## 数据与表现

9×9 通过面积重采样和镜像成对分配迁移到 7×7，保留 49 个仍使用的 Marker/Region 身份，显式移除 32 点，布局版本 6。山脊、岸线、平台和整簇矿石坡度约束不足的格子，按记录成对调配蓝矿预算；资源总量和经济规则保留。候选中心间距至少 55 米，据点避让 65 米。

25 米密度网格允许部分格子跨道路边缘；独立 `ResourceClearance` 作者标记使用 Box/Cylinder 描述 53 条有效路线，按实际矿簇中心排除道路半宽加 5.2 米簇占地。烘焙先布中央和基地，再布候选较少的格子，同时检查全图间距、实际 26 节点坡度和 500 出生槽。此规则只存在于作者端，运行时继续使用 Cooked 布局。

编辑器已保存重开，4081² 16 位高度和六类权重均与准备文件逐像素一致，每点权重和 255；高度 −20～120 米，导入后陆地坡度 ≤15° 的比例为 {import_check['gentle_land_at_most_15_degrees_percent']:.2f}%。180 点地形碰撞抽查最大误差 {import_check['collision_max_error_cm']:.4f} 厘米；据点、矿簇、矿节点和基地共 6533 个地面位置回读最大误差 {scene['resource']['maximum_ground_error_cm']:.4f} 厘米。

两类地面导航与飞行缓存已校验；77 个水域体积使用 `NavArea_Null`。本关卡的两类 Recast 查询上限从 2048 调至 16384，解决扩展路网的搜索截断；重开后两类代理各 52 条寻路查询均完整到达 49 据点和基地。这些是编辑器地形与导航数据证明，运行时生成建筑、矿簇障碍后的移动仍按玩家验收记录。

据点使用指定 `SM_OutpostPlaceholder`，等比缩放 1/30、高 10 米，保持原表面贴图。正式材质位于 `/Game/GuLiStrike/FX/StrongholdOutpost`；中立白光，红蓝使用项目阵营色，独立柔边光晕。

`GuLiOutpostPresentationComponent` 缓存材质并重建动画；归属与起始服务器时间同一复制快照。占领后高度为 `50×(1−cos(2πt/5))` 米，范围 0～100 米、周期 5 秒；换主保留相位，中立恢复后 0.5 秒平滑落地。专用服务器不创建模型、材质或动画 Tick。

规则格中心与 `OutpostGroundLocation` 分开。Actor 及 6×6×10 米地面碰撞固定，浮动模型只有 Visibility 点击查询；命令、设施、运输和资源避让读取地面锚点。

## 重建顺序

1. 在源工程目录运行 `scripts/Rebuild-Gaea.ps1 -Stage final`；改变宏观源布局时再加 `-RefreshLayout`。保留母工程，不重新创建派生文件夹。
2. 项目根目录运行 `python Scripts/CommanderIsland/prepare_ue_sources.py`，生成翻转后的高度与共同归一化的六层权重。
3. 使用已打开的源码版 UE 编辑器与 Python 桥，按 `run_stage.py import_island.py import_height`、`import_weights`、`water_navigation`、`verify`、`collision_checks` 更新当前指挥官地形。已交付工程已有地形，不重复运行首次 `setup/create_terrain/scene`。
4. 更新布局时先运行 `calibrate_budget_metadata.py` 和 `prepare_layout_migration.py`，再执行 `author_layout.py resume_markers`、`author_clearance.py update`。`migrate/create` 只用于显式首次迁移，不能对现有文件重复执行。
5. 执行 `author_scene.py flight_bounds`、`navigation_query_budget`、`navigation` 和 `author_layout.py bake`；返回结果必须明确 success，失败不会覆盖正式资源 DataAsset。
6. 最后执行 `author_scene.py fixtures`、`preview_off`、`import_island.py save/reopen`、`readback_scene.py read`，保存当前地图并读取相关实体。使用 `verify_ue_delivery.py` 对比源与 UE 回读像素。

入口示例：`python Scripts/CommanderIsland/run_stage.py author_layout.py bake --timeout 600`。此桥连接已打开的编辑器；不启动 PIE、Standalone 或自动化测试。原生改动沿用项目编译授权规则。

## 玩家验收

关卡中 `CommanderPlayerStart_Red`、`CameraActor_2`、`Note_0`～`Note_6` 提供真实指挥官入口、观察位置和步骤。默认红方 R1C4、蓝方 R7C4，双方各 250 Mass、8 矿车和 8 建造车。

在玩家自行开始本关卡后，先观察中立贴地和初始归属的阵营色，派兵占领中立格。占领后 0/1.25/2.5/3.75/5 秒应离地 0/50/100/50/0 米。Development 服务器控制台可依次执行 `gs.Resources.SetTerritoryOwner 4 4 Red`、`Blue`、`Neutral`：换主只变色，中立恢复 0.5 秒落地。Shipping 不注册该命令。

检查点击悬浮模型后的移动终点、占领中心、设施与运输端点保持在地面。服务器先占领据点，再让客户端晚加入，检查两端浮动相位一致。助手本轮只完成静态代码、编辑器资产、导航和场景核对，未运行 PIE 或自动化测试；动画、晚加入及玩法效果等待玩家验收。

资源 SourceHash：`{bake['source_hash']}`；LayoutHash：`{bake['layout_hash']}`。
'''
(FILES/'README.md').write_text(handoff,encoding='utf-8')
print(json.dumps({'success':True,'source_readme':str(MASTER/'README.md'),'handoff':str(FILES/'README.md')}))
