# 操作与自动化指南

## 使用前确认

- 用 `D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor.exe` 打开 `D:\UE5.7\test1\GuLiStrike.uproject`。
- 插件启用项在 `GuLiStrike.uproject`，面板入口为“工具 → GuLi → 地图标注与资源涂绘”。新编译插件后必须重启旧 Editor。
- 工具始终编辑当前地图。正式导出前必须为地图指定包路径，并保存地图、外部 Actor 包和所用类型资产。
- 所有空间量为 cm（100 cm = 1 m），角度为 degree；Actor 和区域 Scale 均固定为 1。

## 面板工作流

1. 首次使用点“生成三个预设”。它只补齐缺失的 Resource、Outpost、Generic，不覆盖现有资产。默认目录是 `/Game/GuLiStrike/Editor/MapAuthoring/Types`。
2. 选择类型。点“视口放置”后在场景单击：勾选“表面优先”时先做 Visibility 碰撞落点，未命中才落到工作平面；取消勾选则始终使用指定 Z 平面。Alt 保留相机操作，Esc 或“退出放置”结束。
3. 需要精确起点时，用“世界原点”或“所选 Actor 原点”创建。新 Marker 进入当前世界的持久关卡和 `GuLi/MapAuthoring` Outliner 文件夹。
4. 列表与视口双向同步。双击列表或按 F 聚焦；搜索可按业务键、名称和类型过滤。筛选不会改变数据，也不会缩小导出范围。
5. Details 编辑 MarkerKey、DisplayName、Type、Enabled、Tags、Note、Parameters 和 Regions。业务键建议使用可读稳定名，如 `Outpost_North_01`。
6. 一个 Marker 可有零到多个区域，例如 Capture、Build、Influence。各区域有独立 RegionKey、启用状态、相对平移/旋转和形状。
7. 先点“校验”处理全部问题，再用“保存并导出”。取消 UE 保存/签出流程即停止导出；“保存”只保存，不生成数据。

## 资源密度涂绘

1. 切到“资源涂绘”，点击“创建/补齐密度图”。每张地图只允许一个 `AGuLiMapDensityMap`；按钮补齐 `BlueOre/蓝矿` 和 `RedOre/红矿`，不覆盖现有数据。
2. 选择图层并单独控制显隐。显隐只影响 SceneProxy，不影响保存、校验或导出；红蓝可在同一格重叠。
3. 设置加深/擦除、半径、强度、软边。默认 200m、25%、50%；LMB 绘制，Alt 操作相机，Esc 有活动笔画时恢复整笔前值，再按一次退出模式。
4. 一次拖拽是一次事务。路径按固定世界距离重采样，每格只按该笔最大覆盖结算一次；不要改成逐鼠标事件累加。
5. 默认 CellSize 为 2500cm。非空密度图不能直接修改；面板“应用格尺寸”只有在用户确认后才清空所有层并修改，不做隐式重采样。“清空当前层”只清选中层，均可 Undo。

密度 Actor 固定 Identity Transform，位于 PersistentLevel、非空间加载且无 Data Layer/附着/碰撞/Tick，不进 PIE。格坐标是 `floor(WorldXY/CellSizeCm)`；Z 仅用于 Landscape/Visibility/工作平面的贴地预览。u8 密度是相对权重，不是矿量、储量或运行时生成数量。

新建 Outpost 预设有 `Capture` 和 `Territory`；旧类型/Marker 不自动迁移。有效 Territory 是启用、水平的 `PolygonPrism`。缺失、未归属或严格重叠显示 Warning 但不阻断导出；错误形状和倾斜是 Error。精确共享边界按 MarkerKey/RegionId 稳定归属。

## 列表操作与事务

- 复制/粘贴/原生复制会生成新的 MarkerId、所有 RegionId 和默认业务键。
- 删除、启用、禁用、复制、增加区域等批量操作按一次面板动作形成事务；一次视口拖动也只形成一个事务。
- Undo/Redo 必须保留原对象的身份。若身份因移动、保存或 Undo 改变，视为实现缺陷。
- 删除后选择缓存、Details、组件路径和 HitProxy 可能短暂失效；UI 应忽略过期对象而不是继续访问。

## 区域形状

| ShapeType | 数据 | 视口操作 |
|---|---|---|
| `Cylinder` | `Radius`, `MinZ`, `MaxZ` | 半径、上下边界控制柄 |
| `Sphere` | `Radius` | 半径控制柄 |
| `Box` | `HalfExtents` | 三轴半尺寸；完整尺寸为 2 倍 |
| `PolygonPrism` | `Vertices`, `MinZ`, `MaxZ` | 白点拖顶点；绿点选边后 Insert 插中点；Delete 删顶点；Ctrl+R 反转；上下高度 |

区域中心控制柄用于相对移动/旋转；Actor 原生 gizmo 移动整组。多边形允许凹形但不允许自交、非相邻边接触、重叠边、零长度边、近零面积或内孔；顶点数 3..4096，闭合边隐含，不重复首顶点。

拖动中无效轮廓标红且不填充；松开仍无效时恢复拖动前完整记录并取消事务。Details 可暂存无效输入，但校验和导出必须失败。

## 类型和字段

普通新类型不需要 C++：在面板输入合法 TypeId（ASCII 标识符），创建 `UGuLiMapTypeDefinition`，再配置 DisplayName、Color、AllowedShapes、DefaultRegions、DefaultParameters 和 FieldRules。

首版字段类型：`bool`、`int32`、`double`、`name`、`string`、枚举、`FVector`、软对象引用、软类引用。容器、Text、float、硬 UObject 引用不在合同内。

