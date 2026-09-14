# GuLi Map Authoring

UE 5.7 项目级编辑器插件。它生产地图布局数据，不是 GameMode、运行时玩法模块或独立 exe。

项目内维护 Skill：[guli-map-authoring](../../.agents/skills/guli-map-authoring/SKILL.md)。以后询问本插件的使用、导出、故障、源码维护或扩展时可直接调用 `$guli-map-authoring`；Skill 会按任务只加载操作、架构、数据合同或维护验证参考。

交付状态（0.2.0，2026-09-10）：地图标记能力保持兼容，并新增红蓝矿密度涂绘、Territory 汇总和三份扩展导出。源码版构建与 `GuLi.MapAuthoring` 10/10 自动化通过；资源热图、双层混色、笔刷手感和 8km 双层压力仍待重启源码 Editor 后人工验收，详见[本次开发记录](../../Progress/DevelopmentDocumentation/20260910-地图资源密度涂绘与导出.md)。正在运行的旧编辑器需要保存工作并重新启动，才能载入新原生插件；使用 `D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor.exe`，不要仅凭 `.uproject` 关联判断引擎版本。

## 开始使用

1. 用源码版 UE 打开项目，在“工具 → GuLi → 地图标注”打开面板。
2. 第一次使用点击“生成三个预设”，然后保存。Resource、Outpost、Generic 位于 `/Game/GuLiStrike/Editor/MapAuthoring/Types`。此按钮只创建缺失项，不覆盖已有配置。
3. 选类型，点击“视口放置”，再单击场景。勾选“表面优先”时使用碰撞表面；没有命中时使用指定 Z 工作平面。取消勾选则始终使用平面。Alt 保留相机，Esc 退出。
4. 也可从“世界原点”或“所选 Actor 原点”新建。列表与视口同步选择；双击列表或按 F 聚焦。
5. Details 编辑业务键、名称、字段及 Regions。可以给一个据点同时添加 Capture、Build、Influence 等多个独立区域。RegionKey 在该标记内唯一。
6. “校验”列出问题；点击问题定位标记。“保存并导出”先走 UE 保存流程，取消保存则不导出。

Marker 保存于当前地图持久关卡的 `GuLi/MapAuthoring` 文件夹。禁止附着到其他 Actor、移动到子关卡/Level Instance、设置空间加载或流送 Data Layer。Marker 无碰撞、导航、Tick，不进入 PIE/游戏。

### 红蓝矿资源涂绘

1. 在面板顶部切到“资源涂绘”，点击“创建/补齐密度图”。当前地图会在 PersistentLevel 创建唯一的 `GuLi Map Density` Editor Actor，并补齐 `BlueOre/蓝矿`、`RedOre/红矿`；不会覆盖已有图层。
2. 选择蓝矿或红矿，可分别控制两层预览显隐。两层数据彼此独立，可以在相同格上同时有值。
3. 选择“加深”或“擦除”，设置半径、强度和软边，再点击“进入涂绘”。默认半径 200m、强度 25%、软边 50%；LMB 绘制，Alt 保留相机，Esc 先取消当前笔画，再按一次退出。
4. 一次拖拽是一次事务。路径按固定世界距离补点，同一笔内每格只取最大覆盖，所以结果不依赖鼠标事件频率；Undo/Redo 会恢复整笔。
5. 默认格尺寸 25m。空图可直接修改；已有数据时“应用格尺寸”会明确要求清空全部，不会隐式重采样。“清空当前层”只影响所选图层，两种清空均可 Undo。

密度格按 `floor(WorldXY / CellSizeCm)` 世界对齐，负坐标也遵守向下取整。Z 不进入业务数据；热图显示会查询 Landscape 高度并缓存格角，无 Landscape 时回退 Visibility 命中或工作平面。预览由单个代码 SceneProxy 批量绘制，不创建逐格 Actor/Component。

密度值 `0..255` 仅表示相对生成权重。插件不定义全图矿量、单矿储量、刷新或采矿，也不会在 PIE 生成矿物。

