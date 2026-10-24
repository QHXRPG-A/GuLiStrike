---
schema: guli-progress/v1
id: DEV-20261023-001
work_id: WORK-20261023-001
kind: development
role: root
title: 地面机甲 MechaDestroyer 替换与风格化 — 实施记录
areas: [art, combat]
categories: [art, gameplay]
status: verification
verification: partial
created: '2026-10-23'
updated: '2026-10-24'
summary: 修订先限角再生成瞄准姿态，开放浮空右键转向，W与右键共用转身曲线；源码构建、冷加载和地图回读完成。
next_action: 用户在LVL_CommanderMassPrototype以Ground复验越界瞄准稳定、浮空右键及W与右键转速同步。
relations:
  requirement: REQ-20261023-001
status_note: 本轮代码静态检查通过，原测试地图已保存并完成实体数据核对，待玩家验证效果。已按授权编译重启并直接读取保存图；未启动PIE、未求值运行姿势、未读图测试。
---

# 地面机甲 MechaDestroyer 替换与风格化 — 实施记录

## 已落地

- [x] 原包解压至 `ArtSource/Characters/GroundMech_MechaDestroyer_20261023/OriginalPackage`，保留压缩包清单；原包57个 UE 资源保留 `/Game/MechaDestroyer` 路径，未合并其项目 Config。
- [x] 项目候选 `MechaDestroyer_v1/Meshes/SK_MechaDestroyer` 复用原骨架，并增加 mesh-only `Muzzle`、`Muzzle_L`、`Jet_Outlet_L/R`，保留原 Gun 和 Rocket 插槽。
- [x] 原包蓝图与独立适配蓝图已导入编译；2026-10-24补正此前单侧开火接线遗漏，双枪实际姿势数据见本次修复。
- [x] 原生 Character／AnimInstance／Weapon／Rocket／客户端开火表现适配编译；左右开火由累计有效射击数奇偶选择，沿用现有 MuzzleIndex 网络提示字段，总射速和伤害不变。
- [x] 保存原地图并以源码 UE5.7 构建、重启；用户已明确授权。
- [x] 用户审图迭代至 A_v4：A_v1 改原 Ground 下身；A_v2 追加折腿；A_v3 被指出臃肿；A_v4 回到 A_v2，只调整腿部关节折角。
- [x] 用户明确两张 A_v4 为建模定稿；保存本轮附图、哈希与批准原话。
- [x] 制作真实 Blender B_v1：172 主体分件、12,088 主体三角面，9,420 描边三角面，UV、可编辑倒角、三档明暗和阵营属性驱动。
- [x] 17 骨骼刚性绑定、四挂点、上身瞄准与左右后坐独立控制；原待机采用独立动作重定向，实际烘焙脚部目标误差小于 0.000001 m。
- [x] 保存并打开成品 blend，导出真实模型整体、正侧背和单机预览，准备用户参考对照页。
- [x] 正式 ModelId 4001 和玩家蓝图安装、编辑器运行入口配置核验；实际游玩待用户。
- [ ] 用户核对 Hybrid 远端实际动画和开火；远端零时间推进的代码已修复，未进行联机运行验收。
- [ ] 用户审核 B 的造型还原度与实际动作效果；技术适配已完成，不替代视觉验收。
- [x] 用户明确授权当前版导入，接入正式风格化资源与阵营改色，并保存对应 Ground 验收地图。

## 证据与边界

工作目录：[资产目录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023)。当前入口：[UE交付与检查记录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/UE_DELIVERY.md)、[Blender B_v2](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/REVIEW.md)。A_v4 已放行，B_v1 曾被用户反馈“不一致”。当前 B_v2 获得导入授权，外观终验仍由用户完成。

### 最新：限位稳定、浮空右键与W同速（2026-10-24）

按三项反馈改为先裁剪瞄准角再生成旋转；开放空中右键转向；W和右键共用转身曲线，移动/空中保留对应动画。源码编译通过，首次链接受旧编辑器退出占用影响，退出后重试成功；冷加载55节点和原地图实体回读通过。详见[本轮交付](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/AIM_TURN_DELIVERY.md)。未启动PIE或读图，待用户验收。

