> 2026-10-26 本会话已按用户指令完成归档。以下保留当时版本及审核事实，最终方向/时长与清理说明见[完成归档](../../../../../Progress/Archive/20261026-地面机甲镜头方向闪现与会话完成归档.md)；不补记未执行的视觉验收。

# 地面机甲 A4 B2：UE 导入与验收入口

**当前版本：**[后退步态与空中推进](BACKWARD_AIR_DELIVERY.md)（2026-10-25）。后退扩大轨迹并重标定，空中W转向与机体方向ASD接通；仅有效喷气前进最高864，其余324，退出额外推进平滑制动。源码构建、五表导入、69节点直接冷读取和原地图8实体保存核对完成。编辑器已重开，本轮未启动PIE或读图，手感由用户验收。

**此前版本：**[脚部跳变与机械腿结构约束](STRUCTURE_STABILITY_DELIVERY.md)（2026-10-25）。按用户新增PIE调试授权，定位锁脚瞬时释放造成的前进/转向跳变，补连续退锁、同轴刚性腿、膝角及修正速率约束，参数走GroundIK表。源码编译、针对性运行数值复查、69节点直接冷加载、四表和原地图8实体保存核对完成。诊断PIE已停止，编辑器已重开；视觉与未覆盖路径待玩家验收。

**上一版：**[固定操作方向、双枪回正与厚重步态](HEAVY_GAIT_DELIVERY.md)。保留机体WASD、非右键回正、216/432移速和扩大步幅方案。本版的前进挑地/转向反折收到用户失败反馈，现以上述结构约束修复记录为准；下面保留更早历史。

2026-10-23，用户明确授权：“导入进UE编辑器中，并核对蓝图、动画等资源是否正常（效果我来验）”。当前版已正式接入玩家 Ground，ModelId 4001。此授权不等于 B 造型终验通过。

## 2026-10-24 PIE失败后的补正

用户反馈所有动画缺失、枪口不跟随。实际PIE使用了正确的模型和ABP，但冷加载的图只有33个节点，新增的双枪姿势节点丢失，最终骨骼停在参考姿势。前次检查会先重建图，掩盖了此问题；其通过结果不能证明冷启动和PIE正常。

本次已恢复完整图，并将Ground角色蓝图从GameMode构造时硬加载改为角色解析时按需加载。编辑器出生清单和实际Ground入口共用此解析，仍指向`BP_GroundMech_Light`。检查脚本已去除重建/编译/保存，直接验证磁盘加载结果。原生构建通过；用户实际PIE与视觉需重新验收。

完整重启后保留34节点及全部关键姿势连接；直接检查冷加载资源的18组混合输入、11组姿势通过，最大枪口轴误差约0.00000171°。见[冷加载结果](animation_camera_cold_load_audit_20261024.json)、[最终构建](animation_graph_role_resolver_build_20261024.log)、[场景交付](animation_graph_repair_delivery_20261024.json)。当前PIE关闭，BP和ABP已打开；请用户在原地图以Ground启动，优先复验脚部动作和枪口朝向。

## 此前的动画修复与越肩操作

用户批准的动画/双枪/越肩方案已实施，源码Editor构建成功，地图和关联资源已保存，编辑器重新打开。本轮没有启动PIE，也没有截图或视口目检；以下数据核对不代替玩家验收。

- Walk/Run原先有有效动画样本，但固定前进时实际采样权重为空、时间为0；补齐原包网格插值配置并重采样后恢复。18组中心/前后/左右/斜向输入的权重、播放时间和脚骨变化通过，重启后仍有效。
- Idle、Strafe_L/R、Jump_Start/Loop使用指定A4B2动画。起跳Start一次后进入Loop，走出平台直接Loop，空中再次喷气不重复Start，落地恢复地面动作。
- 两枪独立播放原包派生`Anim_Mecha_Shoot`，保留原枪管旋转，按实际`MuzzleIndex`驱动并沿枪轴增加5cm后坐，使用原0.15秒曲线。开火层只覆盖Gun_R/L，腿部逐帧姿势与不开火相同。当前帧枪臂解算含真实枪口偏移及后坐位置，11组瞬态姿势的最大枪口轴线误差约0.00000171°。
- 鼠标转镜头，中心准星；地面A/D横移、W/S前后，机甲朝向瞄准。相机方向转世界方向后进入既有Ground网络接口，语义输入接口保留。
- 初始SpringArm为1084.4236cm，SocketOffset=`(0,338.24235,135.74471)`，Pitch=-14.85°、Yaw=-3.90°，Camera局部位置/旋转归零，FOV90，开启避障。根相对相机位置读回约`(-988.06158,406.38673,409.13710)`cm；滚轮范围704.88–2168.85cm。
- 建造、传送选点、菜单恢复自由鼠标并暂停转镜/开火，退出后恢复越肩。F10沿用原GM入口，角色权限保持原有规则。