- 字段 GUID 是迁移身份，字段名是导出键。改名应保留同一 GUID，不要“删除后新建”模拟改名。
- 新增/改名后，对已有 Marker 执行 `Synchronize Type`；同 GUID 同类型值保留，新字段采用默认值。
- 删除/改类型产生 schema conflict。修回定义，或明确执行 `Upgrade And Reset Conflicts`；后者保留兼容字段，对冲突使用新默认值，并可 Undo。
- 修改默认值只影响新实例/新增字段，不自动覆盖已有实例。
- FieldRules 通过 FieldId 关联字段，可定义中文标签、必填和数字上下界。

## 校验和导出

默认输出目录：

```text
Data/MapAuthoring/<地图完整包路径去掉开头斜杠>/
```

例如 `/Game/Maps/LVL_CommanderMassPrototype` 输出到 `Data/MapAuthoring/Game/Maps/LVL_CommanderMassPrototype/`。无密度 Actor 时标准文件为 `layout.json`、`markers.csv`、`regions.csv`、`vertices.csv`、`properties.csv`、`tags.csv`；有密度 Actor 时同批增加 `density_layers.json`、`density_cells.csv`、`density_territories.csv`。

导出包含整张受支持地图中的所有 Marker，包括禁用项；不受面板筛选、视口显隐或选择影响。当前批次先写 `.staging-<GUID>`，成功后整目录切换，上一批位于 `.previous`。输出目录被占用时先关闭相关表格/预览程序再重试，不要手工拼接半批文件。

World Partition Marker 与密度 Actor 必须位于主容器、非空间加载且不在 Data Layer。工具分别用 Actor Descriptor 清点并按需加载；任何描述符无法解析或 Actor 集合不完整都应中止导出。

## Python 服务

面板与 Python 共用 `UGuLiMapAuthoringSubsystem`：

```python
import json
import unreal

service = unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
types = {str(t.get_editor_property("type_id")): t for t in service.list_types()}
marker = service.create_marker(types["Outpost"], unreal.Vector(10000, 20000, 0))
marker_id = marker.get_editor_property("record").get_editor_property("marker_id").to_string()

patch = {
    "schema_version": 1,
    "marker_key": "Outpost_North_01",
    "display_name": "北部据点",
    "position": [10000, 20000, 0],
    "parameters": {"Tier": 2},
    "regions": [{
        "region_key": "Build",
        "shape_type": "PolygonPrism",
        "geometry": {
            "vertices": [[0, 0], [10000, 0], [10000, 10000], [5000, 5000], [0, 10000]],
            "min_z": -5000,
            "max_z": 5000
        }
    }]
}
result = service.update_marker(marker_id, json.dumps(patch, ensure_ascii=False))
print(result.success, [(issue.field, issue.message) for issue in result.issues])

snapshot = service.get_snapshot()
service.ensure_density_map(2500.0)
density = service.update_density_cells(json.dumps({
    "schema_version": 1,
    "layer_key": "BlueOre",
    "cells": [{"cell_x": 10, "cell_y": -3, "density_u8": 128}]
}))
print(density.success, service.get_density_snapshot().json)
if snapshot.success and service.save_authoring_packages():
    exported = service.export_map()
    print(exported.success, list(exported.files))
```

接口：`ListTypes`、`CreateType`、`EnsurePresets`、`CreateMarker`、`GetSnapshot`、`UpdateMarker`、`EnsureDensityMap`、`GetDensitySnapshot`、`UpdateDensityCells`、`ValidateMap`、`ExportMap`、`SaveAuthoringPackages`、`OpenPanel`、`ClosePanel`。

补丁规则：提供 `region_id` 修改既有区域；省略时新增；删除只能使用 `{"region_id":"...","remove":true}`。区域数组是逐项补丁，不是整组替换。改变 shape 时完整提供对应 geometry。MarkerId 不可改；任一未知键/错误类型/无效区域使整个更新失败。

密度补丁 v1 顶层只允许 `schema_version/layer_key/cells`，格对象只允许 `cell_x/cell_y/density_u8`。坐标必须是 int32 整数，密度是 0..255 整数，同一补丁不得重复格；零值清格。任一成员失败时真实 Actor 完全不改。

从 PowerShell 管道发送中文脚本时用 `python -X utf8 Scripts/ue_exec.py -`。Guid 用 `.to_string()`，不要用 `str(guid)`。换图前释放 Python 持有的 World/Actor/地图资产引用；把批次代码放入函数局部作用域，避免持久全局变量阻止 World GC。

## 常见问题

- 看不到新修复：保存工作并重启源码版 Editor，核对实际进程路径。
- `ExportMap` 失败且提示保存：先另存临时地图，再保存地图、外部 Actor 和相关类型资产；程序化导出不会猜正式路径。
- 新字段校验冲突：先判断是同 GUID 改名/新增，还是删除/改类型；不要直接重置用户值。
- 区域看似小/大 100 倍：确认单位 cm；Box 填的是半尺寸。
- CSV 不能直接导入现有 DataTable：当前产物是布局交换数据，仍需玩法 ID 和列映射。
- 改格尺寸提示已有数据：这是防止隐式重采样的门禁；使用面板带确认的清空全部流程，或先清空两个图层再调用 `EnsureDensityMap(new_size)`。
- 有黄色问题但导出成功：Warning 用于 Territory 缺失、未归属或重叠统计；红色 Error 才阻断。不要把密度汇总解释成实际矿量。