### 此前：双枪臂水平摆幅150°（2026-10-24）

用户要求双手左右总摆幅150°。源表及UE表原已为单侧75°，但旧节点只限枪口方向；参考骨架的枪管相对枪臂外偏约23.73°。现于最终写骨前对GunArm_L/R实际水平朝向各限±75°，枪骨随臂旋转，限位优先，弹丸继续沿最终枪轴发出。未改表值、配置指纹或网络字段。源码Editor构建退出0、BuildId一致；冷加载55节点保留，原地图及8个实体保存回读通过。详见[本轮交付](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/ARM_YAW_DELIVERY.md)。未启动PIE、未求值运行姿势或读图，待用户验收。

### 此前：W跟随镜头与降速修订（2026-10-24）

用户反馈W不转向、转身太慢和步态过快，确认W按住持续跟随及加减速同步降低。新增独立镜头朝向输入，不触发右键镜头；走/跑216/432、加减速864、移动转速1080、左右转身2倍，标定速度保持。输入/移动/共享控制版本3，Ground状态7。源码构建、55节点直接冷加载、配置与网络清单回读通过，原验证地图已保存；详见[本轮交付与玩家步骤](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/W_FOLLOW_DELIVERY.md)。未启动PIE，效果待用户验收。

### 此前：数据驱动机动、脚部IK与右键越肩（2026-10-24）

四张Excel新表经标准导出、生成结构及DataTable导入接入角色快照和网络指纹；技能表继续负责喷气与燃料。按实际脚底标定支撑曲线和参考速度，接入无拉伸双腿IK、有限骨盆补偿、平台局部锁定、步幅/步频匹配。普通镜头不带动机体，右键按指定转身蒙太奇及根曲线分步转向；最终枪轴驱动射击及偏离提示。

源码Editor构建成功，BuildId一致。保存后完整重启，直接读取55节点及关键连线通过；未先重建图、未启动PIE。主测试地图保存平地、18°坡面、四级台阶和入口定位点，实体数据及200/600出生清单检查通过。详细字段、调表命令、证据和玩家步骤见[本轮交付](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/GROUND_MOBILITY_DELIVERY.md)。运行、联机和视觉效果尚未由用户验收，旧姿势报告不证明本轮新增IK通过。

### 此前：PIE全部动画缺失的冷加载修复（2026-10-24）

用户在自己的PIE中明确反馈“动画全部都没有，枪口也没有对准射击的方向”。只读查询确认实际占有新版Ground角色、动画实例及瞄准输入均正确，但脚骨/枪骨连续读回为参考姿势；加载的AnimGraph只有33个节点，缺少`AnimGraphNode_GuLiGroundMechAim`，导致姿势链断开。证据：[运行状态](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/pie_animation_failure_20261024.json)、[缺失节点](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/pie_loaded_graph_20261024.json)。

上一轮`inspect_b2_animation_camera.py`在姿势核对前重建了图，因此掩盖了重开编辑器后的故障。该脚本现在先检查已加载图的34个节点与关键Pose连接，再读取姿势；不重建、编译或保存目标资产，原报告保留为历史内存图结果。

恢复节点后，同进程保存/包重载可保留34个节点。进一步定位`AGuLiCommanderGameMode`的原生构造函数通过`FClassFinder`过早加载Ground蓝图及其ABP。最终改为`DefaultGroundPawnClass`软引用，在统一角色解析入口按需加载；编辑器出生清单查询CDO与实际出生走同一解析函数，显式角色覆盖（含空项）仍优先。`GuLiStrikeAnimGraph`维持Default阶段，运行时模块不增加编辑器依赖。直接把图模块提前到PreDefault的尝试导致启动失败，已撤销并保留[失败日志](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/animation_graph_predefault_failed_20261024.log)。

角色解析改动已通过源码Editor构建：[构建日志](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/animation_graph_role_resolver_build_20261024.log)。未改动画内容、骨架、模型或武器数值；已结束用户失败的PIE以修复并重启编辑器，未代替用户操作角色或做视觉目检。

