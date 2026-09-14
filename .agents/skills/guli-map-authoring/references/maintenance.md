# 维护、排障与交付

## 安全起点

先记录：源码版引擎路径、项目路径、当前地图包、是否 PIE/SIE、面板状态、选中 MarkerKey/MarkerId、复现输入、预期和实际结果。查看限定路径的 `git status`，保留无关用户改动。

遵守项目搜索禁令：源码只查 `Source/` 与 `Plugins/**/Source/`，配置只查 `Config/`；不得遍历 `Intermediate/`、`Saved/`、`DerivedDataCache/`、`Binaries/`、`Content/Assets/`、`Downloads/`，也不得读取 `.uasset/.umap/.uexp/.ubulk/.pak` 内容。已知日志/CrashContext 可以用精确路径读取。

不要在 `/Game/Maps/LVL_CommanderMassPrototype` 等用户正式地图里做破坏性复现。优先使用：

- `/Game/GuLiStrike/Editor/MapAuthoring/Validation/LVL_MapAuthoringAcceptance`
- `/Game/GuLiStrike/Editor/MapAuthoring/Validation/LVL_MapAuthoringPartitionAcceptance`
- `FAutomationEditorCommonUtils::CreateNewMap()` 创建的独立测试世界

验证样例不是正式玩法布局；若要修改/删除样例，也先确认不会破坏既有证据。

## 崩溃排查

1. 复制原始日志和 CrashContext 到新的 `Data/MapAuthoringValidation/<Issue-Date>/`，再改代码，保留失败证据。
2. 用日志时间线确认最后一个成功阶段。CrashContext 的 `PCallStack` 可用构建对应 DLL/PDB 和 `llvm-symbolizer --relative-address` 解析；必须在重编覆盖 PDB 前完成。
3. 排查顺序通常为：Panel action → Editor selection/transaction → Actor delete/update → GC → viewport/component visualizer → scene proxy/render thread。
4. 区分根因与后续噪声。只修有证据的第一个坏转换，不顺手重构 Details、导出或玩法模块。
5. 复跑同一隔离场景，再做相邻路径验证：选择/聚焦、Undo/Redo、关面板、换图、PIE、保存/导出。

### 2026-09-07 删除崩溃基线

表现：面板选中 Generic Marker 后点“删除”，Actor 删除日志已经成功，随后 GameThread 访问违规 `0xD8`。

符号化根因：`FGuLiMapVisualizer::DrawVisualization` 由 `UUnrealEdEngine::DrawComponentVisualizers` 调用时收到 nullptr component，旧代码直接 `C->GetOwner()`。修复是在绘制入口检查 Component/Owner，并在 `VisProxyHandleClick` 安全解析弱组件和过期 HitProxy。

证据：`Data/MapAuthoringValidation/DeleteCrash-20260907/` 和 `Progress/Archive/20260907-地图标记删除崩溃空指针修复.md`。若同症状复现，先确认运行 DLL 是修复后构建，不要默认又是相同根因；重新取 CrashGUID 和栈。

## 修改原则

- 数据模型/格式变化：先判断兼容性；非兼容变更提升 schema_version 并给迁移/消费方案。
- Panel/Slate：委托成对注册/解除；异步刷新不捕获裸生命周期超出 Widget 的 `this`；Details/选择同步避免递归。
- Actor/Visualizer：删除、GC、换图、PIE 均可能使弱对象失效；回调入口做有效性检查。
- 事务：一次用户动作一个 `FScopedTransaction`，先 Modify 再改；失败路径 Cancel/恢复完整前值；Undo/Redo 不改变身份。
- 渲染：SceneProxy 构造阶段复制数据，渲染线程不访问 Actor/PropertyBag；无效/NaN 数据不能进入 renderer。
- WP：完整性错误必须失败，不能用已加载列表假装全图。
- 导出：先 BuildFiles 全成功，再 Publish；新 exporter 不得覆盖标准文件或创建子路径。

## 源码版构建门禁

任何 C++、Build.cs、Target.cs 或原生插件修改后执行：

```powershell
& 'D:\UnrealEngine-5.7\Engine\Build\BatchFiles\Build.bat' GuLiStrikeEditor Win64 Development '-Project=D:\UE5.7\test1\GuLiStrike.uproject' -WaitMutex -NoHotReloadFromIDE
```

只有修改引擎源码时才需要额外重编引擎目标；本插件普通变更不应无故构建整个 UnrealEditor。最后一次原生构建必须来自 `D:\UnrealEngine-5.7`，不是 Launcher 版。

构建成功后直接读取这三个精确清单，不搜索 Binaries：

```text
D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor.modules
D:\UE5.7\test1\Binaries\Win64\UnrealEditor.modules
D:\UE5.7\test1\Plugins\GuLiMapAuthoring\Binaries\Win64\UnrealEditor.modules
```

三个 BuildId 必须完全一致。记录命令、退出码、BuildId 和构建日志路径。原生构建后提醒用户保存并重启仍在运行的旧 Editor；不要擅自关闭用户会话。

## 当前已授权自动化

基础测试位于 `Plugins/GuLiMapAuthoring/Source/GuLiMapAuthoringEditor/Private/Tests/GuLiMapAuthoringTests.cpp`：