新创建的 Outpost 预设同时具有 `Capture` 圆柱和独立 `Territory` 多边形。已有 Outpost 类型/Marker 不会自动改写；请按需要手动增加 `RegionKey=Territory` 的水平 PolygonPrism。缺少辖区或密度未归属/严格重叠会显示警告但允许导出；错误形状或倾斜 Territory 是阻断错误。共享边界上的格中心按稳定 MarkerKey/RegionId 唯一裁决。

### 范围操作

存储和界面空间量均为 **cm**（100cm=1m），角度 degree。Actor Scale 固定为 1，尺寸用形状参数控制。Box 存半尺寸，完整宽/深/高等于各轴半尺寸 ×2。

| 形状 | Details 字段 | 视口控制 |
|---|---|---|
| Cylinder | Radius、MinZ、MaxZ | 半径、上下边界 |
| Sphere | Radius | 半径 |
| Box | HalfExtents | 三轴尺寸；中心控制点支持 W 移动 / E 旋转 |
| PolygonPrism | Vertices、MinZ、MaxZ | 白点拖顶点；绿点选边后 Insert 插中点；Delete 删顶点；Ctrl+R 反转顶点顺序；上下高度 |

区域中心白点用于相对移动/旋转，Actor 原生 gizmo 用于移动整组。点位可没有区域。轮廓/填充常驻，所选 Marker 显示区域控制点。

轮廓线宽固定为屏幕像素，远景也可读；半透明填充按 UE 材质的真实渲染阶段绘制。按 G 进入游戏视图时编辑辅助形状隐藏，返回编辑视图恢复。

一次拖动是一个事务；临时自交/退化轮廓标红且不填充，松开时无效则回滚整次拖动。切换地图、关闭面板、进入 PIE 或 Esc 退出时取消未提交的编辑。Details 的手工输入由原生事务记录，无效数据可以留待修复，但不能导出。

多边形允许凹形，不允许自交、非相邻边相接、重叠边、零长度边和内孔。首版上限 4096 顶点；不重复存储首顶点。

## 新增类型和字段：无需 C++

在面板输入 `SupplyPoint`，点“创建类型”，编辑名称/颜色、允许形状、默认区域和 DefaultParameters，然后保存。已有类型可用“编辑类型/字段”直接打开同一个 Details。

DefaultParameters 使用 UE Property Bag 的添加/改名控件。支持 bool、int32、double、Name、String、枚举、Vector、软对象和软类引用；容器、Text、float、硬对象引用等不在首版合同内，校验会阻止导出。字段键/TypeId/业务键/RegionKey 使用英文字母或下划线开头、后接字母数字下划线，中文用于 DisplayName、备注和字符串值。

字段 GUID 是身份，键是名称。FieldRules 的 FieldId 指向该 GUID，可以设置中文标签、必填和数值上下界。GUID 可从快照的 `types[].fields[]` 获取；不要删除字段再新建来模拟“改名”。

- 新增或改名后，选中已有 Marker，点击 Details 下的 **Synchronize Type**（同步类型定义）。同 GUID、同类型的值保留；新增字段使用默认值。
- 删除或改类型不会静默覆盖旧实例。校验提示冲突后，可修回定义；或者明确执行 **Upgrade And Reset Conflicts**（升级并重置冲突字段），兼容字段保值，其余采用新版默认值。可 Undo。
- 改默认值不改已有实例值。切换 Marker 的 Type 也需要明确同步/升级。
- 复制 Marker 生成新的 Marker/Region GUID 和业务键；移动、改 Actor Label、保存、加载、Undo/Redo 不改变身份。业务键可以手动改为 `Outpost_North_01`。

## JSON / CSV 合同

输出目录：`Data/MapAuthoring/<完整地图包路径去掉开头斜杠>/`，例如 `Data/MapAuthoring/Game/Maps/LVL_CommanderMassPrototype/`。筛选仅影响面板，导出始终包含整张受支持地图及禁用项。