完整退出/启动后，34个节点及唯一瞄准节点保留，Pose链与7个数据输入均连接；不重建图、不编译资产的18组混合输入和11组瞬态姿势检查通过，最大枪口轴误差约0.00000171°。证据：[冷加载图](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/animation_graph_cold_load_20261024.json)、[冷加载姿势](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/animation_camera_cold_load_audit_20261024.json)、[地图保存与Ground引用](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/animation_graph_repair_delivery_20261024.json)。当前PIE关闭，BP/ABP已打开，供用户复验；这仍不是玩家视觉或实战通过。

### 此前：动画冻结、双枪与越肩操作修复（2026-10-24）

执行用户明确批准的方案。修改前备份当前内存中的角色蓝图、ABP和两个混合空间至`MechaDestroyer_A4B2/Rollback/AnimationCamera_20261024_031332`，保留原包、模型、17骨骼、阵营材质及Ground玩法。

- 冻结的第一处异常是混合空间运行采样返回空权重，固定前进的播放时间一直为0；不是源动画缺帧。恢复原包网格插值设置并执行`ResampleData`后，18组中心/前后/左右/斜向输入权重和时间正常，重开编辑器后仍有效。步行/跑步前进最大腿部旋转变化约51.45°/70.92°。
- 新ABP直接读取原生运动快照，中心Idle，前后Walk/Run，左右指定Strafe；播放节点正常循环。起跳显式时间仅在地面起推时归零，0.333秒后进入Jump_Loop；空中恢复喷气与走出平台不重播起跳，落地返回地面混合。
- 右侧开火层原先没有动画输入，现为两个独立Shoot求值器，分别读取实际发射MuzzleIndex对应的时钟。只混合Gun_R/L，保留原包枪管旋转；沿枪轴叠加5cm、0.15秒现有曲线后坐。开火和不开火的腿部姿势逐帧一致。
- 当前帧组件空间节点分别求解两枪臂，使用实际Socket偏移和后坐后的枪位置。近/高/低目标及行走、跳跃组合的枪口轴误差最大约0.00000171°；此为数学姿势核对，不替代视觉验收。原总射速、伤害、发射序号、网络输入版本不改。
- 相机初始相对根位置`(-988.06158,406.38673,409.13710)`cm，Pitch=-14.85°、Yaw=-3.90°、FOV90，Arm1084.4236cm，SocketOffset`(0,338.24235,135.74471)`，Camera本身位置/旋转为0。启用避障，缩放范围704.88–2168.85cm，移除BeginPlay按旧高度重置距离。
- 鼠标Mouse2D转镜头、中心准星使用统一色库奶油白`#FEE4D9`；WASD按镜头转为世界方向后进入原Ground输入。A/D横移、身体朝瞄准方向，语义输入仍使用世界轴。建造/传送选点/菜单暂停转镜与开火并释放鼠标，退出恢复；保留现有权限和UI入口。
- 远端ApplyPose取消每次`TickAnimation(0)`，非模拟副本由表现帧推进动画，模拟端继续使用既有逻辑步长。

| 模块 | 职责与依赖 |
|---|---|
| GuLiStrike | 输入、移动快照、发射事件和运行时骨骼解算；不依赖编辑器模块 |
| GuLiStrikeAnimGraph（UncookedOnly） | 单个动画图节点的创作外壳，依赖运行时模块；支持蓝图编译/烘焙，正式运行不需要该节点类 |
| GuLiStrikeEditor | 限定资源安装、采样重建和瞬态数值查询；依赖以上模块，无反向依赖 |

新节点从本轮临时Editor归属移到UncookedOnly，准确类重定向保留已保存ABP；最终ABP编译0错误/0警告。沿用用户本任务“保存、编译并重启UE”授权，源码版`D:/UnrealEngine-5.7`构建`GuLiStrikeEditor Win64 Development`成功，BuildId与引擎一致。首次编辑器工具构建的接口错误已修正，日志保留；未运行Game目标、PIE或视觉读图。

