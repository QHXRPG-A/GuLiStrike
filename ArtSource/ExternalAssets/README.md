# 外部资源迁移清单

更新于 2026-11-18。清单记录 `20261026` 和 `20261118` 两批，共八个外部资源包。20261118 批次的迁移、归类和 GOOD FX 缺件修复已结项。

机器清单登记来源 ZIP、SHA256、目标路径和资产类型。批次记录包含导入结果、编辑器查询和源包兼容性。 [机器清单](../../data/ExternalAssets/catalog.json) · [20261026 批次](../../data/ExternalAssets/migrations/20261026) · [20261118 批次](../../data/ExternalAssets/migrations/20261118/verification.json)

## 分类与来源

所有下列路径均相对于`/Game/Assets`。对应磁盘根目录为`D:/UE5.7/test1/Content/Assets`。

| 资源包 | 分类后的最终目录 | 外部来源 | 文件数 | Registry登记 / 当前可解析 |
|---|---|---|---:|---:|
| Drone Bot | `Models/Robots/DroneBotContent` | `D:/BaiduNetdiskDownload/Drone Bot/Drone Bot 5.3/DroneBotContent.zip` | 58 | 58 / 58 |
| Mech Constructor Spiders and Tanks | `Models/Mechs/Mech_Constructor_Spiders` | `D:/BaiduNetdiskDownload/Mech Constructor Spiders and Tanks.zip`中的`Content/Mech_Constructor_Spiders` | 322 | 319 / 318 |
| Advanced Dash VFX Pack | `VFX/Movement/AdvancedDashVFX` | `D:/BaiduNetdiskDownload/UE5虚幻4 Advanced Dash VFX Pack 高级冲刺闪现视觉特效合集/【5.0-5.3】Advanced Dash VFX Pack.zip` | 186 | 186 / 186 |
| Muzzle Flashes | `VFX/Weapons/MuzzleFlashes` | `D:/BaiduNetdiskDownload/a510_枪火特效/Muzzle Flashes.zip`中的`Muzzle Flashes/muzzles` | 241 | 241 / 241 |
| Buff and Debuff VFX | `VFX/Buffs/BuffLoop` | `D:/BaiduNetdiskDownload/Buff and Debuff VFX ( Include Sun ,Bubble, Tentacle ).zip` | 184 | 183 / 183 |
| Fantasy VFX Collection | `VFX/Buffs/FantasyVFXCollection`、`VFX/Weapons/FantasyVFXCollection` | `D:/BaiduNetdiskDownload/FANTASY VFX COLLECTION.zip` | 296 | 296 / 296 |
| GOOD FX Status | `VFX/Buffs/GoodParticleStatus` | `D:/BaiduNetdiskDownload/GOOD FX Status.zip` | 249 | 249 / 249，含 4 个替代资产 |
| High Speed Thrusters | `VFX/Weapons/Missiles/HighSpeedThrusters` | `D:/BaiduNetdiskDownload/High Speed Thrusters 高速移动拖尾发光特效.zip` | 30 | 30 / 30 |
| 合计 | 八包 | 原始压缩包保留 | **1566** | **1562 / 1561** |

“当前可解析”采用各批次的 UE5.7.4 编辑器资产对象回读结果。视觉与玩法验收另行记录。

## 升级、Buff 和武器

