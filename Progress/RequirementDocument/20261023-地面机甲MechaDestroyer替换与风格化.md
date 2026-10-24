---
schema: guli-progress/v1
id: REQ-20261023-001
work_id: WORK-20261023-001
kind: requirement
role: root
title: 地面机甲 MechaDestroyer 替换与风格化
areas: [art, combat]
categories: [art, gameplay]
status: approved
verification: partial
created: '2026-10-23'
updated: '2026-10-24'
summary: B_v2已导入；用户追加越界瞄准停止跟随、浮空右键转向及W与右键转速同步，保留每侧75°机械限位和现有移动数值。
next_action: 用户在LVL_CommanderMassPrototype以Ground复验限位稳定、浮空右键和W同速转向。
relations:
  development: DEV-20261023-001
status_note: A_v4已放行，B_v2造型仍待用户验收。本轮四表和原生链路已实施，编译、冷加载资产读取和地图实体核对通过；未启动PIE或读图验收。实际操作、联机和视觉待用户确认。
---

# 地面机甲 MechaDestroyer 替换与风格化

2026-10-24最新三项反馈：达到75°后继续移动鼠标，枪口不再尝试追随越界目标；浮空时右键允许改变机体朝向；W转向与右键转向同步，避免瞬间转正。实施为先裁剪瞄准角、开放空中右键，并让W/右键共用转身曲线和Gait倍率；移动与空中保持对应动画。[本轮交付与复验](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/AIM_TURN_DELIVERY.md)。本次覆盖旧的浮空禁止转向和移动按固定1080°/秒追向规则。

2026-10-24追加：“双手左右可摆动角度限制在150°，现在看着是180°”。按每只枪臂相对机体正前方左右各75°实施，继续使用Mobility表的GunYawLimit；达到边界后沿最终枪口发射。[限位修订及验收入口](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/ARM_YAW_DELIVERY.md)。

2026-10-24最新确认：W按住持续跟随镜头水平朝向，W+A/D仍面朝镜头斜移，W不触发越肩；W+右键以镜头水平角优先。走/跑由720/1440改216/432，加速/制动由2880改864，移动转速540改1080，左右转身蒙太奇1改2倍并同步根旋转；标定速度、步幅范围、其他动作和技能表保持。详见[本轮交付](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/W_FOLLOW_DELIVERY.md)。下列首版数字与朝向约束以本次修订为准。

## 当前批准范围：机动、脚部IK与右键控制

用户完整批准2026-10-24方案：GuLiStrikeMech.xlsx新增Mobility、Gait、GroundIK、Camera；步行720/冲刺1440，加速制动2880。真实脚底测点、双腿IK、有限骨盆补偿和支撑锁定兼容Mass平台；按标定速度联动步幅和步频。普通鼠标自由转镜，右键才改变机体朝向；指定左右转身蒙太奇按根曲线分步推进，15°开始、5°停止。枪臂限角后仍沿实际枪轴射击，并提示实际射向。Ground输入/姿态增加朝向与转身状态，同步网络版本、恢复及配置一致性。

右键构图为根相对(-296.69,240.27,332.83)cm，Pitch -9.65°/Yaw -3.70°，FOV90，Arm362.96，SocketOffset(0,220.63,275.89)。切换保持当前观察方向、0.2秒混合；普通远距离镜头及缩放保留。UI优先消费鼠标，右键取消不触发瞄准。沿用BP_GroundMech_Light、ABP_GroundMech_A4B2、原包、骨架和阵营材质。助手只做静态、资产和场景实体核对，不启动PIE、不截图或目检；用户负责效果验收。

实施、场景及验收入口见[当前交付](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/GROUND_MOBILITY_DELIVERY.md)。以下为此前批准范围，镜头/朝向行为以本节为准。

2026-10-24追加已批准实施范围：记录的相机位置为`(-988.06,406.39,409.14)`cm，旋转Pitch=-14.85°、Yaw=-3.90°，FOV90；鼠标转镜、中心准星、地面A/D横移。修复Walk/Run混合空间，指定Idle、Strafe_L/R、Jump_Start→Jump_Loop；双枪使用原包派生Shoot各自播放，保留枪管旋转并增加各枪轴5cm/0.15秒后坐。枪口、特效和弹丸对准同一点，原总射速伤害与Ground玩法保持；建造、传送选点和菜单期间释放鼠标并暂停转镜/开火。助手完成结构和姿势数据核对，PIE与视觉效果由用户验收。实施证据见同名开发文档。

## 目标与顺序

来源为用户提供的 `D:\BaiduNetdiskDownload\UE4UE5可操控第三人称射击机甲机器人带特效MechaDestroyer.zip`。对象是玩家 Ground 席位、稳定 ModelId 4001，不是 WM01 重防号。

用户要求先解压、导入 UE5 并跑通原模型，再进行风格化；模型制作前先交付参考图和正／侧／背三视图，成品放入已打开的 Blender 审核。完整保留 Ground 的控制、移动、冲刺、火箭跳、建造、伤害与升级、相机、传送、输入锁和网络路径。

