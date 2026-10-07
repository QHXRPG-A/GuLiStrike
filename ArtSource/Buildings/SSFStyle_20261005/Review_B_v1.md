# SSF 建筑实际成品审核 B_v1

六座建筑及平台、灯柱、灯片、无人机已完成 Blender 制作，按已审 A_v7 配色、原模型尺寸/装配与原动画制作。当前提交具体成品 **SSF_Production_B_v1**；用户视觉审核 B 与24项本体预算例外均待决定。

用户“开始制作，严格一比一按照参考图和原模型制作”作为 A_v7 制作放行依据；[A 决定](approval_A.json)固定其版本与清单哈希。所有参考 A_v1–A_v7 保留。

- [完整图板与22个视频审核页](review_B_v1.html)
- [实际可编辑 Blender 成品](Production_B_v1/SSF_Production_B_v1.blend)
- [原始逐档面数与制作记录](Production_B_v1/construction_report.json)
- [本体预算差额与待决定例外](Production_B_v1/Budget_Exceptions.md)
- [网格、权重和材质核对](Production_B_v1/native_validation.json)
- [实际已审色值与参数保持](Production_B_v1/palette_preservation_validation.json)
- [22动作 × 3LOD 实际变形核对](Production_B_v1/animation_deformation_validation.json)

![实际建筑成品总览](Production_B_v1/Overview_Buildings_B_v1.png)

## 一比一依据与真实成品

42张2048×2048效果/正交图、十套三视图图板、十套参考同机位对照、十套三档LOD对比和两张4096×4096组合均由实际成品渲染。相机、姿态和正交尺度读取已审 A_v7；没有用二维生成图代替 Blender 成品。

保留792个制作分件、7套骨架共355根骨骼、原骨骼名称/层级/Root/轴心和全部22个原名称动作。金属分件权重为1；克隆中心软管保留源柔性权重。编辑集合含镜像和减面修改器，三档合并副本用于渲染/后续导出。

近档与中档使用独立内部线稿遮罩和实际绑定描边壳；远档内部线稿为0、描边为0。源半透明标识和灯片保留，Light为原2面单面贴片，因此侧/背图不可见属原结构。

![真实材质拆分](Production_B_v1/Sheets/Actual_Style_Breakdown.png)

固定艺术光向 `(0.35,-0.55,0.76)`；明暗阈值 `0.38/0.68`、因子 `0.40/0.72/1.00`，沿用 A_v7。内部线稿 R=源面板线、G=规则结构折线，强度随LOD为 `1.00/0.70/0`；`Base Color`、`Team Color` 参数保留。成品外轮廓来自真实壳网格，Freestyle关闭。壳在预算内覆盖主要不透明构件，部分外轮廓比参考Freestyle细；源线稿重投影也有局部差异，对照中如实呈现，严格视觉一致性仍待B决定。

## 实际面数与预算例外

本体优先清理冗余面、重合点与可安全减少的分段；保留开门、运转和破坏时暴露的内部结构。为维持曲面、细长天线截面和所有运动零件，六座建筑、平台和无人机共24档本体未达到原上限；当前方案是保留结构并申请例外，尚未擅自调整预算。描边各档已达预算，实际材质近/中档最多3区段、远档最多2区段。

| 资产 | 原本体 | LOD0 实际本体＋描边 | LOD1 实际本体＋描边 | LOD2 实际本体＋描边 | 本体差额 L0/L1/L2 |
|---|---:|---:|---:|---:|---:|
| 空军基地 | 5390 | 5216＋700 | 4839＋400 | 4128＋0 | +916 / +2539 / +3078 |
| 克隆中心 | 2634 | 2454＋296 | 2338＋200 | 2170＋0 | +254 / +1238 / +1670 |
| 指挥中心 | 4550 | 4465＋598 | 4277＋299 | 3999＋0 | +665 / +2377 / +3149 |
| 平台 | 8113 | 6699＋963 | 6699＋473 | 6057＋0 | +199 / +3199 / +4557 |
| 灯柱 | 48 | 48＋0 | 48＋0 | 48＋0 | +0 / +0 / +0 |
| 灯片 | 2 | 2＋0 | 2＋0 | 2＋0 | +0 / +0 / +0 |
| 无人机 | 254 | 254＋36 | 254＋18 | 210＋0 | +44 / +144 / +160 |
| 军工厂 | 2130 | 1953＋249 | 1895＋149 | 1639＋0 | +153 / +995 / +1239 |
| 反应堆 | 3654 | 3546＋494 | 3154＋238 | 2635＋0 | +546 / +1654 / +1935 |
| 战略中心 | 4326 | 4175＋600 | 3939＋298 | 3582＋0 | +575 / +2139 / +2782 |

