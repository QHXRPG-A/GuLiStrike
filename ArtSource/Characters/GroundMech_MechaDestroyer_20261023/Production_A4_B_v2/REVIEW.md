> 2026-10-26 本会话已按用户指令完成归档。以下保留当时版本及审核事实，最终方向/时长与清理说明见[完成归档](../../../../Progress/Archive/20261026-地面机甲镜头方向闪现与会话完成归档.md)；不补记未执行的视觉验收。

# 地面机甲 A_v4 — Blender B_v2 修订中

A_v4 为基础，叠加用户本轮双枪加长50%等六处明确修订。B_v1 已被用户指出“不一致”，当前 B_v2 继续修订，**未通过 B 审核，未宣称三视图无差异**。

当前源文件：[GroundMech_A4B2_Editable_B_v2.blend](GroundMech_A4B2_Editable_B_v2.blend)。历史 B_v1 和原 MechaDestroyer 资源保留。

## 当前：UE导入已授权并完成

用户明确“导入进UE编辑器中，并核对蓝图、动画等资源是否正常（效果我来验）”。当前版已接入正式Ground与FireReview，14段动画重定向为新骨架副本，配套新动画蓝图，原资源保留。蓝图/引用/骨骼与关键帧核对通过；外观终验仍待用户，未启动PIE或读图测试。

2026-10-24更正：实际试玩入口为`/Game/Maps/LVL_CommanderMassPrototype`，本地PIE席位Ground，已读回当前玩家实际占有新SK/ABP实例。`LVL_GroundMech_FireReview`保留红蓝编辑器展示机，当前本地会话启动器不支持直接在该图PIE。完整说明见[UE交付说明](UE_Export/UE_DELIVERY.md)。Blender原可编辑源保留。

## 此前：按参考补充枪体细节

用户要求“枪的细节再多一点，跟参考图对齐”。以A_v4整机与上半身参考中的武器结构为依据，新增前端八角装甲护圈、独立凸起顶盖与连接扣、侧面队色护甲；乳白片缩为前部切角面板并叠装在侧甲上。枪口增加三段台阶、内收环槽、端面倒角和内凹枪膛，橙色内芯落在枪膛底座上；下托架改为有宽折面的镂空梯形框。

左右枪和两队实例同步。保留1.5倍总长、原枪口直径和挂点、炮壳／旋转枪管的既有骨骼归属；此前收拢的后臂架及非枪体几何保持。见[枪体细节修改记录](gun_detail_revision.json)，含改前完整备份与28处实体连接核对。当前Blender定位可编辑枪体近景；外观由用户审核。

![当前枪体细节](Previews/07_WeaponAssembly.png)

## 此前六张标注修订

- 双枪沿枪轴从1.345 m加长至2.0175 m（1.5倍），口径保持；枪口插槽向前0.6725 m，双侧和红蓝实例同步。
- 枪体进入独立可编辑 `Weapon_Module_R/L` 集合，后方增加内收的安装接头，外壳与肘部护套分开；壳体仍绑定 `GunArm`，旋转枪管仍绑定 `Gun`。
- 枪侧及上臂矩形块改为贴合外壳斜面的切角实体片；后侧支架收向肘部，下部枪架贴近枪体。
- 髋、膝、胫部侧甲按参考轮廓重建厚度、斜角和包覆面；内侧膝甲与支座收短。
- 脚背上层靴甲与下层队色甲之间留出至少0.06 m的高度分层，深色支承连接脚踝、脚掌、双趾和后跟；后跟与前趾的既有收尖轮廓保留。

见[本轮实体修改与检查](annotation_revision.json)。修改前保留含未保存状态的完整备份；骨骼、驱动器及其他网格摘要未变。外观由用户在 Blender 审核。

![本轮枪体与安装结构](Previews/07_WeaponAssembly.png)

![本轮腿部与脚部装配](Previews/08_LegAssembly.png)

## 此前脚部轮廓修订