用户曾明确“两张 A_v4 图作为本次建模定稿，在blender中一比一建模，完全一致后交付”，据此先完成Blender制作。建模以重新附加的两张原图为基础，叠加下方最新六张标注；双枪加长50%为用户明确的新尺寸方向，无需重新询问A批准。原图与[A决定及哈希](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1/approval_A.json)保留。此前的原模型运行接入未完成，不以新网格交付补记完成。

## 造型与表现

- 三渲二、三档明暗、简单色块，降低上身网格和机械细节密度，配色按[全项目规范](GuLiStrike美术规范.md)且指定区域随阵营改变。
- 下半身参考实际原玩家 Ground 的 `Style_v9/Meshes/SK_Legs`。
- 最新修订：“太臃肿了，在原来的基础上调”“只改腿折角就好了，其他不要动”。A_v2 的上身、装甲尺寸、腿段厚度／长度、脚掌和站宽保持，只改前后关节折角；A_v3 体量方向撤回。
- 保留原 MechaDestroyer 骨架、动画、蓝图资源可用。开火与喷气挂点明确命名和归属，兼容原玩法。
- 用户指定双枪交替开火：右／左逐发交替，总射速和单发伤害不翻倍。

## 验收

- [x] 已产出 [A_v4 效果图与三视图](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Reference_A_v4/REVIEW.md)。
- [x] 用户确认两张 A_v4 为本次建模定稿。
- [ ] 原包资源与原 Ground 全部玩法运行接通。
- [x] 制作真实可编辑 Blender B_v1、三档明暗／阵营材质、分件刚性绑定、枪口／喷口与原待机重定向预览，产出实际模型红蓝三视图。
- [ ] 用户确认 B 与定稿的视觉一致性，按反馈修正剩余偏差。
- [x] UE中14段原动画完成新骨架适配，2混合空间、2蒙太奇与动画蓝图资源结构/编译核对通过；实际效果另验。
- [x] 按用户明确的当前版导入授权，完成正式资源接入及阵营换色注册，不补记B视觉通过。
- [x] 在对应Ground地图保存左右交替、喷气与原玩法入口，配置实体核对通过。

原生构建已获用户“允许保存、编译并重启 UE”授权；该授权不等于美术通过。本次模型修订中，用户明确要求“不如你来找不同然后总结一下，然后改”，据此对本资产开展差异修订；用户最终美术验收仍独立记录，不把结构检查或助手判断登记为 B 通过。

最新用户明确“导入进UE编辑器中，并核对蓝图、动画等资源是否正常（效果我来验）”，覆盖此前等待B放行才导入的顺序。当前已安装新版，原包与旧Ground资源保留。2026-10-24按“PIE已开，我并没有在控制机甲”反馈修正会话入口，当前使用`/Game/Maps/LVL_CommanderMassPrototype`和本地Ground席位，已读回实际占有新模型/新动画实例；FireReview只保留编辑器展示用途。[UE交付说明](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/UE_DELIVERY.md)记录准确入口和验证边界，完整玩法及视觉仍待用户。

当前修订入口：[Blender B_v2](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/REVIEW.md)。[B_v1](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1/REVIEW.md)保留历史，用户已反馈“不一致”。B_v2 的两个胸部面板必须是独立实体装甲，具有侧壁、倒角和贴合的安装底座；不能只保留平面描线，也不能再次浮空。

用户追加“脚下这两部分目前是分开的，需要处理下”，指定后跟与前脚掌间的装配断开。左右脚均应具有连续实体连接，保留可编辑分件、已有脚骨与脚趾骨归属；本次结构修复不重新设计全身。按最新 AGENTS.md，本轮只核对几何与绑定数据，提供 Blender 实物给用户检查外观。

随后用户在后跟附图标注“这两块东西很丑”，并圈出前脚趾追加“这里也是，方方正正的不好看”。据此将外伸方块收成切角楔形：后跟为贴合脚踝的单体，前端保留 A_v4 双趾并收尖、斜切，固定米色面板贴合新表面；保持此前实体连接与脚部骨骼接口。

## 最新六张标注的制作约束

- 机枪轴向加长50%，维持口径；双侧同步，枪口挂点随实际枪口前移。
- 机枪作为独立部件，保留与上臂、肘护套之间的安装关系和清楚分界，不融合成一块。
- 枪侧和上臂矩形面改为适配模型外壳的切角形面板。
- 后侧臂架收拢，降低外伸程度。
- 髋、膝、胫部侧片与内侧膝甲按A_v4参考的装配、厚度和轮廓修改。
- 脚部装甲、支承、脚掌与后跟分件明确；保留实体支承连接，不能恢复此前浮空断开的问题。

输入原图、原话、哈希与实施结果见[六处修改记录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/annotation_revision.json)。读取用户标注和批准参考用于制作，输出预览供用户自行核验；未授予助手输出图像测试许可，未授予B最终放行。

用户随后要求“枪的细节再多一点，跟参考图对齐”。在当前1.5倍长度和独立武器结构上，依据批准A_v4补足前端护圈、台阶枪口、凸起顶盖、侧面装甲层次和镂空下托架，保留原色库、三档明暗、枪口挂点和骨骼职责；此次不重做非枪体部位。实施见[枪体细节记录](../../ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/gun_detail_revision.json)，外观仍由用户在Blender核验。
