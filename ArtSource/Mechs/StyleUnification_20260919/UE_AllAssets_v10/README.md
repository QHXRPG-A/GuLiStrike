# 机甲与武器整批同步 · UE 交付总结

本批已制作的 **8 项成品全部进入源码版 UE5.7**，共用 **10 个独立网格**。本轮补齐 Mecha_01、Mecha_02、FireWeapon_01、MissileWeapon_01、Missile_01；此前已同步的轻型机甲、SpiderMech 和玩家机枪继续使用最新资源。

用户本轮明确要求“把已经制作了的资源都同步至UE，然后做一波总结”，据此完成整批导入。范围为这两组机甲的八项成品，已退出制作范围的独立驾驶舱和被否定的 Spider 减面版不计入。

[查看全部实际 UE 画面](Review_UE_All_v10.html) · [完整资源与哈希清单](delivery_manifest.json) · [上一阶段玩家接入说明](../UE_StyleSync_v9/README.md)

## 资源用途与当前版本

| 成品 | 当前制作内容 | UE 使用状态 | 主体 / 描边三角面 |
|---|---|---|---:|
| 轻型机甲 Mech_Lightest | v9：赭金弧面封舱、贴壳排气底座、内凹肩侧散热舱、对称灰蓝腿甲；线稿与三档明暗 | 当前 Ground 玩家；原动画、Enhanced Input 和建造入口保留 | 20,038 / 11,325 |
| SpiderMech | 原完整网格、四片完整暗红上腿甲、对称配色、线稿与三档明暗 | 独立风格蓝图，继承原 Spider 蓝图与动画；Demo 内有参照演员 | 839,778 / 839,778 |
| Mecha_01 | v1 已制作版本：低饱和灰蓝/黑灰、补充舱盖和关节细节；连续受光 | 新建独立展示蓝图，保留原骨架、挂点与待机动画；原行走姿态已验证 | 3,982 / 0 |
| Mecha_02 | v1 已制作版本：金黄/黑灰、规整舱盖和关节；连续受光 | 新建独立展示蓝图，保留原骨架、挂点与待机动画；原行走姿态已验证 | 5,964 / 0 |
| FireWeapon_01 | 三管、灰蓝/黑灰、线稿与三档明暗 | 独立可装配武器网格及展示蓝图；保留 Yaw/Pitch 骨骼和原挂点 | 1,184 / 484 |
| MissileWeapon_01 | 灰红/灰结构、双列弹架、线稿与三档明暗 | 独立可装配武器网格及展示蓝图；保留 Yaw/Pitch 骨骼和原挂点 | 7,181 / 3,202 |
| Machinegun · Lv1 | 赭黄/深青绿、琥珀灯、线稿与三档明暗 | 已是轻型玩家右侧机枪，同一网格在展示关卡单独摆放 | 569 / 162 |
| Missile_01 | 已制作的灰白弹体、深灰分界和尾翼；连续受光 | 独立静态网格及展示蓝图 | 772 / 0 |

轻型总面数已经包含 Machinegun，统计独立网格时不重复相加。Spider 遵循“不再减面”，没有执行新的简化。

这批三套武器中，**Machinegun Lv1 属于当前轻型玩家装配**；三管炮和导弹武器来自 MechaController 的独立武器模块，本轮未将其装到玩家或 Spider 上。独立 Missile_01 是弹体模型。资源导入没有增加射击、伤害、换枪或投射物逻辑；也没有改变现有战争机器导弹系统。

五项明确要求补线稿的资源维持三档明暗。Mecha_01、Mecha_02、独立导弹按现有成品保留连续受光材质，没有把它们描述为已完成相同的三渲二升级。

## UE 入口

| 用途 | 资源路径 |
|---|---|
| 完整八项展示关卡 | `/Game/GuLiStrike/Mechs/StyleShowcase/LVL_MechAsset_Showcase` |
| 可操控的 Ground 测试地图 | `/Game/Maps/LVL_GroundMech_Demo` |
| 轻型玩家蓝图 | `/Game/GuLiStrike/GroundMech/BP_GroundMech_Light` |
| 轻型四部件与玩家机枪 | `/Game/GuLiStrike/GroundMech/Style_v9/Meshes` |
| Spider | `/Game/GuLiStrike/Mechs/SpiderMech/BP_SpiderMech_Styled` |
| Mecha 01 | `/Game/GuLiStrike/Mechs/Mecha_01/BP_Mecha_01_Styled` |
| Mecha 02 | `/Game/GuLiStrike/Mechs/Mecha_02/BP_Mecha_02_Styled` |
| 三管炮 | `/Game/GuLiStrike/Weapons/MechModules/FireWeapon_01/BP_FireWeapon_01_Styled` |
| 导弹武器 | `/Game/GuLiStrike/Weapons/MechModules/MissileWeapon_01/BP_MissileWeapon_01_Styled` |
| 独立导弹 | `/Game/GuLiStrike/Weapons/MechProjectiles/Missile_01/BP_Missile_01_Styled` |