证据：[冻结前](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/blendspace_forward_before_20261024.json)、[修复过程](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/animation_camera_fix_20261024.json)、[最终姿势核对](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/animation_camera_final_audit_20261024.json)、[场景实体与相机](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/animation_camera_scene_20261024.json)。对应Map已保存，当前入口仍为`/Game/Maps/LVL_CommanderMassPrototype`，MainRole=Ground，角色`BP_GroundMech_Light`、动画`ABP_GroundMech_A4B2`；本轮保持PIE关闭供用户自行启动。

### 此前：PIE观察者问题与运行入口更正（2026-10-24）

用户在已开启的PIE反馈“并没有在控制机甲”。只读核对发现FireReview中玩家为Observer、Team Unassigned、无Pawn，网络错误为`Hybrid requires an explicit valid room identity`。当前项目本地PIE子系统只为`/Game/Maps/LVL_CommanderMassPrototype`启动协调器与逻辑对端，旧GameMode角色映射不能代替会话身份分配；上一轮可直接在FireReview试玩的交付判断不完整。

修复将`Config/DefaultEditor.ini`的GuLi Local PIE `MainRole=Commander`改为`Ground`并同步当前编辑器配置，保存旧图，结束失败PIE，切到受支持的CommanderMassPrototype后重新打开此次游玩。未手动创建/占有Pawn，角色由原Hybrid出生和占有流程生成；未修改模型、动画、蓝图或C++。

实际读回：玩家Ground、BattleReady=true、locally_controlled=true，控制器与相机均绑定`BP_GroundMech_Light_C_1`；其Mesh为`SK_GroundMech_A4B2`，AnimClass和实际AnimInstance均为`ABP_GroundMech_A4B2_C`，输入仍为`IMC_GroundMech`，移动未锁定，4插槽存在。PIE保持开启供用户操作。当前本地会话及对端就绪、600单位加载，不将此记为完整战斗或联机效果通过。

证据：[修复前](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/pie_entry_before_20261024.json)、[会话就绪](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/pie_entry_status_20261024.json)、[实际角色绑定](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/pie_player_confirmed_20261024.json)。无截图目检、无输入注入或玩法自动化。

### 此前：B_v2正式UE接入与资源核对（2026-10-23）

用户明确“导入进UE编辑器中，并核对蓝图、动画等资源是否正常（效果我来验）”，据此执行导入，不再次索要B批准，也不补记视觉通过。

- 当前Blender会话另存输入副本，导出厘米制骨骼网格；独立新骨架保留17骨骼名称与父子结构，按折腿及枪轴的新版静止姿态适配14段动作。原包骨架、动作和蓝图保留。
- `/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2` 共29个正式新资源：网格/骨架2、动画类资源19、材质8。UE回读每段首中末帧，位置误差最大约0.000108cm；新骨架2个混合空间、2个蒙太奇与新ABP全部接通。新ABP沿用项目原生父类和原适配状态机。
- 为避免低趴短腿拉伸，移动/起跳的根相对步幅为原轨迹55%，转向为40%；原时长/帧数及游戏移速保持。全部帧腿长约束成立、0超范围目标；滑步和动作自然度仍待用户检查。
- 正式`BP_GroundMech_Light`与独立靶场`BP_GroundMech_FireReview`都启用整机，绑定新SK与`ABP_GroundMech_A4B2`，Root Motion为Ignore；清除旧独立装甲/肩部/枪网格。ModelId4001通过Excel管线晋升，三张UE表全行回读一致。
- 给两处角色补`GroundModelPresentation`，明确注册Body/CharacterMesh0到现有本地阵营系统，使用CPD8/12/16。4插槽分别绑定Gun_R/Gun_L/cockpit，位置及方向回读正确。
- 原包7个蓝图、新动画蓝图、正式/靶场角色与模式编译通过；8材质编译调用成功；29新版资源硬依赖无缺失。角色输入、速度、相机、胶囊及武器表/启用值与替换前备份一致。本轮没有修改或编译C++。
- 先保存当前Commander地图，再打开并保存`/Game/Maps/LVL_GroundMech_FireReview`。两台仅编辑器显示的红蓝机位于`ArtReview/GroundMech_A4B2`，真实创建新ABP实例；Ground角色路由和原起点保留，未启动游玩。