| 分类 | 在 `/Game/Assets/VFX` 下查找 | 内容 |
|---|---|---|
| 升级 | `Upgrades/NiagaraUpgradeGlow` | 17 个 Niagara 升级辉光，含建造表现可复用的材质和网格。 |
| Buff | `Buffs/BuffLoop`、`Buffs/GoodParticleStatus`、`Buffs/FantasyVFXCollection` | 持续状态、护盾、恢复、控制与减益。Fantasy 的 Buff 粒子和专用材质共 16 个包。 |
| 武器 | `Weapons/FantasyVFXCollection`、`Weapons/WeaponBulletVFX`、`Weapons/Beam_Spells_VFX`、`Weapons/MuzzleFlashes`、`Weapons/Arc_Beam_VFX`、`Weapons/BulletTraceVFX` | 弹丸、光束、枪口火焰、拖尾、命中和爆炸。Fantasy 共用材质与贴图位于武器侧。 |
| 武器 · 导弹 | `Weapons/Missiles/HighSpeedThrusters`、`Weapons/Missiles/Missile_Cluster` | 四种火箭尾焰位于 `HighSpeedThrusters/FX`。导弹簇含演示飞弹蓝图、弹体和命中特效。`Weapons/WeaponBulletVFX/NS` 另有 `VFX_Missile` 和 `VFX_Missile_Loop`。 |

内容浏览器的本机集合 `VFX_Weapons` 汇集 302 个武器特效，包括 `/Game/Assets/Niagara_Toon_Projectiles_2` 中的 45 个 Niagara 系统。卡通弹道资源沿用正式数据表与审计工具所引用的路径。

## 按文件夹查找内容

| 最终目录（`/Game/Assets/`后缀） | 内容与用途 |
|---|---|
| `Models/Robots/DroneBotContent/DroneBot` | 无人机骨骼模型、骨架/物理资产、14段动画、动画蓝图、材质及贴图。用于机器人模型和动作参考。 |
| `Models/Robots/DroneBotContent/Rob_Podium` | 展示台骨骼网格、骨架/物理资产、材质与贴图。 |
| `Models/Robots/DroneBotContent/Blueprints`、`Maps` | 演示GameMode、鼠标与动画列表Widget，以及Overview地图与烘焙数据。 |
| `Models/Mechs/Mech_Constructor_Spiders/Meshes`、`Meshes_Skeletal` | 蜘蛛足、坦克/履带/越野底盘、驾驶舱、背包和武器。共73个静态网格、34个骨骼网格，另有骨架与物理资产。 |
| `Models/Mechs/Mech_Constructor_Spiders/Animations` | 蜘蛛足、履带、背包机构等91个动画资源。 |
| `Models/Mechs/Mech_Constructor_Spiders/Blueprints` | 17个组装/示例蓝图，包含旧PhysX越野车逻辑；不能直接作为UE5车辆玩法使用。 |
| `Models/Mechs/Mech_Constructor_Spiders/Materials`、`Textures` | 6个材质、40张纹理；迷彩、手绘配色、PBR车体和履带纹理。 |
| `Models/Mechs/Mech_Constructor_Spiders/Maps` | `MC_Spiders_Demo_Scene`与烘焙数据；`Demo_Scene`是未注册资产的旧包文件。 |
| `VFX/Movement/AdvancedDashVFX/NiagaraVFX` | 18个Niagara系统，用于冲刺、闪现与运动拖尾参考。 |
| `VFX/Movement/AdvancedDashVFX/Materials`、`Meshes`、`Textures` | 特效材质/实例/函数、特效网格与纹理，保留内部目录结构。 |
| `VFX/Movement/AdvancedDashVFX/Blueprints`、`DemoContent`、`Map` | 特效演示蓝图、DemoRoom、人体模型/动画和DemoLevel。 |
| `VFX/Weapons/MuzzleFlashes/Particles` | **101个传统Cascade ParticleSystem**，包括枪口闪光和火焰；未转换为Niagara。 |
| `VFX/Weapons/MuzzleFlashes/Materials`、`Meshes`、`Textures`、`PhysicalMaterials` | 枪火材质、贴图、网格及4个物理材质。 |
| `VFX/Weapons/MuzzleFlashes/demoroom`、`Maps` | 展示武器、人物、演示房间、蓝图和MuzzleFlashes地图。 |

## 示例地图入口

在内容浏览器中可按完整路径定位各包提供的演示地图。