新资源各自包含 `Meshes`、`Materials`、`Textures`。原商城网格、骨架、动画和挂点保留为来源；没有覆盖商城资产。新增五个展示蓝图不带玩家或武器业务逻辑，展示组件关闭碰撞。骨骼网格保留来源物理资产，具体玩法碰撞仍需接入方配置。

展示关卡为独立中性工作室环境，全部 Actor 缩放为 1。轻型玩家仍在组件内按 2.0512617 放大，以约 7.48m 的战争机器站立高度为基准；其余对象按制作时实际厘米尺寸摆放。单件截图会调整相机，因此不能用不同单件图片的像素高度比较尺寸。Demo 的环境、灯光、曝光和玩家配置没有在本轮改动。

## 制作与迁移过程

1. 从实际 UE 源网格、原骨架和挂点制作，先交效果参考与三视图；红蓝按反馈降低饱和度，轻型驾驶区封闭。
2. Spider 早期减面出现碎面，按最新指令恢复完整原网格，只处理色块、材质及线稿。
3. 为指定五项增加内线、外轮廓与三档明暗；轻型修正对称染色。Spider 最终使用整片暗红装甲，轻型最终保留赭金头甲和灰蓝腿甲。
4. 轻型头部重做弧度、排气叶片、肩侧散热舱；v9 补齐排气口贴壳底座，解决浮空。
5. 先同步轻型与 Spider，更新 Ground 玩家模型及 Demo 参照，并修复动画类默认值缓存导致的根位移配置问题。
6. 本轮补齐另外五项，制作八项展示关卡、11 张 UE 实际截图，统一清单与归档。

## 本轮实际验证

- [五份 FBX 保存回读](fbx_readback.json)：厘米尺寸、主体/描边面数、材料槽、骨骼数量与权重通过。
- [UE 导入结果](ue_import.json)：来源骨架与父子关系保留；新网格参考姿态最大位移误差约 0.000236cm，尺寸误差小于 0.05cm。
- [当前编辑器回读](live_assets_readback.json)：五个新蓝图引用正确并编译通过。两台 Mecha 的左右武器/视线挂点、两套武器的 Pitch 挂点均从原骨架保留，无额外修改源骨架。
- [独立源码编辑器展示与姿态记录](ue_showcase.json)：8 项成品引用正确，8 项相关蓝图编译通过；Ground 新动画实例在编译前即为 IgnoreRootMotion。Mecha_01、Mecha_02 的待机和行走姿态与原网格对照，最大骨骼位置差小于 0.00009cm，比例稳定。行走样本相对待机有约 44cm 的骨骼运动。
- [展示关卡重新加载](ue_gallery_saved_readback.json)：八个成品 Actor 均已序列化，单位缩放、可见，Mecha 待机引用保留；原模型对照用的临时演员已移除。
- 11 张实际 UE 图包括整批总览、8 张单件和两台 Mecha 行走姿态。首次部分截图取景过近，已扩大范围并排除编辑器相机可视网格对包围盒的干扰后重新拍摄。

这是资产同步与展示检查。本轮没有 C++、Build.cs 或原生插件变更，未运行原生构建；未重跑完整玩家/联机回归，也未做性能压测。先前 Ground 根位移及双端同步结论仍见[v9记录](../UE_StyleSync_v9/README.md)，不冒充本轮新测。

已知边界：Spider 含描边共约 168 万三角面；轻型头甲端部近景仍能见到少量描边交叠点，沿用已同步 v9，本次不新增造型修订。展示关卡保存时，项目现有 `GuLiFlightNavigationWorldValidator` 对非飞行展示地图返回 NotValidated，产生 handled ensure；地图保存和独立重载成功，但不将项目全量 Data Validation 写为通过。最终外观仍以用户反馈为准。

## 来源与复现

完整制作源：[Mechs_VentMount_v9.blend](../Production_v9_VentMount/Mechs_VentMount_v9.blend)。SHA256：`6caa1f5569d1c929b30f924c71f9b06144208ed340c437f3194a3399ec46912b`。本轮没有重写该 Blender 文件。

- [导出脚本](../../../../Scripts/Blender/export_remaining_mech_assets_v10.py)、[FBX 回读脚本](../../../../Scripts/Blender/readback_remaining_mech_assets_v10.py)、[导出报告](export_report.json)。
- [UE 导入脚本](../../../../Scripts/GroundMech/import_remaining_mech_assets_v10.py)，参数 `-MechAllAssetsImportWorker`；只写本批新建目录。
- [展示与实际渲染](../../../../Scripts/GroundMech/showcase_all_mech_assets_v10.py)，参数 `-MechAllAssetsPreviewWorker`；[展示保存回读](../../../../Scripts/GroundMech/readback_mech_showcase_v10.py)。
- UE 执行环境：`D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.exe`；回读使用同目录源码 `UnrealEditor-Cmd.exe`。
- [整批增量归档](../../../../Progress/Archive/20260919-机甲与武器整批同步UE及总结.md)。各次历史反馈、被替代版本和上一阶段回退资源继续保留。