- `GuLi.MapAuthoring.FieldsMigration`
- `GuLi.MapAuthoring.GeometryAndTransforms`
- `GuLi.MapAuthoring.ExportDeterminismAndAtomicity`
- `GuLi.MapAuthoring.AtomicPatch`
- `GuLi.MapAuthoring.EditorIdentityUndoSaveReload`

资源密度测试位于用户已明确授权的新文件 `Plugins/GuLiMapAuthoring/Source/GuLiMapAuthoringEditor/Private/Tests/GuLiMapDensityPaintTests.cpp`：

- `GuLi.MapAuthoring.Density.GridBrushAndSparseTiles`
- `GuLi.MapAuthoring.Density.TerritoryAssignmentAndSeverity`
- `GuLi.MapAuthoring.Density.AtomicPatch`
- `GuLi.MapAuthoring.Density.ExtensionDeterminismAndLegacy`
- `GuLi.MapAuthoring.Density.EditorSingletonUndoWorldPartitionSaveReload`

生命周期测试会切换地图，只能在独立进程使用 `-GuLiMapAuthoringTestSession`：

```powershell
& 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\UE5.7\test1\GuLiStrike.uproject' /Engine/Maps/Entry -unattended -NullRHI -nosound -nop4 -nosplash -GuLiMapAuthoringTestSession '-DisablePlugins=UnrealMCP,UnrealMCPython,VibeUE,Tripo3DUEBridge' '-ini:Editor:[/Script/DataValidation.DataValidationSettings]:bValidateOnSave=False' '-ExecCmds=Automation RunTests GuLi.MapAuthoring' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=D:\UE5.7\test1\Data\MapAuthoringValidation\Latest' '-abslog=D:\UE5.7\test1\Data\MapAuthoringValidation\Latest.log'
```

ini 覆盖只隔离相邻 GuLiFlightNavigation 保存验证器的 ensure，不修改项目设置，也不关闭本插件校验。不能只看进程退出码：读取报告 `index.json` 的每项 State、errors、warnings；此引擎 TestExit 可能在个别测试失败时仍返回 0。

新增或扩充测试前必须取得用户明确许可，并说明对应需求、拟测试内容和文件。许可只覆盖说明的范围。

## 人工验收矩阵

NullRHI 自动化不证明 Slate/视口交互。相关修改至少在隔离 Editor 检查：

- 面板打开、关闭、再次打开；地图切换和 PIE 前后状态清理。
- 列表↔视口选择、Details、双击/F 聚焦；相机输入不被放置/控制柄吞掉。
- 表面和工作平面放置，Esc/Alt。
- Cylinder/Sphere/Box/PolygonPrism 控制柄、区域移动/旋转、Polygon 插点/删点/高度/反转。
- 临时无效轮廓标红、松手回滚、一次拖动一次 Undo/Redo。
- Marker 删除后不崩溃，列表/Details/选择刷新，Undo 恢复同一身份，Redo 再删除。
- 新配置类型和字段同步/显式升级，保存重开，完整导出。
- WP 远处真正未加载 Marker 的恢复加载，以及无法完整收集时明确失败。
- 资源涂绘页切换、红蓝层显隐和叠色；加深/擦除、半径/强度/Falloff 的笔刷手感。
- 笔画中的固定距离补点、一次拖拽一次 Undo/Redo；第一次 Esc 回滚当前笔画，第二次退出模式；Alt 相机操作不落笔。
- Landscape 贴地热图、Visibility 命中回退和工作平面回退；地图/面板/PIE/删除/GC 时清理未完成事务。
- 非空密度图修改格尺寸时阻止隐式重采样；“清空全部并改分辨率”和“清空当前层”的确认路径。
- Territory 未归属/重叠 Warning、错误形状/倾斜 Error、据点统计与三份扩展文件；8 km 双层压力场景。

截至 2026-09-10，资源密度功能的源码构建和十项 `GuLi.MapAuthoring` 自动化已有证据；完整鼠标交互、贴地/叠色视觉检查、8 km 双层压力场景、删除修复后的按钮实测和真正未加载 WP 恢复仍需以当前开发文档为准，不能宣称通过。

## 交付与文档

行为或接口变化时更新：

1. `Plugins/GuLiMapAuthoring/README.md` 的用户操作和扩展示例。
2. 对应工作项的 Progress Requirement/DevelopmentDocumentation；资源密度扩展使用 `REQ/DEV-20260910-005`。
3. 当天 `Progress/Archive/YYYYMMDD-....md`，记录真实文件、原因、验证命令/结果和遗留项。
4. `Progress/README.md` 归档索引顶部一行。
5. 本 Skill 中受影响的参考，避免维护知识漂移。

纯 Skill/Markdown 变更不触发 UE 原生构建门禁；用 skill-creator 的 `quick_validate.py` 验证 Skill 结构。若同一任务还改了原生代码，则仍必须完成源码版构建和 BuildId 核对。

## 完成判据

只有同时满足以下相关项才写“完成”：实现/说明与当前源码一致；失败路径无部分修改；已有测试报告逐项通过；原生变更的源码版构建和 BuildId 一致；需要的人工交互已真实执行；文档和 Skill 已同步。缺证据的项明确写“未验证”。
