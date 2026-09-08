# 实现与扩展架构

## 模块和边界

```text
GuLiMapAuthoringEditor (Editor)
  -> GuLiMapAuthoringCore (Editor)
       -> Core / CoreUObject / Engine
```

Core 的 Private 依赖为 `Json`、`GeometryCore`。Editor Public 依赖 Core/Engine/EditorSubsystem，Private 依赖 UnrealEd、Slate、PropertyEditor、StructUtilsEditor、AssetRegistry/AssetTools、RenderCore/RHI 等。两个模块都在 Default phase 加载，插件 `CanContainContent=true`，版本基线 `0.1.0`。

插件不依赖 `Source/GuLiStrike`。本期没有 Runtime 模块，游戏代码不应引用 Marker 或类型资产。

## 数据模型和所有权

- `UGuLiMapTypeDefinition`：Editor-only DataAsset，保存 TypeId、显示信息、AllowedShapes、DefaultRegions、`FInstancedPropertyBag` 默认字段和 FieldRules。
- `AGuLiMapMarker`：Editor-only Actor，持有 `FGuLiMapMarkerRecord`，根组件为 `UGuLiMapVisualizationComponent`。构造时关闭 Tick、碰撞、空间加载；不进 PIE。
- `FGuLiMapMarkerRecord`：MarkerId、MarkerKey、类型软引用、显示名、Enabled、Tags、Note、Parameters、Regions。
- `FGuLiMapRegionRecord`：RegionId/RegionKey、显示名、Enabled、相对 Translation/Rotation、`FInstancedStruct Geometry`。
- `FGuLiMapSnapshotEntry`：纯值拷贝的 Record + Actor 世界变换；快照边界不携带 Actor 指针。
- `FGuLiMapResult`：Blueprint/Python 返回值，含 Success、Issues、Json、Files。

Actor 世界 Transform 与 Region 相对 Transform 分层组合：`Region.GetTransform() * ActorTransform`。Scale 永远为 1。

## 源码索引

| 路径 | 责任 |
|---|---|
| `Source/GuLiMapAuthoringCore/Public/GuLiMapTypes.h` | 反射数据结构、四种内置形状、类型资产、结果/快照类型 |
| `Source/GuLiMapAuthoringCore/Public/GuLiMapAuthoring.h` | 几何/校验/导出注册接口、字段迁移、验证、构建文件和发布 API |
| `Source/GuLiMapAuthoringCore/Private/GuLiMapFields.cpp` | 字段类型白名单、JSON 值转换、GUID 迁移、类型规则校验 |
| `Source/GuLiMapAuthoringCore/Private/GuLiMapGeometry.cpp` | 四形状验证、JSON、网格构建、GeometryCore 凹多边形三角化 |
| `Source/GuLiMapAuthoringCore/Private/GuLiMapAuthoring.cpp` | 注册表、全图验证、canonical JSON/CSV、确定性排序、原子目录发布 |
| `Source/GuLiMapAuthoringEditor/Public/GuLiMapAuthoringSubsystem.h` | 面板/Python 共用的公开 Editor Subsystem API |
| `Source/GuLiMapAuthoringEditor/Private/GuLiMapAuthoringSubsystem.cpp` | 当前世界、AssetRegistry 类型目录、Marker 创建、WP 快照、保存、补丁事务 |
| `Source/GuLiMapAuthoringEditor/Public/GuLiMapMarker.h` 与 Private cpp | Marker 生命周期、身份复制规则、字段同步、Undo 后刷新 |
| `Source/GuLiMapAuthoringEditor/Private/SGuLiMapPanel.*` | Slate 面板、Details、筛选、选择同步、操作入口、委托清理 |
| `Source/GuLiMapAuthoringEditor/Private/GuLiMapViewport.*` | 放置 EdMode、ComponentVisualizer、形状控制柄和拖动事务 |
| `Source/GuLiMapAuthoringEditor/Private/GuLiMapVisualization.cpp` | Editor-only PrimitiveSceneProxy，常驻轮廓和半透明填充 |
| `Source/GuLiMapAuthoringEditor/Private/GuLiMapAuthoringEditorModule.cpp` | 菜单、NomadTab、视口注册、换图/PIE 生命周期 |
| `Source/GuLiMapAuthoringEditor/Private/Tests/GuLiMapAuthoringTests.cpp` | 当前唯一授权的插件测试文件 |

所有路径均相对 `Plugins/GuLiMapAuthoring/`。

## 启动与 UI 生命周期

Core `StartupModule` 注册内置几何，Shutdown 清空 exporter/validator/geometry 注册表。Editor 等 `OnPostEngineInit` 后：

1. 注册四种几何编辑器和 `UGuLiMapVisualizationComponent` 的可视化器。
2. 注册放置 EdMode。
3. 注册 `GuLiMapAuthoring` NomadTab 和“工具 → GuLi”菜单。
4. 在换图与 `PreBeginPIE` 调用 `EndInteraction`。

Panel 用 `AddSP` 绑定 Selection、PropertyChanged、MapChange 和 AssetRegistry 事件，析构时解除并注销 Undo client。刷新通过 active timer 合并请求；同步选择期间用守卫避免递归。