| 资源包 | 完整地图路径 | 使用边界 |
|---|---|---|
| Drone Bot | `/Game/Assets/Models/Robots/DroneBotContent/Maps/Overview` | 源地图依赖未启用的HDRIBackdrop，背景需按下文恢复。 |
| Advanced Dash VFX Pack | `/Game/Assets/VFX/Movement/AdvancedDashVFX/Map/DemoLevel` | 需要Niagara；个别源材质缺贴图。 |
| Muzzle Flashes | `/Game/Assets/VFX/Weapons/MuzzleFlashes/Maps/MuzzleFlashes` | Cascade演示，运行效果待用户确认。 |
| Mech Constructor | `/Game/Assets/Models/Mechs/Mech_Constructor_Spiders/Maps/MC_Spiders_Demo_Scene` | 模型展示资源已迁入，旧PhysX车辆与部分车Actor不能在当前引擎完整加载。 |
| Buff and Debuff VFX | `/Game/Assets/VFX/Buffs/BuffLoop/Demo/OverView_DemoScene` | 体积毒雾需要与当前引擎构建号匹配的 NiagaraFluids。 |
| Fantasy VFX Collection | `/Game/Assets/VFX/Weapons/FantasyVFXCollection/0_Scene/Preview_Scene` | 一个地图展示武器与 Buff 两侧的效果。 |
| GOOD FX Status | `/Game/Assets/VFX/Buffs/GoodParticleStatus/Maps/Level_GoodParticleStatus` | 已补齐角色动画、Chaos、火焰护盾及熔岩材质。角色引用 BulletTraceVFX 的 Enhanced Input 动作和映射。 |
| High Speed Thrusters | `/Game/Assets/VFX/Weapons/Missiles/HighSpeedThrusters/Maps/Overview` | 四种火箭尾焰的源包演示。 |
| Missile Cluster | `/Game/Assets/VFX/Weapons/Missiles/Missile_Cluster/001` | 既有导弹簇演示。旧 `/Game/Assets/Missile_Cluster/001` 指向该地图。 |

## 已知源包问题与保留文件

1. **Drone Bot：** 源Overview引用`/HDRIBackdrop/Blueprints/HDRIBackdrop`及相关材质，当前编辑器未挂载该插件。迁移保存时这些背景对象无法完整加载；需要启用插件并从原始包恢复后再核验背景。原始Overview已另存到兼容性备份。源动画蓝图还出现“Widget查询为空仍取索引0”的警告，本次没有改写示例逻辑。骨架预检对`/Game/DroneBot/Meshes/DroneBot`的缺失外部引用在迁移保存后的Registry中不再出现，不能据此宣称示例已完成视觉验收。
2. **Advanced Dash：** `Materials/MaterialInstances/MI_TrailBeam`仍引用源包没有提供的`/Game/CharacterVFXPack/Textures/T_Wave`。预检的`MF_Hilight`还引用`/Game/AdvancedNiagaraVFX/Textures/Mask/T_Mask12`，该引用在保存后的Registry中不再出现；两个原始材质包已保留。没有猜测替换缺图或重新设计配色。
3. **Mech Constructor：** `Blueprints/Pawn_Buggy_Chassis/Wheel_Buggy_Tire`是当前引擎缺失的PhysX `TireConfig`，因此原样保留但无法加载。涉及`PhysXVehicles/PhysXVehiclesEditor`的车辆和动画蓝图需要后续适配；加载演示地图时存在车辆类及部分Actor外部容器无法创建的警告。相关源蓝图、动画蓝图和地图已备份，后续兼容工作应以这些原文件为恢复依据。
4. **Mech Constructor的3个未注册旧包：** `MyCarGame.uasset`、`Blueprints/Pawn_Buggy_Chassis/MyCarGame.uasset`、`Maps/Demo_Scene.umap`已在最终目录保持原文件。它们没有被计入803个可解析资源，也不列为可用示例入口。
5. **源对象名修正：** `Meshes/Projectile_Grenade_3.uasset`原先包含名为`Projectile_Grenade_1`的对象，迁移时通过UE AssetTools统一为`Projectile_Grenade_3`并更新引用；映射保存在机器清单。