用户先指出后跟断开，随后指出后跟两块方块难看、前脚趾也过于方正。此前改为每脚一个切角楔形后跟，宽度0.94→0.74 m，向后延伸0.64→0.48 m；当时连接块与后跟搭接0.26 m，现由上述内收深色支承接续连接。前脚掌双趾前端宽度0.55→0.32 m、前伸1.65→1.55 m，前端斜切并收尖，米色嵌板贴合新斜面。

[一体后跟记录](integrated_heel_revision.json)与[楔形脚趾记录](tapered_toes_revision.json)包含改前备份、闭合几何、刚性权重及装配共同内部点。后跟和连接块仍绑定 `Foot_R/L`，脚趾及嵌板仍绑定 `FootEnd_R/L`；红蓝共用网格同步。原[断开修复](heel_connection_revision.json)保留历史，其后跟分件已被本版替代。没有助手输出读图测试，尚未取得 B 放行。

![当前一体楔形后跟](Previews/05_HeelAssembly.png)

![当前收尖双趾](Previews/06_ToeAssembly.png)

## 背部装配修订

中央支撑收至散热片下方，侧壳移至队色背甲外侧；按 A_v4 背面轮廓重建三段深灰背板和两块队色护甲，并补上顶部圆筒后端的圆环与橙色中心。数据与备份见[背部装配记录](back_assembly_revision.json)。参考还原度仍待用户审核。

## 本次胸甲修订

用户指出胸前两块在参考中是独立部件、当前只像两条线。已改为 `GMA4B2_Chest_IndependentArmor_R/L` 两个独立封闭网格，具有侧壁和宽倒角；名义厚度0.18 m，前面高出安装底座0.117 m，背面压入底座，仍刚性绑定 `cockpit`。

![实际 Blender 胸甲装配](Previews/04_ChestAssembly.png)

## Blender 入口

- `GroundMech_A4B2_Construction`：实际可编辑蓝方分件、修改器、骨架与挂点；胸甲可以分别选择。
- `GroundMech_A4B2_01_TeamReview`：同几何的蓝／红实例。
- `GroundMech_A4B2_02_ThreeViews`：正／侧／背两队六视图。
- `GroundMech_A4B2_03_SingleInspect`：整机三分之四视角。
- `GroundMech_A4B2_04_ChestAssembly`：胸甲近景，仍为实际三维模型渲染。
- `GroundMech_A4B2_05_HeelAssembly`：一体切角后跟近景。
- `GroundMech_A4B2_06_ToeAssembly`：收尖双趾与嵌板近景。
- `GroundMech_A4B2_07_WeaponAssembly`：本轮加长枪体、面板、独立安装接头。
- `GroundMech_A4B2_08_LegAssembly`：本轮腿甲、内侧膝甲和脚部装配。

![实际 Blender 三视图](Previews/02_BlueRed_ThreeViews.png)

[对照页](review.html)并列展示批准参考和本次模型；[反馈与变更记录](revision_notes.json)保留用户原话、附件哈希及本版本剩余事项。

## 数据与边界

当前主体 187 分件、24,654 三角面；轮廓壳 22,122 三角面；骨架 17 骨骼。高度 4.8800 m。数据随本工作版本继续修订，以 [geometry_report](geometry_report.json) 为准。

当前闭合网格与UV统计见 geometry_report，受影响部件的绑定与装配检查见上述各次修订记录。[历史控制检查](blender_data_checks.json)记录之前阶段的三档节点、双枪后坐、瞄准和插槽核对，不作为当前整机全动作验收。源文件 SHA256：`2fc8f6f8f4412404fe7b73bc59bd91340481c50525fbea66e33ef0d1472dbe28`。

这些技术检查不等于外观通过。B_v2 的完整动画适配、正式 UE 资源提升和 Ground 全玩法运行仍未完成；原包动画和蓝图未被本次模型修订覆盖。B_v1 的待机预览不是 B_v2 全动作兼容证明。