| 文件 | 关联与内容 |
|---|---|
| layout.json | schema_version=1，坐标/单位、类型/字段定义、Marker、参数、标签、所有区域及局部几何 |
| markers.csv | marker_id 主键，业务键、type_id、名称、enabled、备注、完整世界变换 |
| regions.csv | marker_id + region_id，区域信息、局部/世界变换、显式尺寸 |
| vertices.csv | marker_id + region_id + vertex_index，局部 XY 与完整世界 XYZ |
| properties.csv | Marker 字段的 GUID/键/类型/值或 XYZ；形状参数以 shape_json 逐项保留 |
| tags.csv | marker_id + tag，一项一行 |
| density_layers.json | 仅密度 Actor 存在时：独立 schema v1、格尺寸、通用图层、排序非零格、Territory 汇总和异常格 |
| density_cells.csv | 仅密度 Actor 存在时：layer、格坐标、世界格中心、density_u8 与 density_01 |
| density_territories.csv | 仅密度 Actor 存在时：territory/unassigned/overlap 的格数、权重和加权面积 |

Polygon 顶点 CSV 的世界坐标是区域局部 z=0 的轮廓；上下边界通过区域 transform 和 min_z/max_z 求得。所有尺度 cm，旋转按 Pitch/Yaw/Roll degree；Scale 固定为 [1,1,1]。枚举使用稳定项名，软引用使用完整路径。

UTF-8 无 BOM，文件记录 LF 换行，JSON 键/CSV 列与记录顺序稳定，数字保留 double 往返精度。CSV 引号、中文、逗号和换行正常转义。这些布局 CSV **不能直接交给现有 Excel→DataTable 导入器**，后续需业务 ID 和列映射。

整批文件先写同级 `.staging-<GUID>`，成功后目录切换；上一批保留在 `.previous`。失败时不发布半套文件，替换失败会尝试恢复并返回错误。Windows 同一卷目录改名是本版发布实现的前提；不要在导出时锁定输出目录。额外导出器只能生成本批次的单层文件名，不能覆盖标准文件。

正式导出要求地图有保存路径，地图、外部 Actor 和所用类型包没有未保存改动。World Partition 通过 Actor Descriptor 分别清点并用临时引用加载 Marker 与密度单例；任何缺失描述符、无法加载的标记/密度 Actor、重复密度单例或不合规的空间加载/Data Layer 设置都失败，不输出残缺布局。

没有密度 Actor 的旧地图仍严格只有原六文件，`layout.json` 继续为 schema v1 且不嵌入密度。密度 Actor 存在时，三份扩展文件和原六文件作为同一原子批次发布。据点汇总的 `density_weight_sum=Σ(density_u8/255)`，`weighted_area_m2` 是该权重乘格面积；二者都不是实际矿量。

## Python：与面板共用服务

在 UE Python 中运行，或通过项目现有 `Scripts/ue_exec.py` 发送；不需要更换 MCP 服务。

从 PowerShell 管道发送中文脚本时使用 `python -X utf8 Scripts/ue_exec.py -`，防止本机 Python 的系统代码页误解码；脚本文件入口本身按 UTF-8 读取。