组件/Actor 生命周期是高风险边界：UE 的选择缓存和 HitProxy 可能比删除/GC 后的组件多活一帧。`DrawVisualization` 和 `VisProxyHandleClick` 必须先 `IsValid(Component)`，再检查 Owner；其他回调同样不能默认弱引用仍有效。2026-09-07 的删除崩溃正是此处空组件解引用，修复记录在 `Progress/Archive/20260907-地图标记删除崩溃空指针修复.md`。

## 创建、身份与字段迁移

`CreateMarker` 先校验类型，再开启事务、在 PersistentLevel Spawn，复制类型默认参数/区域，生成身份，设置 Actor Label/Folder 并刷新可视化。

`RegenerateIdentity` 生成 MarkerId、以 `TypeId_GUIDDigits` 为初始 MarkerKey，并重建全部 RegionId。调用点：首次创建缺失身份、正常 Duplicate、Paste/PostEditImport。`EnsureRegionIdentities` 修复无效/重复 RegionId，并为新增区域补 key/默认 Cylinder；不得在普通移动、保存或 Undo 中调用整组身份重建。

`MigrateFields` 以 PropertyDesc.ID 找新字段：

- GUID 相同且 ValueType/ValueTypeObject/ContainerTypes 相同：把旧值按严格类型复制到新默认结构。
- 字段缺失或类型不兼容且 `ExplicitReset=false`：返回 false，原 Bag 完全不变。
- `ExplicitReset=true`：兼容字段保值，其余采用新默认；不做宽松数值类型转换。

## 补丁更新链路

`UpdateMarker` 的原子顺序：

1. 解析 MarkerId 并在当前已加载 Marker 中要求唯一匹配。
2. `ParsePatch` 在 Record/Transform 副本上验证 schema、允许键、JSON 类型、身份、字段、区域和几何。
3. 构造当前整图快照，用候选记录替换目标后跑全图 Validate。
4. 无问题才创建单个 `FScopedTransaction`，Modify Actor，写回 Record/Transform，并发出 PropertyChanged。

任何失败都在事务和真实 Actor 写入前返回。扩展 patch 格式时仍须保留这个顺序，并提升 schema_version 处理非兼容变化。

## 快照与 World Partition

`CollectSnapshot` 拒绝 PIE/SIE，记录当前地图包路径。正式导出时还检查临时/不存在包、地图/外部包/类型包 dirty。

WP 路径通过所有 ActorDescContainer 清点 `AGuLiMapMarker` 描述符，以 `FWorldPartitionReference` 在收集期间持有并加载 Actor，然后核对：

- 描述符和实际 ActorGuid 集合完整对应。
- Actor 属于当前 World、PersistentLevel 和主容器，不在 Level Instance/嵌套容器。
- Marker 非空间加载且 DataLayers 为空。
- 已保存 Marker 必须存在 Actor Descriptor。

无法取得完整集合时添加 Issue 并失败，不能退化为 `LoadedMarkers()` 的可见子集。

## 渲染和编辑事务

`UGuLiMapVisualizationComponent` 的 SceneProxy 在游戏线程构造区域网格快照，渲染线程只读。无效形状保留红色轮廓但没有填充；数据变化调用 UpdateBounds + MarkRenderStateDirty。

SceneProxy 负责全部 Marker 的常驻轮廓/填充；`FComponentVisualizer` 只为所选组件绘制控制柄。DebugMeshMaterial 的真实 MaterialRelevance 决定填充 pass；编辑器辅助显示受 Game view 门控。

Visualizer 在拖动开始时保存完整 Record 并创建事务，所有 delta 作用在局部几何/区域 Transform。停止时验证，失败恢复完整 Record 并 Cancel；`EndEditing`、换图、关面板、PIE 和 Esc 都必须清理未提交事务。

## 扩展方式

### 只新增普通类型/字段

首选 DataAsset 配置，不改 C++。新增字段必须保留稳定 GUID；为已有 Marker 提供显式同步/升级流程。

### 新增几何

1. 在合适的 Editor 数据模块定义继承 `FGuLiMapShape` 的 `USTRUCT`。
2. 实现 `IGuLiMapGeometryHandler`：稳定 ShapeType、Struct、严格 Validate、完整 ToJson/FromJson、局部空间 BuildMesh。
3. 实现 `IGuLiMapGeometryEditor`：稳定控制柄 ID、GetHandles/MoveHandle；多边形类按需实现插点、删点、反向。
4. Startup 注册 data handler 后注册 editor handler；Shutdown 按相反层次注销。
5. 类型资产把 ShapeType 加入 AllowedShapes，并以对应 Struct 创建默认区域。

同一 ShapeType 或同一 UScriptStruct 重复注册会失败。形状 JSON 必须保存全部原始参数；CSV 通过 `properties.csv` 的 `shape_json` 行保留扩展参数，不能只导出包围盒。

### 特殊校验和额外格式

用唯一名称 `RegisterValidator/UnregisterValidator` 增加针对 SnapshotEntry 的规则。实现 `IGuLiMapExportProvider::Generate` 后用 `RegisterExporter/UnregisterExporter` 生成附加文件。

默认全图验证与六个标准文件永远执行。附加导出文件名必须是安全的单层文件名，不得包含 `..` 或覆盖标准文件；所有注册必须在模块卸载前注销。

