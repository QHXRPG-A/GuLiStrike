# 数据与补丁合同

## 标准批次

输出根目录是 `Data/MapAuthoring/<MapPackage>`。每批包含：

- `layout.json`
- `markers.csv`
- `regions.csv`
- `vertices.csv`
- `properties.csv`
- `tags.csv`

`FGuLiMapSnapshot` 没有密度记录时严格只有上述六文件。存在 `AGuLiMapDensityMap` 时，同一原子批次再包含：

- `density_layers.json`
- `density_cells.csv`
- `density_territories.csv`

标准要求：UTF-8 无 BOM、LF、文件末尾换行、double 用 `%.17g` 往返精度、字段/类型/Marker/Region/Tag 固定排序。相同快照必须逐字节一致；禁止把时间戳、临时 Actor 名、指针或诊断文本放入规范数据。

## layout.json

根对象：

```json
{
  "schema_version": 1,
  "map_package": "/Game/...",
  "length_unit": "cm",
  "angle_unit": "degree",
  "coordinate_system": "Unreal left-handed Z-up; local polygon XY at z=0; implicit closure",
  "types": [],
  "markers": []
}
```

`types[]` 包含 `type_id`、`asset_path`、`display_name`、`editor_icon`、`color_rgb`、`color_alpha`、排序后的 `allowed_shapes`、`fields`、`default_regions`。

`fields[]` 包含 `field_id`、`field_key`、`field_type`、`value_type_object`、`default`；存在规则时补 `display_name`、`required`、`minimum`、`maximum`。

`markers[]` 包含 `marker_id`、`marker_key`、`type_id`、`display_name`、`enabled`、`note`、`world_transform`、`parameters`、排序后的 `tags` 和 `regions`。

`regions[]` 包含 `region_id`、`region_key`、`display_name`、`enabled`、`relative_transform`、`world_transform`、`shape_type`、`geometry`。

Transform 对象固定为：

```json
{
  "position": [0, 0, 0],
  "rotation_pitch_yaw_roll": [0, 0, 0],
  "scale": [1, 1, 1]
}
```

## CSV 列

```text
markers.csv
marker_id,marker_key,type_id,display_name,enabled,note,world_x_cm,world_y_cm,world_z_cm,pitch_deg,yaw_deg,roll_deg

regions.csv
marker_id,region_id,region_key,display_name,enabled,shape_type,local_x_cm,local_y_cm,local_z_cm,local_pitch_deg,local_yaw_deg,local_roll_deg,world_x_cm,world_y_cm,world_z_cm,world_pitch_deg,world_yaw_deg,world_roll_deg,radius_cm,half_x_cm,half_y_cm,half_z_cm,min_z_cm,max_z_cm

vertices.csv
marker_id,region_id,vertex_index,local_x_cm,local_y_cm,world_x_cm,world_y_cm,world_z_cm

properties.csv
marker_id,region_id,field_id,field_key,field_type,value,x,y,z

tags.csv
marker_id,tag
```

Marker 字段行的 `region_id` 为空。Vector 字段拆到 x/y/z，其他值放 value。区域形状的每个 geometry 键也写入 properties.csv：`region_id` 有值、`field_id` 为空、`field_type=shape_json`、value 为 canonical JSON。

PolygonPrism 的 vertices.csv 世界坐标对应区域局部 `z=0` 轮廓；上下边界要结合 Region Transform 和 min_z/max_z 计算。

## 密度扩展 schema v1

`layout.json` 不嵌入密度字段且继续保持 schema v1。`density_layers.json` 使用独立 schema v1，根字段为：

```text
schema_version, map_package, length_unit, coordinate_system,
density_map_id, cell_size_cm, territory_type_id, territory_region_key,
layers, territory_summary, anomalies
```

`layers[]` 按 LayerKey/LayerId 排序，含 `layer_id/layer_key/display_name/color_rgb/color_alpha/cells`。`cells[]` 只保存非零格，按 cell_y、cell_x 排序，每项为：

```json
{"cell_x":10,"cell_y":-3,"density_u8":128}
```

密度记录内部每层是稀疏 `32×32` row-major uint8 瓦片；零格不导出，零瓦片不允许持久存在。世界映射固定为 `cell=floor(world_xy/cell_size_cm)`，格中心为 `(cell+0.5)*cell_size_cm`；负坐标不是朝零截断。Z 不属于密度业务数据。

```text
density_cells.csv
layer_id,layer_key,cell_x,cell_y,world_center_x_cm,world_center_y_cm,density_u8,density_01

density_territories.csv
assignment_kind,marker_id,marker_key,region_id,layer_id,layer_key,cell_count,density_weight_sum,weighted_area_m2
```

每图层的 Territory 行按 MarkerKey/RegionId 排序，随后固定有 `unassigned`、`overlap` 行，即使计数为零也保留。`density_weight_sum=Σ(density_u8/255)`；`weighted_area_m2=density_weight_sum*cell_size_cm²/10000`。这些字段只表达相对权重与加权面积，不是矿量或储量。

`anomalies[]` 逐格列出 unassigned/overlap、layer_key、格坐标、density_u8 和排序后的 Territory candidate 标识。格中心严格位于多个多边形内部才算 overlap；没有严格内部命中但精确落在多个共享边界时，按 MarkerKey/RegionId 的稳定顺序唯一归属。

## 支持字段类型