证据：[最终姿势/资产核对](animation_camera_final_audit_20261024.json)、[最终构建](animation_camera_build_delivery_retry_20261024.log)、[BuildId](animation_camera_build_ids_20261024.json)、[保存的验收场景](animation_camera_scene_20261024.json)。动画蓝图最终编译为0错误、0警告。UI实际操作、完整Ground玩法、网络远端表现仍待用户确认。

## 现在使用的资源

- 模型：`/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2`。
- 动画蓝图：`/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations/ABP_GroundMech_A4B2`，父类仍是项目 `GuLiGroundMechAnimInstance`。
- 14 段原 MechaDestroyer 动作重定向为新骨架副本，配套 2 个混合空间、2 个转向蒙太奇。原 `/Game/MechaDestroyer` 资源保留，原包蓝图继续使用其原资源。
- 正式角色沿用 `/Game/GuLiStrike/GroundMech/BP_GroundMech_Light`；靶场独立角色 `Review/BP_GroundMech_FireReview` 同步接入新版。移动、建造、火箭跳、伤害与升级及网络仍走原玩法系统；正式角色相机和物理输入按上面的2026-10-24方案更新。
- 17 骨骼刚性绑定、8 个材质槽。主体 24,654 三角面，描边壳 22,122 三角面。阵营主辅色通过既有本地阵营系统写 CPD 8/12/16；两处蓝图增加 `GroundModelPresentation` 注册组件。
- 右枪口 `Muzzle` 挂 `Gun_R`；左枪口 `Muzzle_L` 挂 `Gun_L`；`Jet_Outlet_R/L` 挂 `cockpit`。左右交替仍使用此前接好的发射序号逻辑，总射速和单发伤害沿用原表。

## 用户验收

**当前试玩入口为 `/Game/Maps/LVL_CommanderMassPrototype`，GuLi Local PIE 的 MainRole 已设为 Ground；请用户自行启动PIE。** 本轮保存并读回了正式角色`BP_GroundMech_Light`、网格`SK_GroundMech_A4B2`和动画实例类`ABP_GroundMech_A4B2`。此前Observer问题修复阶段曾在用户的游玩会话中确认实际占有，见[历史玩家确认](pie_player_confirmed_20261024.json)，该报告不代表本轮已运行PIE。

此前把FireReview描述为可直接单人PIE不准确：当前`GuLiLocalPIESubsystem`只为CommanderMassPrototype建立本地协调器/对端会话。用户在FireReview启动PIE后得到Observer、无Pawn，报错`Hybrid requires an explicit valid room identity`，见[失败记录](pie_entry_before_20261024.json)。修复仅将本地试玩席位从Commander改为Ground并使用受支持地图；沿用真实协调器分配及占有流程，没有手动Possess或修改动画。

`/Game/Maps/LVL_GroundMech_FireReview`继续保留为编辑器模型展示图，`ArtReview/GroundMech_A4B2`下红蓝两台展示机仅在编辑器出现，坐标约`(8600,-8800,289)`、`(9400,-8800,244)`。旧起点和部署配置保留，但当前不作为直接PIE入口。实际运行入口按下列步骤核验：

1. 启动PIE并控制Ground：确认初始越肩构图、屏幕中心准星和鼠标转镜；滚轮可缩放，靠近遮挡物时相机避障。
2. W/S、A/D、组合键及Shift：确认前后/横移/斜向，脚部持续运动，身体朝向准星目标，走跑切换自然。
3. 按住左键：确认左右逐发交替，各自枪管旋转和后坐；近处、远处、高低目标均观察枪口、特效与弹丸是否指向同一点，腿部动作不被覆盖。
4. 从地面按空格起跳，松开后在空中再次推进，再走出平台：分别确认起跳一次→浮空循环、再次推进不重播起跳、走出平台直接浮空；落地恢复Idle/移动。
5. B/数字键进入建造，右键或Esc取消；F10打开/关闭原GM界面：确认期间自由鼠标、停止转镜和开火，退出后恢复。传送选点只按既有角色权限操作。
6. 反馈具体操作、左右枪、动画名和问题时刻；阵营换色、远端动画、双客户端与完整玩法仍由玩家确认。

## 2026-10-23导入阶段技术记录