所有报告、输入/FBX哈希、回退与操作步骤见[交付说明](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/UE_DELIVERY.md)。旧全局迁移校验器因无关旧装配假设抛出StopIteration，本轮改用限定范围合同检查和共享导入器回读，不宣称全项目校验通过。UE材质采用三档法线光照，未烘焙Blender AO；效果、PIE完整玩法、远端动画和联机仍待用户验收。

### 此前：枪体按参考补细节（2026-10-23）

用户追加“枪的细节再多一点，跟参考图对齐”。读取批准的A_v4整机及上半身参考作为制作输入，以其中的枪口套筒、前端护圈、凸起顶盖、叠装侧甲和镂空下托架为依据修订；不读取输出图像进行视觉测试。

- 枪身前端补独立八角装甲护圈，环绕原圆形枪管接口；后上方补凸起顶盖及安装连接扣。
- 侧面补队色护甲，乳白片改为叠装在护甲上的前部切角面板，形成主体、侧甲、面板的实体层次。
- 枪口重建三段台阶、内收环槽、端面倒角和内凹枪膛；橙色内芯压入枪膛底部支承。
- 下托架改成具有宽折面的镂空梯形框；此前收拢的后侧臂架保持。

左右与红蓝实例同步；两枪轴向总长仍为2.0175 m（原长1.345 m的1.5倍），前端平面和枪口挂点保持。炮壳部件继续绑定 `GunArm`，圆管部件继续绑定 `Gun`。新增10个主体分件、替换20个既有主体分件；28处装配共同内部点、闭合网格、退化面、UV和单骨骼权重核对通过，非枪体几何、骨架、驱动器、动作列表及枪口局部矩阵未变。当前主体187分件、24,654三角面。

见[枪体细节记录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/gun_detail_revision.json)，包含完整改前备份及结构检查结果。当前源文件SHA256：`2fc8f6f8f4412404fe7b73bc59bd91340481c50525fbea66e33ef0d1472dbe28`；Blender停留在可编辑枪体近景。当前外观与参考还原度由用户审核，不把实体连接或真实渲染记为B通过。

### 此前：六张标注修订（2026-10-23）

用户提供六张带文字的模型截图，授权具体几何修改，解除此前缺少可执行视觉反馈的停滞；这不等于允许助手对输出图片做视觉测试，也不等于B放行。输入截图、批准的A_v4和侧面参考仅作为制作依据。

1. 双枪从后安装端沿枪轴加长50%：原长1.345 m、新长2.0175 m；截面和枪口内／外半径保留。四个红蓝实例枪口Empty同步向前0.6725 m，枪骨轴心和既有驱动器保持。
2. 枪体整理为左右独立可编辑 `Weapon_Module_R/L` 集合，增加内收安装接头；炮壳与肘护套有明确分件，炮壳仍绑定 `GunArm_R/L`，圆枪管仍绑定 `Gun_R/L`，避免把炮壳随枪管自转。
3. 枪侧及上臂矩形片改为贴合外壳斜面的切角实体；后侧支架收向肘部，下方枪架贴近枪身。
4. 髋、膝、胫侧护甲按A_v4侧面轮廓重建厚度、倾角和包覆面；内膝件与安装支座收短，消除原长外伸支杆。
5. 脚背上层靴甲和下层队色甲分开，最小高度间隙0.06 m；新内收深色支承连接脚踝、脚掌、双趾与后跟。此前的一体切角后跟与收尖双趾轮廓保留，旧大块队色连接体由本次分层结构取代。

修改前保存了包含未保存状态的完整备份，路径见[本轮修改记录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/annotation_revision.json)。56个既有主体分件更新、4个主体分件新增；红蓝共享几何同步，17骨骼、动作列表、驱动器及未涉及网格摘要不变。主体177分件、21,550三角面，轮廓壳19,018三角面；受影响网格闭合、无退化三角形、UV及单骨骼权重1检查通过，40处装配存在共同内部点。

