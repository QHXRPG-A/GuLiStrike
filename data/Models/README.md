# 模型目录与本地阵营接口

维护入口是 [GuLiStrikeModels.xlsx](../Excel/GuLiStrikeModels.xlsx)。Models、Parts、MaterialParameters、ColorRegions 保持三行元数据；`_Guide` 是填写说明。JSON、CSV 与 Generated 头文件均由导出器生成，不手改。

当前登记 84 个稳定模型 ID、103 个部件、2,844 条 Vector/Scalar 绑定、9,402 条区域记录。Models 已增加 Description，84 行均有中文用途说明，原生行结构及 DataTable 已同步。2,782 条绑定来自现有材质，62 条正式 CPD 绑定包含扫荡者新增的 TeamPrimary／TeamEnabled，组合模型展开核对70项、错误及待制作遮罩均为0。取消的两类占位和扫荡者未使用的第二队色／灯参数共10条候选绑定，以及两条占位区域记录已移除；模型ID和原资源保留。颜色默认值是线性值，Models 中色卡字符串按 sRGB 转线性。ID 与兵种、建筑、部件及网络身份独立，发布账本禁止删除、重排和复用，后续 ID 只追加。

| 身份范围 | ModelId | 说明 |
|---|---|---|
| 兵种 | 1001–1006 | WM01／重防号为 1002；UnitTypeId 仍是 2 |
| 玩法建筑 | 2001–2008 | 矿厂 2006 指向原有实际装配；2008 共用彼之矛分区设计 |
| SSF 建筑 | 3001–3006 | 六座现有资源，不新增玩法建筑类型 |
| Ground 机甲 | 4001–4004 | 4001 为腿与组合入口；装甲、肩部、机枪通过 Parts 绑定 |
| 舰体 | 5001、5002 | Default、Dreadnought；既有姿态、挂点、缩放不迁入模型数值 |
| 活跃飞船部件 | 5101–5113 | 14 个 PartId 使用 13 个物理模型；Thor 两级复用 5113 |
| 旧占位部件 | 5901–5904 | 历史行保留，不作为活跃部件替代资源 |
| 僚机 | 6001 | 原模型与当前配色 |
| 矿石玩法模型 | 7001–7024 | 原 24 个资源／阶段 |
| 装配子件及兼容对象 | 8001–8016 | 矿车与建造车同名不同路径分别登记；8015 为矿厂坡道；8016 为 Ground 早期占位，均非 WM01 |

## 接口

`UGuLiModelRegistrySubsystem` 是 WorldSubsystem，支持游戏和编辑器预览。C++／Blueprint 提供 GetModelDefinition、GetModelParts、GetMutableColorRegions、GetMaterialParameters、LoadStaticModel、LoadSkeletalModel、LoadPresentationClass。ResourceType 指定 StaticMesh、SkeletalMesh 或 PresentationClass，资源类型不符时返回空；后者填写 `_C` 完整软引用。服务器继续解析必要资源，不跳过骨骼姿态或 socket。

`GetVectorParameter`／`GetScalarParameter` 必须传目标 MeshComponent、ModelId、PartKey、材质槽和统一键。MID 从目标实例读取覆盖后的值；CPD 从目标组件读取索引中的实际值。未写入的 CPD 元素是 0，不替换成材质默认值；参数类型、槽或 CPD 索引不符返回 false。例如旧 SSF 的统一键 TeamPrimary 实际绑定 `Team Color`，候选版本绑定 `GuLi_TeamPrimary`。

`SetVectorParameter`／`SetScalarParameter` 仅允许表中 RuntimeWritable 且非 TeamManaged 的绑定。动画／VAT 内部参数只读；队色由 LocalPlayerSubsystem 的 ApplyLocalTeamColors 管理。更换装配使用 ModelPresentationComponent.SetModelId／RefreshModel，普通组件使用 RegisterModelComponent，结束时 UnregisterModelComponent；不修改共享材质资产或 MPC。

