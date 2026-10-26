> 2026-10-26 本会话已按用户指令完成归档。以下保留当时版本及审核事实，最终方向/时长与清理说明见[完成归档](../../../../Progress/Archive/20261026-地面机甲镜头方向闪现与会话完成归档.md)；不补记未执行的视觉验收。

# Ground Mech A_v4 — Blender B_v1

2026-10-23。本版是按已批准 A_v4 制作的真实、可编辑 Blender 网格候选。A 已通过，B 待用户核对；结构检查不代表已达到视觉完全一致。

- [打开 Blender 源文件](GroundMech_A4_Editable_B_v1.blend)
- [参考与实际模型对照页](review.html)
- [实际红蓝效果图](Previews/01_BlueRed_Hero.png)
- [实际红蓝正／侧／背三视图](Previews/02_BlueRed_ThreeViews.png)
- [实际单机检查视角](Previews/03_Blue_Inspect.png)
- [用户定稿效果图](References/Approved_A4_Concept.png) / [用户定稿三视图](References/Approved_A4_ThreeViews.png) / [A 决定及原图哈希](approval_A.json)

## Blender 中的入口

当前打开的是 `GroundMech_A4_01_TeamReview`，呈现同造型红蓝两机。数字小键盘 0 切换审核相机；旋转视口可自由查看实体。

| 场景 | 用途 |
| --- | --- |
| `GroundMech_A4_Construction` | 蓝方原始分件、刚性骨架、可编辑倒角与法线修改器；参考图作为隐藏 Empty 保留 |
| `GroundMech_A4_01_TeamReview` | 红蓝并排整体审核 |
| `GroundMech_A4_02_ThreeViews` | 红蓝各一组正／侧／背视图 |
| `GroundMech_A4_03_SingleInspect` | 蓝方单机三分之四视角 |
| `GroundMech_A4_04_SourceIdle` | 1–151 帧、30 FPS 的原包待机重定向预览；空格播放 |
| `GroundMech_A4_SourceAnimation` | 未改写的源待机动作与源骨架 |

在 Construction 场景选 `GMA4_Rig_BLUE`，自定义属性 `TeamIndex` 为 0 时蓝方、1 时红方；`AimYaw`、`AimPitch` 单位为度，`RightRecoil`、`LeftRecoil` 单位为米。静态审核姿态的瞄准与后坐均已归零。图纸已打包到 blend 中。

## 本次实际制作

主体 172 个命名分件、12,088 三角面、6,388 顶点；独立描边壳 129 件、9,420 三角面，合计 21,508 三角面。它们由规则截面、切角面板、支架、环和圆柱重新制作，未复用原包的 436,588 三角面高模作为成品。

高度 4.88 m，宽度 4.764 m，前后深度约 2.617 m。保留三角胸甲、四片散热鳍、上部双圆形装置、肩关节、双前臂炮及下方开口支架、分段装甲与宽脚掌；腿部沿定稿侧视图的髋／膝／踝折线制作。是否忠实还原图纸仍待本次 B 审核。

配色采用[美术规范第 11 节](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md#11-全项目统一色库v192026-10-08)的选色，分别保存队色主／辅区、奶油白、米砂、机构灰、墨线和琥珀灯。队色由骨架的阵营属性驱动，固定色区独立。表面三档因子为 0.44 / 0.73 / 1.00，使用常量色阶；灯芯独立发光。全部分件具备 UV，硬表面保留可编辑单段倒角。

## 绑定、枪口与动画

17 骨骼保留原 MechaDestroyer 名称与父子关系。刚性零件每顶点仅一个骨骼、权重 1；方形炮壳跟随 `GunArm_R/L`，圆形枪管跟随 `Gun_R/L`，避免原待机中的枪管自转带动整块炮壳。

| 挂点 | 所属骨骼 | 用途 |
| --- | --- | --- |
| `Muzzle` | `Gun_R` | 右枪口，兼容旧主枪口名称 |
| `Muzzle_L` | `Gun_L` | 左枪口 |
| `Jet_Outlet_R` / `Jet_Outlet_L` | `cockpit` | 左右下喷口 |

Blender 对象有 `GMA4_` 前缀，挂点局部 +X 为发射方向；具体骨骼局部矩阵保存在 [geometry_report.json](geometry_report.json)。左右枪独立后坐已核对；逐发交替的真实玩法仍属于后续 UE 接入。

为了适合 A_v4 折腿，骨骼参考位置已经调整，枪管骨骼转轴也对齐新枪管。原文件和动画未覆盖，但新骨架不声明能不经适配直接使用全部 UE 动画。当前只完成原待机动作的独立重定向副本：身体使用源相对动作、腿使用双段 IK 保持源脚部运动，实际烘焙帧的最大踝目标误差约 0.00000082 m。行走、冲刺、跳跃、左右开火和动画蓝图完整兼容性仍待后续 UE 适配。

## 证据与验收边界

- [网格、尺寸、骨架与挂点](geometry_report.json)
- [刚性权重、三档材质、后坐／瞄准及镜头范围数据检查](blender_data_checks.json)
- [原待机适配记录](source_idle_adapter.json)
- [源骨架合同](source_rig_contract.json) / [目标骨架合同](target_rig_contract.json)
- [真实 blend 渲染记录及文件哈希](render_manifest.json)

预览由此 blend 中真实网格渲染。助手未进行输出图片目检、截图比对或视觉验收，不用面数、文件存在和绑定检查推定“一比一完全一致”。用户在 Blender 或对照页核对轮廓、比例、折腿、装甲厚度、分件、色块和主要细节后记录 B 决定。

本阶段未替换正式 Ground 资产，未改碰撞或玩法参数。后续按[模型技能](../../../../.agents/skills/guli-model-production/SKILL.md)和[美术规范](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md)的“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”流程推进；本轮交付是 B 待审候选，不是最终视觉通过记录。