```python
import json
import unreal

service = unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
types = {str(t.get_editor_property("type_id")): t for t in service.list_types()}
marker = service.create_marker(types["Outpost"], unreal.Vector(10000, 20000, 0))
identity = marker.get_editor_property("record").get_editor_property("marker_id").to_string()

patch = {
    "schema_version": 1,
    "marker_key": "Outpost_North_01",
    "display_name": "北部据点",
    "position": [10000, 20000, 0],
    "parameters": {"Tier": 2},
    "regions": [{
        "region_key": "Build",
        "shape_type": "PolygonPrism",
        "position": [0, 0, 0],
        "rotation_pitch_yaw_roll": [0, 30, 0],
        "geometry": {
            "vertices": [[0,0],[10000,0],[10000,10000],[5000,5000],[0,10000]],
            "min_z": -5000,
            "max_z": 5000
        }
    }]
}
result = service.update_marker(identity, json.dumps(patch, ensure_ascii=False))
print(result.success, [(i.field, i.message) for i in result.issues])

snapshot = service.get_snapshot()  # 可编辑未保存地图；仍要求数据合法
print(snapshot.json)

# 创建/补齐全图密度 Actor，并原子设置格值。零值会清除格子。
service.ensure_density_map(2500.0)
density_patch = {
    "schema_version": 1,
    "layer_key": "BlueOre",
    "cells": [
        {"cell_x": 10, "cell_y": -3, "density_u8": 128}
    ]
}
density_result = service.update_density_cells(json.dumps(density_patch))
print(density_result.success, density_result.issues)
print(service.get_density_snapshot().json)

# 程序化 ExportMap 不会替你保存，必须明确完成保存。
if service.save_authoring_packages():
    exported = service.export_map()
    print(exported.success, list(exported.files))
```

区域补丁提供已有 region_id 时修改它；省略该 ID 时新增区域；删除使用 `{"region_id":"已有 GUID","remove":true}`。区域数组是逐项补丁，不是替换整组。更改 geometry 时传完整形状参数。Marker ID 只读；未知键、错误类型或不存在的区域 ID 使整个更新失败，不产生部分修改。

Python 的 Guid 请用 `.to_string()`，不要用 `str(guid)`（后者是包装对象描述）。跨地图加载前释放 Python 持有的 World/Actor/地图资产引用，尤其是 duplicate_asset 返回的 World；推荐把批次代码放在函数中，避免 UE 换图 GC 被持久 Python 全局变量阻止。

接口：`ListTypes / CreateType / EnsurePresets / CreateMarker / GetSnapshot / UpdateMarker / EnsureDensityMap / GetDensitySnapshot / UpdateDensityCells / ValidateMap / ExportMap / SaveAuthoringPackages / OpenPanel / ClosePanel`。验证/导出结果包含 `Success, Issues, Json, Files`，问题含 Severity（Error/Warning）、MarkerId、RegionId、Field、Message。`UpdateDensityCells` 拒绝未知键、未知图层、重复格、非整数坐标和范围外密度，整个补丁不会部分写入。SaveAuthoringPackages 在普通编辑器走保存/签出流程，在 unattended 进程使用 UE 的无对话框保存接口；临时地图仍需先明确另存为，绝不猜测正式地图路径。

## 原生扩展

依赖：`GuLiMapAuthoringEditor → GuLiMapAuthoringCore → Core/CoreUObject/Engine`。二者均为 Editor 模块，没有 GuLiStrike 玩法依赖。RenderCore/RHI、Slate、世界操作只在 Editor。

### 新形状

以 `FGuLiMapHexPrism` 为例：

```cpp
USTRUCT()
struct FGuLiMapHexPrism : public FGuLiMapShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Shape") double Radius = 10000;
    UPROPERTY(EditAnywhere, Category="Shape") double MinZ = -5000;
    UPROPERTY(EditAnywhere, Category="Shape") double MaxZ = 5000;
};
```

在附加 Editor 模块实现 `IGuLiMapGeometryHandler`：`GetShapeType()="HexPrism"`，`GetStruct()=FGuLiMapHexPrism::StaticStruct()`；实现 Validate、ToJson、FromJson、BuildMesh。六边形可用 6 个 `(cos(iπ/3),sin(iπ/3))*Radius` 顶点构造临时 PolygonPrism，再委托内置 PolygonPrism handler 的 Validate/BuildMesh；JSON 保留 Radius/MinZ/MaxZ，不丢成边界盒。

再实现 `IGuLiMapGeometryEditor` 的 GetHandles、MoveHandle，返回区域局部位置/稳定控制柄 ID。宿主负责世界变换、绘制这些控制点、常驻 mesh 渲染、一次拖动事务和无效回滚；新形状不必重复实现这些机制。多边形类还可以实现 InsertVertex/DeleteVertex/ReverseWinding。