默认屏幕尺寸 `1.0 / 0.10 / 0.035` 已写入三档对象元数据；此阶段尚未验证 UE 自动切换和项目镜头。

建筑四张2K图集/资产，平台四张4K，无人机和灯柱四张1K；共享源Logo及灯片各1K，共38张唯一新贴图，PNG共14.21MiB。未压缩RGBA8基准约680MiB（完整mip约906.7MiB）；这是纹理预算记录，UE压缩/流送/驻留和实战帧率未测量。

## 每个资产的审核图板

### AirBase · 空军基地

[实际效果与三视图](Production_B_v1/Sheets/AirBase_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/AirBase_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/AirBase_LOD_Comparison.png)

### CloningCenter · 克隆中心

[实际效果与三视图](Production_B_v1/Sheets/CloningCenter_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/CloningCenter_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/CloningCenter_LOD_Comparison.png)

### CommandCenter · 指挥中心

[实际效果与三视图](Production_B_v1/Sheets/CommandCenter_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/CommandCenter_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/CommandCenter_LOD_Comparison.png)

### Floor · 平台

[实际效果与三视图](Production_B_v1/Sheets/Floor_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/Floor_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/Floor_LOD_Comparison.png)

### Lamp · 灯柱

[实际效果与三视图](Production_B_v1/Sheets/Lamp_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/Lamp_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/Lamp_LOD_Comparison.png)

### Light · 灯片

[实际效果与三视图](Production_B_v1/Sheets/Light_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/Light_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/Light_LOD_Comparison.png)

### Drone · 无人机

[实际效果与三视图](Production_B_v1/Sheets/Drone_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/Drone_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/Drone_LOD_Comparison.png)

### MilitaryFactory · 军工厂

[实际效果与三视图](Production_B_v1/Sheets/MilitaryFactory_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/MilitaryFactory_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/MilitaryFactory_LOD_Comparison.png)

### Reactor · 反应堆

[实际效果与三视图](Production_B_v1/Sheets/Reactor_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/Reactor_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/Reactor_LOD_Comparison.png)

### StrategyCenter · 战略中心

[实际效果与三视图](Production_B_v1/Sheets/StrategyCenter_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/StrategyCenter_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/StrategyCenter_LOD_Comparison.png)

## 全部动画与技术检查

22个完整视频均在一个固定镜头并排播放三个实际LOD，15fps预览包含准确开始/结束及最后1/15秒停帧；动作来自UE逐帧30fps采集的6003个姿态样本，不裁剪破坏碎片运动。原 `Destoy`、`Edle` 名称按源包保留。

**空军基地**

- [TB1_AirBase_Destroy1_Anim](Production_B_v1/AnimationPreviews/TB1_AirBase_Destroy1_Anim.mp4)
- [TB1_AirBase_Destroy2_Anim](Production_B_v1/AnimationPreviews/TB1_AirBase_Destroy2_Anim.mp4)
- [TB1_AirBase_Idle_Anim](Production_B_v1/AnimationPreviews/TB1_AirBase_Idle_Anim.mp4)
- [TB1_AirBase_OpenDoor_Anim](Production_B_v1/AnimationPreviews/TB1_AirBase_OpenDoor_Anim.mp4)

**克隆中心**

- [TB1_CloningCenter_Destroy1_Anim](Production_B_v1/AnimationPreviews/TB1_CloningCenter_Destroy1_Anim.mp4)
- [TB1_CloningCenter_Destroy2_Anim](Production_B_v1/AnimationPreviews/TB1_CloningCenter_Destroy2_Anim.mp4)
- [TB1_CloningCenter_Idle_Anim](Production_B_v1/AnimationPreviews/TB1_CloningCenter_Idle_Anim.mp4)
- [TB1_CloningCenter_OpenDoor_Anim](Production_B_v1/AnimationPreviews/TB1_CloningCenter_OpenDoor_Anim.mp4)

