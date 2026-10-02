# 指挥官导航连通性

实现位于 GuLiStrikeEditor。新原生代码编译并重启编辑器后才可使用下列入口；这些命令只读取编辑器源世界，不烘焙、不开始 PIE。

```text
gs.Navigation.CheckConnectivity
gs.Navigation.CheckPoints -30343 69255 1897.7790832519531 -29962.509924831735 69008 1868.789886580086
gs.Navigation.CheckPoints -30343 69255 1897.7790832519531 -37338.484873398265 64139.232366813674 7532.089454739742
```

第二行是山脚内的连接例子，第三行是此前已确认的山脚/高台断开例子。坐标单位厘米，控制台投影范围 X/Y 10、Z 50 厘米。Connected 表示双向可达；OneWay 表示仅一方向可达；Disconnected 表示两个方向都不通。端点未被导航覆盖或配置/数据不可用时返回独立状态，不冒充断开判定。

Python/Blueprint 使用 `GuLiNavigationConnectivityLibrary.AnalyzeWorldConnectivity` 和 `CheckPointConnectivity`。Python 可以明确传入源世界或 PIE 世界；结果只针对该世界本次读取的 NavMesh，分析不会修复运行时路径跟随。示例在 UE Python 控制台执行：

```python
import unreal
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
result = unreal.GuLiNavigationConnectivityLibrary.check_point_connectivity(
    world, unreal.Vector(-30343, 69255, 1897.7790832519531),
    unreal.Vector(-37338.484873398265, 64139.232366813674, 7532.089454739742),
    unreal.Vector(10, 10, 50))
print(result.status, result.start_to_end, result.end_to_start, result.message)
```

整图报告提供每个地面强连通区域的 RegionId、代表位置、包围范围、面积（平方米）和多边形数。RegionId 是单次诊断编号，不是持久玩法身份。所有通过默认过滤的导航多边形都参与；有效特殊链接按 UE 默认查询规则计算方向和可用性，无法评估特殊链接依赖时返回错误。

地图 `/Game/Maps/LVL_CommanderMassPrototype` 在 `Config/DefaultGame.ini` 的 `GuLiNavigationConnectivitySettings.RequiredWorldPackages` 中启用严格连通合同。其他地图可显式加入该列表。新门禁复用现有 `GuLiNavigationBakeLibrary.prepare_world_navigation(world, True)`、`validate_world_navigation(world)`、PIE 许可和 Cook 校验入口。任何额外地面分区，包括没有据点或很小的区域，都会使认证失败；失败发生在新元数据写入和烘焙输出保存之前。UE Build Paths 自身负责生成数据，随后需要经过 GuLi Prepare/Validate 认证。

在原生代码加载后由玩家显式执行 Prepare，预期当前未修复导航返回失败，Output Log 出现 `[GULI_NAV_CONNECTIVITY]` 和各分区的 `[GULI_NAV_CONNECTIVITY_REGION]`。修复导航参数或地形并重新烘焙，完整图仅有一个分区时才可通过。这项检查不忽略孤岛，也不会自动调整坡度、地形或搜索预算。

`prepare_scene.py` 仅在指定地图制作三个 EditorOnly Note，保留选择，不改玩家入口；保存后读取相关实体。需要当前源码版 UE 编辑器和已启用的本机 Python 桥：

```powershell
python Scripts/commander_editor_python.py --file Scripts/NavigationConnectivity/prepare_scene.py --output outputs/navigation-connectivity-20261002/scene-authoring-result.json
```

已保存验证标记在 Outliner 文件夹 `CommanderIsland/Review/NavigationConnectivity`，标签统一为 `GuLiNavigationConnectivityQA`。脚本不运行新代码、烘焙、PIE 或自动化测试。场景实体记录在 `outputs/navigation-connectivity-20261002/scene-entities.json`。

## 坡面生成预设修正

CommanderSoldier 采用20厘米台阶、44度最大坡度和 `UseStepHeightFromAgentMaxSlope` 边缘过滤。Low/Default/High 的水平格子为19/19/9.5厘米，垂直格子均为2厘米，台阶均为20厘米。当前地图没有启用高精度区域的修改器，使用默认分辨率；现有水域 NavArea_Null 保留。Default 代理仍使用其独立生成配置。

预设由 GuLiStrikeEditor 私有代码集中维护。现有 `prepare_world_navigation` 在计算源指纹前应用预设，取消旧参数任务并完整重建受影响的导航；补建的新导航实例也执行相同流程。`validate_world_navigation` 会拒绝不符合预设的数据。旧 `migrate_object_scale020` 对 CommanderSoldier 调用同一预设，其他代理仍采用原缩放迁移。

本次原生代码需要另行获准编译和重启后才生效。编译加载后，通过现有编辑器入口执行 Prepare；继续要求所有可行走地面属于同一强连通区域。若仍有真实陡坡断区，保留失败状态及坐标/面积/边界报告，后续单独处理地形。

当前指挥官Map的既有查询预算为16384。SupportedAgent台阶改变后，UE可能替换旧NavData；Prepare会恢复此Map的预算并重新创建实时默认查询过滤器，Validate同时检查属性和过滤器。其他Commander地图保留各自查询设置。

`prepare_slope_scene.py` 只更新已有三处 Note 的说明及导航绘制：仅显示 CommanderSoldier，显示偏移50厘米；保留 Note 的身份、位置与无碰撞设置。它不执行新原生代码、导航重建、两点查询或 PIE：

```powershell
python Scripts/commander_editor_python.py --file Scripts/NavigationConnectivity/prepare_slope_scene.py --output outputs/commander-slope-navigation-20261002/scene-authoring-precompile.json
```

当前山脚 A/B 查询用于确认原有连接保留；山脚 A/高台 A 用于检查生成修正是否恢复合法缓坡连接。场景说明已改为待重建核对，不再把高台必须不连通作为修正后的预期。准备失败、原生代码未加载或整图仍有多个分区时，不能标记导航修复通过。

## 2026-10-02 编译与重建记录

用户已授权本轮编译与重建，源码版Editor构建通过且BuildId一致。新参数及16384预算已生效，完整图为20个地面强连通区域，严格认证失败；未保存本次导航输出，当前UE保留新内存数据。19个孤立区域的完整位置、面积与边界见 `outputs/commander-slope-navigation-20261002/navigation-rebuild-report.md`。

三个旧Note的Z与当前高度图/碰撞不一致，旧控制台示例当前返回StartNotNavigable。保留它们的历史身份和位置；按相同XY的当前地面Z约1073.44165厘米查询，两组双向Connected。当前三个位置均在约10.73米地面，不能将这些连接结果作为原75.32米高台爬坡案例修复的证明。当前参数与两类查询的分开记录见 `navigation-final-readback.json`，本轮没有修改地形。

## 当前验收配置（2026-10-02）

用户已要求本会话全部文档通过。DefaultGame.ini 的 GuLiNavigationConnectivitySettings 当前使用 `!RequiredWorldPackages=ClearArray`，暂关全图认证，分区查询和运行时不可达新令预检继续有效。当前源图20个分区为原始诊断事实；恢复严格认证时改回 `+RequiredWorldPackages=/Game/Maps/LVL_CommanderMassPrototype` 并重新 Prepare。验证证据已迁入 `Artifacts/Diagnostics/CommanderIslandSession_20261002`。