源文件SHA256：`5a7aaa3b327219ccae8af14148eadb3fe54b80339aca5de515e955017925f5b3`。八张实际模型预览与该源文件哈希一致，新增 `GroundMech_A4B2_07_WeaponAssembly` 和 `GroundMech_A4B2_08_LegAssembly`；当前Blender停留在可编辑整机视角。输出图像未由助手读取测试，外观与还原度待用户核验；此次未覆盖原包动画、蓝图或正式Ground资源。

### Blender B_v2：按用户反馈修订中

用户连续反馈“不一致”“不如你来找不同然后总结一下，然后改”“浮空了”“还是有很大差异”，并追加胸部两块在参考中是独立部件、当前只像两条线。本次任务据此进行差异修订，不转交用户重新诊断，也不覆盖已批准 A_v4。反馈原图和版本说明保存在 [revision_notes](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/revision_notes.json)。

- 重建连续胸腔支撑体，胸甲背面与底座搭接；`Chest_IndependentArmor_R/L` 是两套独立封闭网格，具有实体侧壁和宽倒角，不使用原平面描线替代部件。
- 肩甲、髋甲、腿甲按前面宽度与侧面深度重建截面，恢复折面厚度；修正肘轴、膝盖护片、髋部侧甲、脚背与背部装配。
- 保持 17 骨骼名称和层级、单骨骼刚性权重、双枪后坐与四挂点。枪管本地 X 轴重新对准实际炮管中心线，避免源枪管自转产生偏心公转；未改写源包骨架或动画文件。
- 当前材质继续采用已审蓝／红队色、固定乳白／米砂／机构色及三档明暗。真实模型预览由 Blender 生成，未用参考图替代模型产物。

技术数据见 [geometry_report](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/geometry_report.json)、[绑定与控制检查](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/blender_data_checks.json)和[渲染清单](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/render_manifest.json)。这些检查不代表“一比一完全一致”；B_v2 未放行、未提升为正式 UE 资源。B_v1 中保留原待机预览；B_v2 当前几何修订不能沿用 B_v1 动画检查作为本版本全部动作通过的证据。

### B_v2 脚后跟连接修订（2026-10-23）

用户在 Blender 圈出后跟与前脚掌断开。实体数据确认 `Foot_BlueBridge_R/L` 尾端位于脚部局部 Y=-0.10 m，后跟始于 Y=0.18 m，间隔0.28 m。本轮只为连接块添加封闭的收束尾段至 Y=0.32 m，与后跟搭接0.14 m；保留原脚踝、脚趾与后跟外形。

修改直接应用于当前打开的 B_v2，修改前将包含用户未保存状态的场景另存备份。红蓝实例共用更新后的网格；左右连接块分别继续单骨骼、权重1绑定 `Foot_R/L`，脚趾仍沿用 `FootEnd_R/L`。四个后跟与连接块均存在修改器计算后的内部共同点；改动件无非流形边或退化面，其他网格摘要与骨架契约保持。主体更新为18,630三角面，描边16,538三角面。

保存路径、备份和数据结果见 [heel_connection_revision](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/heel_connection_revision.json)。Blender 定位脚部近景供用户审核，未进行助手输出读图测试；技术搭接不代表 B 审核通过或完整动画适配完成。生成脚本已同步相同截面，重建不会恢复旧空档。

### B_v2 背部与脚部轮廓修订（同日后续）

- 背部：将横跨散热片的中央壳体收为下方支撑；深色侧壳移至队色背甲外侧，按 A_v4 背面坐标重建两块队色护甲和三段深灰背板，补充顶部圆筒后端圆环与橙色中心。新旧倒角宽度已同步，受影响部件无退化面。见[背部装配记录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/back_assembly_revision.json)。
- 用户圈出后跟两块方块并标注“这两块东西很丑”：每脚替换为一个连续、收窄的切角楔形后跟，宽度0.94→0.74 m、向后延伸0.64→0.48 m、高度0.36→0.31 m；连接块调整为压入后跟0.26 m。旧后跟分件从当前模型移除，改前 blend 备份保留。见[一体后跟记录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/integrated_heel_revision.json)。
- 用户圈出前脚趾追加“这里也是，方方正正的不好看”：四个前脚趾保留双趾关系，前端宽度0.55→0.32 m、前伸1.65→1.55 m，增加平面切角和倾斜端面；米色嵌板重建为贴合新斜面的薄实体。四处趾根搭接和四处嵌板贴合均有修改器计算后共同内部点。见[脚趾记录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/tapered_toes_revision.json)。