[16份兼容性相关原文件的来源与校验记录](../../data/ExternalAssets/migrations/20261026/compatibility_originals.json)对应[原文件备份目录](../../data/ExternalAssets/migrations/20261026/compatibility-originals)。它们是维护源文件，不是另一套运行资源，不应直接用文件操作覆盖已迁移资产。

GOOD FX Status 的四个不可读原文件保存在 ZIP。项目中的对应路径已补齐，`ThirdPerson_AnimBP` 使用同骨架动作重建，`GS_GPS_Hera` 和 `PS_GPS_Chaos` 分别沿用同包的心形与眩晕粒子，`T_GPS_BurningTex_02_D` 采用同包燃烧贴图。四个旧暂存副本已清理，正式目录共 249 个可解析包。 [GOOD FX 修复核对](../../data/ExternalAssets/migrations/20261118/good-fx-repair.json)

BuffLoop 已采用正名 `Vfx_ControlParasite`，旧拼写 `Vfx_ControlParastie` 是原 ZIP 内的无引用重定向。体积毒雾 `Vfx_GpuVolumetricPoison` 依赖 NiagaraFluids。现有插件模块与源码版编辑器的 BuildId 不同，项目已撤回启用项并验证编辑器启动。 [源文件与引用记录](../../data/ExternalAssets/migrations/20261118/verification.json)

## 去重、依赖与迁移边界

- Drone Bot采用所提供的最高版本目录5.3；4.27、5.03、5.1、5.2目录不重复迁入。
- Advanced Dash原ZIP与`(1).zip`的SHA256相同：`7281d99466c4d0a65e4f35dda51c77deba09fd7fdfccdf4bc7a6b48e9d9a0263`，只迁入一份。
- 图片预览、安装说明、广告和`.url`文件未作为游戏资源导入；没有执行外部说明中的指令。全部原始下载文件保留。
- 原包未包含独立`.uproject`、`.uplugin`、外部Actor或外部Object支持目录。本次没有遗漏已有的`__ExternalActors__`或`__ExternalObjects__`目录。共有的引擎、Niagara及引擎重存产生的ACLPlugin引用继续使用引擎资源，不复制到`/Game/Assets`。
- 20261026 批次登记 804 个包，其中 803 个可解析。兼容性见该批次记录。20261118 批次保存并回读 754 个新包和 444 个重组包，25 个既有引用方均指向新路径。
- 新目录资源用于模板复用。GOOD FX 的四个替代资产和既有引用已在 UE 回读，视觉验收通过包内演示地图进行。

## 后续维护方式

1. 新资源先在`data/ExternalAssets/catalog.json`新增条目，使用稳定`id`和最终`target_root`；记录来源ZIP、SHA256、原始根目录、版本、文件/资产数量、主要用途、示例地图和限制。
2. 包内层级优先保留。重命名或移动资产使用UE编辑器API，并同步机器清单、本文及新的Progress增量归档，保留历史来源映射。
3. 按显式来源清单核对目标文件和Registry；不遍历`Content/Assets`，不直接分析二进制资产。
4. 迁移完成与视觉/运行验收分别登记。缺失依赖不得用占位资源静默替换，适配在项目自有副本中进行。

20261026 批次使用的脚本包括来源暂存、编辑器迁移和清单生成。 [来源暂存](../../Scripts/AssetMigration/stage_requested_packs_20261026.py) · [编辑器迁移](../../Scripts/AssetMigration/migrate_requested_packs_20261026.py) · [清单生成](../../Scripts/AssetMigration/publish_catalog_20261026.py)

归档记录包含迁移范围、分类依据和验证结果。 [20261026 批次归档](../../Progress/Archive/20261027-四批外部资源分类迁移与清单维护.md) · [20261118 批次归档](../../Progress/Archive/20261118-升级Buff与武器特效资产迁移.md) · [GOOD FX 缺件修复](../../Progress/Archive/20261118-GOOD-FX-Status缺件修复.md)
