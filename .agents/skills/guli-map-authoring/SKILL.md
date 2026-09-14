---
name: guli-map-authoring
description: Operate, diagnose, extend, and maintain the GuLiMapAuthoring UE5.7 editor plugin in the GuLiStrike project. Use for the map-marker panel, red/blue ore density painting, Territory statistics, type DataAssets and Property Bag fields, named regions and shapes, JSON/CSV layout export, Python authoring API, plugin crashes, source changes, validation, or release handoff. Do not use for runtime GameMode/mining rules, Excel/DataTable mapping, or ordinary environment art that does not involve this plugin.
metadata:
  short-description: GuLi 地图标注插件使用与维护
---

# GuLiMapAuthoring

负责 `D:\UE5.7\test1\Plugins\GuLiMapAuthoring` 的使用、排障、扩展和交付维护。工作区根目录固定为 `D:\UE5.7\test1`，源码版引擎固定为 `D:\UnrealEngine-5.7`。

## 先选择工作模式

- 面板放置、类型/字段配置、区域编辑或导出：读 [references/operations.md](references/operations.md)。
- Python 批量标注、补丁更新或下游消费数据：读 operations，再读 [references/data-contract.md](references/data-contract.md)。
- 修改 C++、模块边界、形状/校验/导出扩展：读 [references/architecture.md](references/architecture.md)；涉及格式时再读 data-contract。
- 崩溃、行为异常、构建、测试、验收或发布：读 [references/maintenance.md](references/maintenance.md)，再按故障域补读 architecture 或 operations。

只读当前任务需要的参考，不要默认一次加载全部文件。

## 事实源优先级

1. 当前 `.uplugin`、`Build.cs`、Public 头文件和 Private 实现是行为/API 的最高事实源。
2. `Plugins/GuLiMapAuthoring/README.md` 是用户操作手册。
3. `Progress/DevelopmentDocumentation/20260906-地图战略点标注与数据导出工具.md` 是设计合同和当前验收状态。
4. `Progress/Archive/20260907-地图标注插件首版实现与验证.md` 及后续同主题归档保存历史证据。
5. 本 Skill 是导航与维护知识；若与源码冲突，先依据源码和实际证据纠正 Skill，不要让旧说明反向约束真实实现。

## 不可破坏的合同

- 插件是项目级、Editor-only 的地图数据生产工具，不是 GameMode、运行时玩法模块或独立 exe。无密度 Actor 时标准产物是 `layout.json` 加五张 CSV；有密度 Actor 时同一原子批次再增加三份独立 schema v1 密度文件。
- `GuLiMapAuthoringEditor -> GuLiMapAuthoringCore` 单向依赖；插件不得反向依赖 `GuLiStrike` 玩法模块。Slate、世界操作和渲染依赖留在 Editor 模块。
- Marker 只属于当前编辑世界的持久关卡，保持非空间加载、无 Data Layer、无附着、Scale=1；不进入 PIE、碰撞、导航或 Tick。
- `AGuLiMapDensityMap` 每图唯一、世界 Transform 固定 Identity，同样只在 PersistentLevel、非空间加载、无 Data Layer/附着/碰撞/导航/Tick 且不进 PIE。格坐标按 `floor(WorldXY/CellSizeCm)`，非空数据禁止直接改分辨率。
- MarkerId/RegionId 是稳定身份。新建、复制、粘贴产生新身份；移动、保存、加载、Undo/Redo 不得重生成身份。MarkerKey 全地图唯一，RegionKey 在 Marker 内唯一。
- Property Bag 按字段 GUID 迁移。新增和同 GUID 改名保值；删除或改类型必须明确报冲突，只有显式升级才可重置不兼容字段。
- `UpdateMarker` 必须先在副本解析并校验全部输入，再校验整图，最后作为一个事务应用。未知键、错误类型、无效身份或无效几何不得产生部分修改。
- `UpdateDensityCells` 同样必须在副本完整解析并校验后一次事务写回；未知键/图层、重复格、非整数坐标或越界密度使整个补丁失败。密度 Warning 不阻断导出，Error 必须阻断。
- 导出必须从完整快照生成，保持 UTF-8 无 BOM、LF、固定排序、double 精度和字节确定性。发布先 staging，再整批替换并保留 `.previous`；失败不得发布混合批次。
- 面板、可视化器和 HitProxy 必须把 Actor/Component 当作可随删除、GC、换图、关面板或 PIE 失效的对象。进入回调先检查有效性，结束交互时清理事务、委托、模式和弱引用。

## 工作纪律

- 回答使用问题前，先确认用户是否使用源码版 Editor，以及当前地图/面板/选中类型；不要把未保存地图误当成可正式导出。
- 排障时保存失败证据，定位第一个错误转换。不要为了复现而删除或改写用户正式地图；优先用隔离验证地图或新建临时地图。
- 修改只落在当前需求所需文件。工作树可能很脏，保留所有无关改动；不得读取 UE 二进制资产内容或扫描项目禁止目录。
- 新增或扩充插件测试前，必须向用户说明具体需求、测试内容和文件并取得明确许可。当前已授权文件包括原 `GuLiMapAuthoringTests.cpp`，以及覆盖已批准密度方案的 `GuLiMapDensityPaintTests.cpp`；运行现有已授权测试无需再次询问。
- C++、`Build.cs` 或原生插件变化后，必须用源码版引擎构建 `GuLiStrikeEditor Win64 Development`，再核对引擎、项目和插件 `UnrealEditor.modules` 的 BuildId。旧 Editor 进程不会自动装载新 DLL，应提示用户保存并重启。
- 行为、接口或维护事实变化后，同步用户 README、相关开发文档和当天归档；未取得实际证据的人工验收保持“未验证”。

## 当前功能边界

已实现：Resource/Outpost/Generic 预设、配置式新类型和字段、稳定身份、多命名区域、Cylinder/Sphere/Box/PolygonPrism、凹多边形、常驻轮廓/填充、视口控制柄；通用稀疏密度图、BlueOre/RedOre 双层笔刷、Landscape 贴地热图、Territory 归属/警告/汇总；完整 Undo/Redo、统一 Editor Subsystem/Python API、六个基础文件与三个可选密度文件的确定性生成、World Partition 描述符清点和原子发布。

尚不属于本插件：Excel 接线、密度运行时读取/矿藏生成、实际矿量或储量、GameMode/占领/产出/采矿规则、事件编排、样条路径、多边形内孔。需要这些能力时先明确新需求和模块边界，不能悄悄塞入 Editor 插件。