```cpp
auto* Catalog = World->GetSubsystem<UGuLiModelRegistrySubsystem>();
FGuLiStrikeModelsModelsRow Definition;
if (Catalog->GetModelDefinition(GuLiModelIds::WM01, Definition))
{
    auto Regions = Catalog->GetMutableColorRegions(Definition.Id);
    FLinearColor Actual;
    Catalog->GetVectorParameter(MeshComponent, Definition.Id,
        TEXT("Root"), TEXT("*"), TEXT("TeamPrimary"), Actual);
}
```

旧 SSF 的材质槽应填写表中实际槽名；`*` 用于 CPD 绑定。默认查询正式 Existing 范围，ReviewCandidate=true 查询候选；候选路径不允许在 GameWorld 加载。本次已按用户许可完成源码版UE5.7原生编译及九表导入；实例 MID/CPD 的实际值读写、权限拒绝和外部覆盖后的队色恢复已做运行核对。

## 区域、颜色与审核

一般不透明主体用顶点 Alpha 的 `PaintRole/255`；0 乳白、1 米砂、2 深灰、3 浅队色、4 深队色、5 固定暖灯、6 固定青灯、7 浅队色灯。RGB、UV 与几何不用于存放新队色数据。先替换队色，再进入原三档明暗和线稿；透明、显示屏与固定功能材质保持用途。扫荡者1005使用下面明确登记的原UV贴图遮罩例外，正式网格顶点Alpha保持原样。

| CPD | 用途 |
|---|---|
| 8–11 | GuLi_TeamPrimary，线性 RGBA |
| 12–15 | GuLi_TeamSecondary，线性 RGBA |
| 16 | GuLi_TeamEnabled |
| 17 | GuLi_TeamLightStrength，表中默认 0.35 |

保留受击索引 7，Mass 的每实例动画数据单独管理。己方 #6AA4BE／#274E61，敌方默认 #A34053／#662249；身份未知时队色区深灰、队色灯关闭、脚环隐藏。界面脚环始终己蓝敌红，世界径向宽度 20 cm，选中仅增加四个同色外缘标记。

工程车、Ground、DIY 舰体／部件、僚机和矿石登记现有资源与参数，本轮不自动改阵营涂装。14 个 B_v1 与共用彼之矛施工体已依据用户“导入至UE的正式资源中，并将旧资源删除”正式入库，统一 CPD 接线生效。六座SSF旧参数作为兼容普通参数保留，不能与新TeamPrimary重复驱动。用户取消2004和2007占位改色，模型表相应开关关闭；当前不再登记 PendingAuthoringMask。用户最新“导入 UE、接入自动改色。”放行[扫荡者原橙区队色B_v1](../../ArtSource/SweeperTeamColor_v1_20261008/REVIEW.md)，1005已正式接线并完成双方实际召唤验证。用户此前要求黑块调浅；矿石四个既有材质只增加固定色卡暗面补光，保持顶点底色和全部网格，未启用本地阵营重涂。

扫荡者只将原橙色变为 TeamPrimary，其他原涂装固定。正式材质使用原UV0的精确纹理遮罩 `T_Sweeper_OrangeTeamMask`，ColorRegions 的 OriginalOrangeArmor／MaskSource 指向该正式贴图；只登记 TeamPrimary（CPD8–11）和 TeamEnabled（16）。远景简化面会跨固定色块，因此逐像素限制替色，正式网格不改顶点Alpha、不重导入。原 `SM_Sweeper_Rigid` 和 `M_Sweeper_Cel_Rigid` 路径保留，原底色贴图、三档明暗、刚性动画WPO、socket和三档LOD继续使用；无新增阵营网格。详见[正式资源回读](../../ArtSource/SweeperTeamColor_v1_20261008/formal-ue-import.json)。