| Property Bag 类型 | 导出 field_type | JSON |
|---|---|---|
| Bool | `bool` | boolean |
| Int32 | `int32` | integer number |
| Double | `double` | finite number |
| Name | `name` | string |
| String | `string` | string |
| Enum | `enum` | 稳定枚举项名 string |
| FVector Struct | `vector` | `[x,y,z]` |
| SoftObject | `soft_object` | 完整 soft path string |
| SoftClass | `soft_class` | 完整 soft path string |

容器或其他类型返回 unsupported，校验失败。Int32 补丁必须是范围内的整数；不会接受浮点截断。所有数字和 Vector 分量必须有限。

## 内置 geometry JSON

```json
{"shape_type":"Cylinder","geometry":{"radius":10000,"min_z":-5000,"max_z":5000}}
{"shape_type":"Sphere","geometry":{"radius":10000}}
{"shape_type":"Box","geometry":{"half_extents":[10000,10000,5000]}}
{"shape_type":"PolygonPrism","geometry":{"vertices":[[-10000,-10000],[10000,-10000],[10000,10000],[-10000,10000]],"min_z":-5000,"max_z":5000}}
```

FromJson 拒绝缺失、类型错误和未知 geometry 键。Cylinder/Sphere 半径和 Box 各半尺寸须大于 0.001 cm；柱体高度差须大于 0.001 cm。多边形须为 3..4096 个有限点、非退化简单轮廓。

## UpdateMarker patch schema v1

顶层允许键：

```text
schema_version, marker_id, marker_key, display_name, enabled, note,
tags, parameters, position, rotation_pitch_yaw_roll, regions
```

`schema_version` 必须是 number 1。若提供 `marker_id`，必须严格等于目标身份。position/rotation 为有限三数数组。parameters 只允许当前 Property Bag 已知字段。

Region patch 允许键：

```text
region_id, region_key, display_name, enabled, position,
rotation_pitch_yaw_roll, shape_type, geometry, remove
```

- 有 region_id：修改现有区域；一次请求不能重复 patch 同一 ID。
- 无 region_id：创建新 RegionId。
- 删除：对象只能包含 `region_id` 和 `remove:true`。
- 改 shape_type 且没给 geometry：用新形状默认 Struct。
- 给 geometry：必须是目标 handler 的完整严格对象。

解析完成后候选 Marker 和整张地图都通过 Validate 才写回。因此补丁没有“成功一半”的语义。

## UpdateDensityCells patch schema v1

`UGuLiMapAuthoringSubsystem::UpdateDensityCells` 接受完整 JSON 对象：

```json
{
  "schema_version": 1,
  "layer_key": "BlueOre",
  "cells": [
    {"cell_x": 10, "cell_y": -3, "density_u8": 128}
  ]
}
```

顶层只允许 `schema_version/layer_key/cells`，cell 对象只允许 `cell_x/cell_y/density_u8`。schema 必须为整数 1；LayerKey 必须命中现有图层；格坐标必须是 Int32 范围内整数；密度必须是 `0..255` 整数；同一补丁不得重复格坐标。`density_u8=0` 删除该格，并在瓦片全零时回收瓦片。

实现先完整解析和验证候选补丁，再用一次 Editor 事务写入；任何未知键、类型错误、重复格、未知图层或越界值都会让整个请求失败，Actor 和事务历史均不产生部分修改。

## 全图验证要点

- MarkerId 全图有效且唯一；MarkerKey 是合法 ASCII identifier 且全图唯一。
- Actor Transform 有限且 Scale=1；类型资产可加载，TypeId 不冲突。
- Marker Parameters 与类型默认 Bag 同 layout，值类型合法并满足 FieldRules。
- Tags 非空且同 Marker 内无重复。
- RegionId 全图有效且唯一；RegionKey 同 Marker 内合法且唯一。
- Region Transform 有限；shape 已注册、在类型 AllowedShapes 中且几何有效。
- DensityMapId 和各 LayerId 有效且唯一；CellSizeCm 有限且大于零；LayerKey 是唯一 ASCII identifier；瓦片坐标唯一、数据严格为 1024 字节且不存在全零瓦片。
- Territory 匹配条件随密度 Actor 保存。参与归属的区域必须启用、水平且为有效 PolygonPrism；错误形状或倾斜是 Error，未归属和严格多重内部命中是 Warning。
- `HasErrors` 只把 Error 视为阻断条件；Warning 必须保留在诊断和密度异常明细中，但不阻止确定性导出。
- 额外注册 Validator 按名称排序执行。

## 发布事务

`BuildFiles` 先在内存构建完整 `TMap<FString,FString>`。任何 Error、serializer 或 exporter 错误都不写正式目录；Warning 不阻断构建，并随结果返回给调用方。密度存在时，原六文件和三份密度扩展必须作为同一批次构建、暂存和替换。`Publish` 的 Win64 前提是 staging/final/previous 位于同一卷，通过目录改名切换：

```text
Final.staging-GUID -> Final
旧 Final -> Final.previous
```

文件名必须是单层 clean filename，禁止空名、`..` 和标准文件冲突。写 staging 失败时删除 staging；替换失败时尝试恢复旧 Final。不要依赖跨卷原子性。

## 下游边界

这些文件是完整布局交换合同，不是当前 Excel→DataTable 管线的直接输入。密度值只表示相对生成权重，插件不定义矿藏总量、储量、采矿规则或运行时生成。要接入玩法，应新建设计：稳定业务 ID 映射、列映射、运行时数据资产/加载位置、schema 兼容策略、总量归一化策略以及 GameMode/Subsystem 的消费边界；不得让 Runtime 模块引用 Editor Actor、Slate、Landscape 或 EdMode。