```cpp
// StartupModule（数据模块必须先加载）
GuLiMap::RegisterGeometry(MakeShared<FHexPrismHandler>());
GuLiMapEditor::RegisterGeometryEditor("HexPrism", MakeShared<FHexPrismEditor>());
// 类型资产 AllowedShapes 中加入 HexPrism，DefaultRegions.Geometry 即可选此结构。

// ShutdownModule：在模块代码卸载前解除所有回调
GuLiMapEditor::UnregisterGeometryEditor("HexPrism");
GuLiMap::UnregisterGeometry("HexPrism");
```

参考完整首批实现：`Private/GuLiMapGeometry.cpp` 和 `GuLiMapAuthoringEditor/Private/GuLiMapViewport.cpp`。相同 ShapeType 或相同结构重复注册会失败，应检查返回值。

### 特殊校验 / 新格式

```cpp
GuLiMap::RegisterValidator("SupplyRules",
    [](const FGuLiMapSnapshotEntry& Entry, TArray<FGuLiMapIssue>& Issues)
    {
        const auto* Type = Entry.Record.Type.LoadSynchronous();
        if (Type && Type->TypeId == "SupplyPoint" && Entry.Record.Regions.IsEmpty())
            Issues.Emplace(TEXT("SupplyPoint needs an area."), Entry.Record.MarkerId);
    });
// 对应 ShutdownModule:
GuLiMap::UnregisterValidator("SupplyRules");
```

新格式实现 `IGuLiMapExportProvider::Generate`，返回额外文件内容，再使用 RegisterExporter / UnregisterExporter。默认校验和六个标准文件始终执行；注册顺序不影响输出顺序。

## 验证命令

仅运行已授权 `GuLi.MapAuthoring` 十项测试（原五项 + 密度五项）。生命周期测试必须在独立进程加 `-GuLiMapAuthoringTestSession`，避免切换用户当前地图；临时地图和导出证据会保留在 `Content/GuLiStrike/Editor/MapAuthoring/Validation` 和 `Data/MapAuthoringValidation`。

```powershell
& 'D:\UnrealEngine-5.7\Engine\Build\BatchFiles\Build.bat' GuLiStrikeEditor Win64 Development '-Project=D:\UE5.7\test1\GuLiStrike.uproject' -WaitMutex -NoHotReloadFromIDE

& 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\UE5.7\test1\GuLiStrike.uproject' /Engine/Maps/Entry -unattended -NullRHI -nosound -nop4 -GuLiMapAuthoringTestSession '-DisablePlugins=UnrealMCP,UnrealMCPython,VibeUE,Tripo3DUEBridge' '-ini:Editor:[/Script/DataValidation.DataValidationSettings]:bValidateOnSave=False' '-ExecCmds=Automation RunTests GuLi.MapAuthoring' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=D:\UE5.7\test1\Data\MapAuthoringValidation\Latest'
```

测试进程的 ini 覆盖仅隔离现有 GuLiFlightNavigationWorldValidator 的保存时 ensure，不修改项目设置，也不替代本插件的校验。请以报告中每项 State 为准：此引擎的 TestExit 可在个别测试失败时仍返回进程退出码 0。

后续边界：Excel 接线、密度运行时加载/矿藏生成、GameMode、实际矿量与采矿规则、事件编排、样条和多边形内孔尚未实现。

可直接检查的隔离样例地图：`/Game/GuLiStrike/Editor/MapAuthoring/Validation/LVL_MapAuthoringAcceptance`（3 标记、5 区域、5 多边形顶点）；旁边的 `LVL_MapAuthoringPartitionAcceptance` 包含近处和 20km 外的标记。样例不是正式玩法布局。完整导出见项目 `Data/MapAuthoring/Game/GuLiStrike/Editor/MapAuthoring/Validation/`。