源Excel及 Models／MaterialParameters／ColorRegions 三张正式DataTable已更新并[保存回读](../../ArtSource/SweeperTeamColor_v1_20261008/datatable-saved-readback.json)，未改变行结构或生成原生头文件，不需要再次编译。[双方客户端实际Q召唤](../../ArtSource/SweeperTeamColor_v1_20261008/UE/runtime-local-team-readback.json)核对各真实阵营5个扫荡者：每端己方蓝、敌方红，共用原网格／材质，动画实例步长63保持；组件登记归属刷新、[未知阵营中性灰及外部清零后的自动恢复](../../ArtSource/SweeperTeamColor_v1_20261008/UE/runtime-tick-recovery.json)通过。当前[UE实际效果画廊](../../ArtSource/SweeperTeamColor_v1_20261008/UE/index.html)含25张固定视图和4张实际召唤批次图，Mass地图的蓝红EditorOnly样例已保存并重新载入。

护盾顶三点为 role 7；前哨按完整矩形组件和外半圆环着色；空军基地顶甲、盖板和环带按 B_v1。独立 [Alpha 候选](../../ArtSource/ModelInterface_B_20261008/PaletteOnly_14Models_EncodedAlpha_InterfaceCandidate.blend)只保留一套 canonical 网格，不制作阵营 N 套资源。实际 Blender B_v1 的蓝红审核版本与原交付目录保留。

## 执行顺序与边界

1. 修改 Excel，执行 `python Tools/DataPipeline/export_data_from_excel.py`；`--validate-only` 只检查。导出核对外键、ID、类型、色值、槽／区域引用与 CPD 索引。
2. 表结构变更先取得原生编译许可并加载新模块。本次已沿用用户明确编译许可完成 GuLiStrikeEditor Win64 Development；[编译记录](native-build.json)记载最新产物及BuildId。旧 [导入预检](import-preflight.json)仅保留最初未编译阶段的历史。
3. 用 `Scripts/import_data_to_engine.py` 导入 Models 四表及 Soldiers、Buildings、Mech_Visuals、Ship_Parts、Ship_Tuning 九表。通过 `GULI_TABLE_FILTER` 指定；全量导入也走同一预检。随后执行 `Scripts/Models/seed_model_bindings.py`，迁移 CDO/config 的 ModelId，保留既有资源和装配。
4. 需要新涂装候选时执行 `Scripts/Models/stage_model_interfaces.py`。仅复制到 `/Game/GuLiStrike/Review/ModelInterface`，匹配每个原 LOD 的三角与区域；不完整、跨固定材质或对称方向不明确时拒绝。可用 sidecar 的 ue_axes 指定已核实的三条有符号源轴。矿厂坡道是整件固定米砂，直接编码原 Cube，不替换三角划分。之后重新导出并运行编辑器验证，检查候选参数 CPD 类型和索引。
5. 用户明确通过具体 B 版本后才更新 Models.ResourcePath/VATDefinition、正式参数与区域。切换时将候选 CPD／ColorRegions 设为 Existing，移除被替代的旧 TeamPrimary 驱动和旧遮罩；未使用的旧 `Team Color` 参数若保留，应改成只读普通键。禁止同时保留两个正式 TeamPrimary 驱动。旧审核配置保留历史，不能另建手写运行时配色 DataAsset。
6. 编译和导入不等于全部玩家验收；按变更范围分别记录双客户端、晚加入、归属变化、材质更换与参数实际值。当前扫荡者双方实际Q召唤、登记归属刷新和实际CPD已核对；最终美术观感仍由用户确认，未重复晚加入、所有UI环境或长时间性能检查。

当前 [静态检查](static-source-review.json)、[编辑器资源核对](editor-static-validation.json)、[Blender 保存回读](../../ArtSource/ModelInterface_B_20261008/blender-saved-readback.json)与[九表导入](import-live-20261008.json)均已记录。三个对应地图已保存回读：[实体记录](acceptance-scenes-readback.json)。用户明确授权运行效果验证后已做双指挥官、晚加入Ground、归属变化、选择/清除、MID/CPD及六种环境UI核对，详见[当前交付](../../Progress/DevelopmentDocumentation/20261008-统一模型目录与本地阵营改色.md)。长时间多客户端/批量预览曾耗尽内存，不能据此声称长期稳定或性能通过；恢复后保留已保存资源并缩小验证范围。没有新增自动化测试；用户最终美术和游戏验收待反馈。旧B入库存储授权与本次暗部修订不是最终美术通过记录。
