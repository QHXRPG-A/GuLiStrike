# 2026-09-17 模型三渲二与指定爆炸接入

最新调整见 [扫荡者去线稿、爆炸缩放与关卡亮度](../StyleAdjust_20260917/README.md)：扫荡者现在取消内线和外轮廓，战争机器保留；地面摧毁爆炸为此前的 0.6，僚机摧毁配置不变；指挥官关卡已修正压黑的后处理。下文含描边的扫荡者面数是此前阶段数据。

## 在 UE 中查看

- 模型预览地图：`/Game/Commander/Units/Tactical/Cel/Review/LVL_CelModels_Review`。
- 扫荡者：`/Game/Commander/Units/Tactical/Cel/Sweeper/Meshes/SM_Sweeper_Cel`。
- 战争机器：`/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Cel`。
- Ship：`/Game/GuLiStrike/Ship/Stylized/Meshes/SM_Ship_AnimeHull` 与 `SM_Ship_AnimeContour`。
- 爆炸：`/Game/GuLiStrike/FX/CombatExplosions` 下的 `NS_GroundDestruction_03`、`NS_WingmanDestruction_05`、`NS_WingmanBombardment_01`。

`UE_*_three_quarter.png`、`UE_*_front.png`、`UE_*_side.png` 为真实引擎画面。`FXFrames` 包含起爆、峰值、扩散、烟雾、尾烟和结束帧，以及轰炸远景与地面斜坡预览。

## 模型制作与接入

两台单位保留此前手工模型、原有橙红/暖白/蓝灰配色、扫荡者弧形侧甲及战争机器平行 45° 双背舱。本轮加入 2K 内部结构线遮罩、深蓝外轮廓壳和三档明暗。内部线条不显示三角拓扑。主体材质使用固定艺术光向的 Unlit 分档着色，不模拟动态场景灯光；外轮廓关闭投影。

静态/骨骼 FBX、贴图与 Blender 文件位于 `Models`。当前 Blender 交互文件为 `Models/Tactical_Cel_Review.blend`，保留旧场景并新增 `Sweeper_Cel_Review` 和 `WarMachine_Cel_Review`。

| 模型 | LOD0 | LOD1 | LOD2 | LOD3 |
|---|---:|---:|---:|---:|
| 扫荡者（含描边） | 10,912 | 2,727 | 900 | 220 |
| 战争机器（含描边） | 27,303 | 6,826 | 300 | 70 |

屏幕比例阈值为 1 / 0.32 / 0.09 / 0.025；模型有主体与轮廓两个材质槽。LOD0 未达到早期 4,000 / 6,000 面预算，不能据此宣称批量单位性能优化完成。未在本轮实现材质顶点机械动画。

Soldiers 源表和 UE DataTable 已指向新静态模型，Id 1/2、行名及战斗数值保留，显示名为扫荡者/战争机器。WeaponMounts 源表与 DataTable 同步新的枪口和两个背舱出口，并在网格上创建对应挂点。

Ship 船体 13,314 面、4 套 UV，轮廓 3,088 面；第四套 UV 读取原 4K 内部线稿。FBX 已含原模型 0.5 Build Scale，不再重复缩小。原 Hull 保留碰撞、挂点与尺寸接口。

**Ship 玩家蓝图已保存并通过独立进程读回**：用户关闭重复 UE 窗口后，文件占用解除；`Scripts/install_stylized_ship.py` 已编译并保存 `/Game/GuLiStrike/Ship/BP_CombatAvatarFly01`。重新加载并生成临时实例确认 AnimeHull/AnimeContour 可见、附到原 Hull、局部变换为单位变换且不新增碰撞；原 Hull 关闭绘制，原网格、变换、碰撞和 55 个挂点保留。新船体 4 套 UV 保留，内部线稿读取第四套 UV。证据为 `ship_install.json`、`ship_saved_readback.json`；读回进程未保存任何资产。

## 爆炸

所选商城源资产只读，项目副本保留火球与烟团曲线和完整消散。NS03 添加地面薄环；NS05 添加面向镜头的空中环；均在 0.05 秒生成，0.55 秒结束。默认环直径分别 950 / 1050 cm，再乘范围参数。

NS01 保留自己的环层，三个径向发射器的初始水平位移/速度乘 0.70，地面图形由 2500 缩到 1750；没有叠加第二套冲击波。以上是参数收缩量，不能当作已测得最终屏幕直径严格为火球 1.5 倍。

范围统一走 `User.Area_Scale`，组件缩放 1。与面积绑定的 Burst Count 改为原资源 Area=1 的固定数量。发射器距离裁剪从 5,000 调到 180,000 cm。Niagara 池容量 32；摧毁系统现有并发/距离策略不变。

`DA_WingmanGroundExplosion` 已保存为新 NS01、作者缩放 5、随机朝向、无额外冲击波、视觉期限 5.25 秒。伤害/半径/攻击节奏不改。DefaultGame.ini 指向新 NS03/NS05；部署与切换脚本已识别新变体。

## 证据与边界

- `model_import.json`、`model_finish.json`、`soldier_table_import.json`、`mount_table_import.json`：资产、LOD、挂点及数据读回。
- `material_final_and_pie.json`：13 个相关材质编译通过并分别保存。
- `reference_explosions_build.json`、`fx_final_tuning.json`、`explosion_install.json`：三套 Niagara 编译零错误，引用和缩放读回。最终参数以 `fx_final_tuning.json` 和制作脚本 Revision 2 为准。
- `build_and_syntax_gate.json`：源码 UE5.7 Editor/Game Development 均成功；项目和五个相关插件的 BuildId 与引擎一致。
- `runtime_review*.json`：现有 dedicated + two clients 入口的局部记录；完整实战轰炸、僚机死亡入口和 GPU/CPU/透明覆盖性能对照需另行确认，未宣称性能通过。
- 主编辑器此前启动 PIE 后控制未返回；用户关闭重复窗口后桥接已恢复，确认 PIE 已停止，临时 Live Coding 设置已恢复。独立验证进程未保存游戏关卡；这次恢复及 Ship 保存读回不代替完整实战验收。

## 回退与复现

- 模型源和旧网格均保留。Soldiers 源表可恢复之前 `Tactical/Sweeper` 或原 Crowd 网格；之后执行现有数据导出/导入管线。
- Ship 蓝图回退副本：`/Game/GuLiStrike/Ship/Stylized/Rollback/BP_CombatAvatarFly01_BeforeCel`。
- 轰炸运行 `Scripts/set_wingman_explosion_variant.py`，变量 `WTE_INSTALL_VARIANT` 支持 `reference` / `toon` / `old`；`old` 恢复 Big_17 + 独立冲击波。
- 摧毁原配置段和之前轰炸变体记录在 `explosion_rollback.json`。
- 重导入运行 `Scripts/import_cel_model_assets.py`（独立导入进程），会调用 `finalize_cel_model_assets.py` 保留 LOD 和挂点。
- 爆炸制作/安装分别为 `Scripts/build_reference_combat_explosions.py`、`Scripts/install_reference_combat_explosions.py`。只保存明确列出的目标资产。