| 检查 | 结果与证据 |
|---|---|
| 网格/骨骼导入 | 17 骨骼，单位缩放约 1；107,080 个渲染拆分顶点，0 无效权重；[导入报告](ue_mesh_import.json) |
| 动画适配 | 原时长/帧数保留；17 条骨骼轨道；所有帧腿长约束成立，0 个目标超出解算范围；[重定向报告](retarget_report.json) |
| UE 动画回读 | 每段首/中/末帧位置误差最大约 0.000108 cm，旋转误差低于 4e-8；[安装报告](ue_animation_install.json) |
| 蓝图 | 新动画蓝图、正式角色、靶场角色/模式及原包 7 个蓝图编译通过；[角色安装](player_install.json)、[最终蓝图检查](final_blueprint_material_checks.json) |
| 材质/插槽 | 8 个材质编译调用成功；4 挂点定位与方向回读通过；[材质与插槽](ue_material_socket_install.json) |
| 引用与玩法配置 | 新版 29 个资源的硬依赖无缺失；两处角色输入、速度、相机、胶囊及武器表/启用值与替换前一致；[资源检查](resource_audit.json) |
| 数据源 | Excel → JSON/CSV → UE 三张模型表回读一致，无关行保留；[源表更新](catalog_promotion.json)、[数据表导入](datatable_import.json) |
| 编辑器场景 | 两台展示机真实创建了新动画实例，4 插槽存在，Ground 角色路由正确，场景已保存；[场景记录](review_scene.json) |

2026-10-23导入阶段未启动PIE；2026-10-24根据用户正在运行的PIE无法控制角色的反馈，修正入口并重新打开该次游玩，只读核对实际身份、占有、模型和动画实例。未运行自动输入、完整玩法回归、联机专项或性能测试，也未读取输出截图/渲染图做视觉测试。为适配更低的折腿，移动/起跳动作的根相对步幅收至原轨迹的55%，转向收至40%；游戏移速、动作时长未改，是否滑步由用户验收。UE使用三档法线光照材质，未将Blender AO烘焙入材质，外观匹配也由用户验收。

旧全局模型迁移校验器对另一处历史装配假设失效，在查询时抛出 `StopIteration`；本轮未改该校验器。使用限定 4001 的原生字段/材质合同检查、无关行比对及共享 CSV 导入器的全行回读完成三表导入，不宣称全项目校验通过。

## 来源与回退

输入为当前 Blender 会话的保存副本 [GroundMech_A4B2_UEInput.blend](GroundMech_A4B2_UEInput.blend)，SHA256 `7a7182fe49c1cd1305718fbc4124036a4f7480bf425d6f0c0d44400d2563067a`。原可编辑 [B_v2](../GroundMech_A4B2_Editable_B_v2.blend) 保留，枪细节版本 SHA256 `2fc8f6f8f4412404fe7b73bc59bd91340481c50525fbea66e33ef0d1472dbe28`。

[导出 FBX](SK_GroundMech_A4B2.fbx) SHA256 `01d1aeb161b1bcfb9d28b6817613372bd0a78f2d853407b18228b984be89a696`，导出过程仅操作副本。

角色备份位于 `MechaDestroyer_A4B2/Rollback/BP_GroundMech_BeforeA4B2` 与 `BP_FireReview_BeforeA4B2`。真实导入前模型表数据见 [catalog_json_before.json](catalog_json_before.json)；`catalog_promotion.json` 的 before 网格是在重试期间采集，不作为最初状态的唯一回退来源。原 Style_v9 和原包资源保留。回退应同时恢复角色、ModelId 4001 的三张源表及 UE 表，不能只换网格。

本次动画/相机修改前的资产备份在`MechaDestroyer_A4B2/Rollback/AnimationCamera_20261024_031332`。原生代码与新图节点依赖需要配套，不能只恢复网格来回退动画/操作。

重放脚本在 `Scripts/GroundMech/MechaDestroyer`：`export_b2_ue.py`、`import_b2_mesh_worker.py`、`export_b2_source_animation_data.py`、`retarget_b2_animation_data.py`、`install_b2_animations.py`、`install_b2_materials_sockets.py`、`promote_b2_catalog.py`、`install_b2_player.py`、`finalize_b2_blueprints.py`、`audit_b2_resources.py`、`prepare_b2_review_scene.py`。源表晋升脚本有完成标志，不应盲目重复运行。本次修复脚本`fix_b2_animation_camera.py`和姿势查询`inspect_b2_animation_camera.py`要求先加载对应源码Editor构建；查询只核对加载的图和瞬态姿势，不重建/编译/保存目标资产，不启动PIE。