**指挥中心**

- [TB1_CommandCenter_Destoy1_Anim](Production_B_v1/AnimationPreviews/TB1_CommandCenter_Destoy1_Anim.mp4)
- [TB1_CommandCenter_Destoy2_Anim](Production_B_v1/AnimationPreviews/TB1_CommandCenter_Destoy2_Anim.mp4)
- [TB1_CommandCenter_Edle_Anim](Production_B_v1/AnimationPreviews/TB1_CommandCenter_Edle_Anim.mp4)

**无人机**

- [TB1_Drone_Fly_Anim](Production_B_v1/AnimationPreviews/TB1_Drone_Fly_Anim.mp4)

**军工厂**

- [TB1_MilitaryFactory_Destroy1_Anim](Production_B_v1/AnimationPreviews/TB1_MilitaryFactory_Destroy1_Anim.mp4)
- [TB1_MilitaryFactory_Destroy2_Anim](Production_B_v1/AnimationPreviews/TB1_MilitaryFactory_Destroy2_Anim.mp4)
- [TB1_MilitaryFactory_Idle_Anim](Production_B_v1/AnimationPreviews/TB1_MilitaryFactory_Idle_Anim.mp4)
- [TB1_MilitaryFactory_OpenDoor_Anim](Production_B_v1/AnimationPreviews/TB1_MilitaryFactory_OpenDoor_Anim.mp4)

**反应堆**

- [TB1_Reactor_Destroy1_Anim](Production_B_v1/AnimationPreviews/TB1_Reactor_Destroy1_Anim.mp4)
- [TB1_Reactor_Destroy2_Anim](Production_B_v1/AnimationPreviews/TB1_Reactor_Destroy2_Anim.mp4)
- [TB1_Reactor_Idle_Anim](Production_B_v1/AnimationPreviews/TB1_Reactor_Idle_Anim.mp4)

**战略中心**

- [TB1_StrategyCenter_Dead1_Anim](Production_B_v1/AnimationPreviews/TB1_StrategyCenter_Dead1_Anim.mp4)
- [TB1_StrategyCenter_Dead2_Anim](Production_B_v1/AnimationPreviews/TB1_StrategyCenter_Dead2_Anim.mp4)
- [TB1_StrategyCenter_Idle_Anim](Production_B_v1/AnimationPreviews/TB1_StrategyCenter_Idle_Anim.mp4)

实际骨骼抽样矩阵最大差 `0.000004768`，22动作每个5个时间点、各3LOD全部本体顶点对照源骨骼变换，最大位置差 `0.000005888 m`。这些检查证明 Blender 制作与源运动契约，不能代替用户视觉审核或正式UE导入回读。

- [实际MP4回读：帧数、尺寸、帧率和110个检查帧](Production_B_v1/AnimationPreviews/movie_readback_report.json)
- [视频检查帧与原始渲染像素误差](Production_B_v1/AnimationPreviews/movie_pixel_validation.json)
- [助手视觉检查](Production_B_v1/visual_qa.json)
- [冻结交付清单](Production_B_v1/production_manifest.json)

## 决定与交付边界

本成品 Blender SHA256：`6f386c2e4782ce6ab6e81ae0f2380a6345fa05cdc6fc3dcf9d260c13ed3a806e`。A_v7清单 SHA256：`d1d3dcabdac432395edd1554da297de47e555a94c9e6ff77893043a7487c0173`。

需要对 **SSF_Production_B_v1** 的外观/运动作出 B 决定，并对记录中的24项本体差额作出明确例外或继续调整决定。当前 B 与例外均未通过。依据用户原定流程及[模型制作技能](../../../.agents/skills/guli-model-production/SKILL.md)“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”，提交可审成品后停在B。正式导出回读、UE材质/LOD/22动画/物理资产/无人机蓝图副本和项目镜头验收均在B放行后继续；本轮未导入正式目录，未覆盖商城源包。