该阶段主体173分件、20,082三角面，描边17,742三角面；17骨骼和原归属不变，红蓝共用网格同步。当时新增后跟／脚趾近景场景，并将Blender定位脚尖；当前版以上方六处修订记录为准。结构检查、实际预览生成与用户美术验收分开记录，没有助手输出读图测试或与A_v4完全一致的结论。

本阶段随后核对两张A_v4源图与批准哈希一致、当时blend与六张预览同版，并曾等待用户视觉反馈。用户随后提供上述六张明确标注，已据此恢复制作；没有重新要求例外读图许可。当前事实见[完成条件记录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/fidelity_completion_audit.json)，不把结构核对当作B放行。

### Blender B_v1 历史技术事实（外观已被拒绝）

源文件 `Production_A4_B_v1/GroundMech_A4_Editable_B_v1.blend` 已在当前 Blender 打开，默认场景 `GroundMech_A4_01_TeamReview`，相机视图和材质预览已就位。Construction 保留分件修改器，ThreeViews 提供红蓝两组三视图，SourceIdle 提供独立待机动作。图纸打包，源高模和源动作保留。

实际主体高度4.88 m、宽4.764 m、深约2.617 m；17骨骼保留源名和层级，枪管轴向与新枪管对齐，参考位置按折腿设计改变。方形炮壳绑定 GunArm，圆管绑定 Gun，适应源待机包含的枪管自转。骨骼名相同不等于全部动画可直接复用：当前只有原待机通过相对动作与双段腿 IK 转移，未改写原文件，仍须为 UE 完整动作建立适配。

数据证据包括 [geometry_report](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1/geometry_report.json)、[绑定与控制检查](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1/blender_data_checks.json)、[待机适配](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1/source_idle_adapter.json)、[真实渲染清单](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1/render_manifest.json)。未进行助手输出读图测试，没有视觉完全一致的结论。

原包及候选结构记录：[源契约](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/ue_contracts.json)、[插槽与源蓝图](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/staged_asset_contract.json)、[动画适配](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/animation_adapter.json)。

编译引擎为 `D:/UnrealEngine-5.7`，目标 `GuLiStrikeEditor Win64 Development -WaitMutex -NoHotReloadFromIDE`，两轮构建成功；[最终构建日志](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/EditorBuild.log)记录最后一轮29.46秒。项目与引擎 [BuildId](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/native_build_ids.json)同为 `d4757e21-eaef-492e-b079-c8c0056c493d`。不据此认定运行或视觉通过。

`promote_catalog.py` 已修正字典语法，尚未执行目录提升；`install_player_adapter.py` 尚未执行。源表 Model 4001 和正式 `BP_GroundMech_Light` 仍是旧模型引用，不能称已完成替换。先前一次导出管线执行没有完成本任务的目录提升。

后续复用 `/Game/Maps/LVL_GroundMech_Demo` 或 `/Game/Maps/LVL_GroundMech_FireReview`，准备 Ground 出生、左右枪口／喷口和升级入口后保存并读取相关实体；目前该新模型运行交付场景尚未就绪。最新用户明确要求按两张 A_v4 制作 Blender，本轮优先完成该成品候选，后续仍须补齐原模型与最终风格化资产的运行接入，不启动无关自动化测试。

原角色实际站立高度约488.074 cm、胶囊半高244.037 cm、半径150 cm，以现有 CDO 为准。本轮 Blender 高度约488 cm，未改正式碰撞、伤害、射速或玩法；新骨架参考姿态已制作，完整动画适配与正式引擎接入继续单独记录。
