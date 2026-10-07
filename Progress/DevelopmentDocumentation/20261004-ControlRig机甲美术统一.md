---
schema: guli-progress/v1
id: DEV-20261004-002
work_id: WORK-20261004-002
kind: development
role: root
title: ControlRig 机甲美术统一 — 制作与审核记录
areas: [art, assets, rendering]
categories: [art]
status: verification
verification: partial
created: '2026-10-04'
updated: '2026-10-04'
summary: 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。
next_action: 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。
relations:
  requirement: REQ-20261004-002
status_note: 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。
art_revision: '1.2'
---

# ControlRig 机甲美术统一 — 制作与审核记录

## 范围与依据

执行用户本轮完整实施计划和[需求](../RequirementDocument/20261004-ControlRig机甲美术统一.md)。模型路线遵循[制作技能](../../.agents/skills/guli-model-production/SKILL.md)和[美术规范 v1.2](../RequirementDocument/GuLiStrike美术规范.md)。本次是纯美术 A/B 制作阶段，不套用代码功能的编译、PIE 或 Map 测试流程。

首轮已完成只读源采集、参考设计和审核交付；当前按用户 A 放行制作真实 Blender 候选。参考研究场景没有重拓扑或主动减面，不能充当 B 成品。

## 实际源基线

| 项目 | 结果 |
|---|---|
| 主模型/骨架 | `/Game/Assets/ControlRig/Characters/Mech/Meshes/SKM_Mech` / `SK_Mech` |
| 构型 | 四足、单主炮、炮塔/底盘、活塞、足爪、软管与检修舱盖 |
| 骨骼 | 152；全部名称、父子关系、局部/全局参考变换已通过 SkeletonService 记录 |
| ControlRig | `Rigs/CR_Mech`，ControlRigBlueprint；实际 preview=SKM_Mech，依赖 SK_Mech；源函数模型与引用留档 |
| 源 LOD0 | 284,700 三角面，203,324 顶点，14 材质区段；源仅 1 LOD |
| 参考姿态尺寸 | 宽 922.64 cm、前后长 1323.33 cm、高 689.26 cm，包含上仰炮管 |
| 物理资产/挂点 | 源 mesh 的 physics_asset=null，挂点数=0 |
| 部署 | 5 s / 30 fps / 151 采样键 / 152 骨骼轨道 |
| 待机 | 8.666667 s / 30 fps / 261 采样键 / 152 骨骼轨道 |
| 行走 | 5 s / 30 fps / 151 采样键 / 152 骨骼轨道 |

完整数据见[源清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Source/source_manifest.json)和[源 Rig/动画采样](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Source/rig_animation_baseline.json)。没有直接读取 UE 二进制文件内容，原包未保存或覆盖。

只读读取能力通过现有源码版普通 Editor 的独立启动脚本恢复，使用 SkeletonService/AnimSequenceService/AssetRegistry 和 FBX Exporter；没有改持久插件设置或编译原生代码。初次 Python 调用不适用的方法已修正；空 RHI 导出遭遇引擎材质提取断言，改用图形后端并关闭材质烘焙后成功。工作进程只退出自身，不中断用户编辑器。

## 已发现的接口和回读事项

- 源 FBX 仍有 284,700 个三角面；Blender 校验后为 284,660。独立 [FBX 计数审计](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Baseline/fbx_count_audit.json)确认 40 个同顶点索引重复面，不是主动实施减面。灰模和设计参考共用同一回读几何。
- Blender 默认导入把 UE `root` 转成 Armature 对象，骨骼列表仅 151 个名称。B 阶段必须依据源清单恢复完整 152 骨骼层级并回读；当前 `.blend` 不能当成已兼容的生产骨架。
- 部署三点采样显示 `base` 局部 Z 为 87.38→192.8959 cm，`cannon_02` 局部 X 缩放为 0.516426→1。保留原伸缩，不能做通用位移/缩放归一化。
- CR_Mech 静态编辑器默认 hierarchy 仅列 `mech` 控制项，不能当作构造后的运行时控制总数。已读取 31 个图表/函数模型、811 节点、1057 连接及节点默认值，构造事件含 `HierarchyImportFromSkeleton`。保留原图表、函数和骨架/preview 关系，B 后正式副本另行验证。
- 单侧天线及少量源左右姿态差异作为基线保留；生产对称装配不能以改名或改源骨骼契约实现。

## 参考 A-v1 历史与审核记录

入口：[图纸、灰模、配色及活动件说明](../../ArtSource/Mechs/ControlRigMechStyle_20261004/README.md)。全部四设计图和四灰模对照为原生 2048×2048；正、左、背及三分之四相机共用姿态、正交尺度和色区。暖白主装甲、珊瑚红检修件、深暖灰骨架、暖钢灰关节、原镜片琥珀灯，内线与外轮廓清楚。

对实际四图进行视觉检查：四足、单炮和装配未产生视图冲突，左右主要色区一致，关节和曲面没有替换成粗块。参考着色前后位置/拓扑/权重哈希一致；自定义法线保持源导入结果。三档着色通过固定世界艺术光向实现，内线 1.4 px、轮廓 2.25 px；Freestyle 只作参考渲染，生产仍要独立遮罩和简化壳。

| 阶段 | 版本 | 决定 | 依据 |
|---|---|---|---|
| 需求/用途/方向 | 用户当前实施计划 | 已确认 | 当前用户要求“PLEASE IMPLEMENT THIS PLAN: ControlRig 机甲美术统一方案” |
| A 参考设计 | A-v1 | **修改后再审** | 用户 2026-10-04 提供四色色板并明确“改成这种配色”，未通过旧配色 |
| A 参考设计 | A-v2 | **通过** | 用户 2026-10-04 原话“审核通过，blender已开，根据参考图和源模型一比一制作”，版本与哈希见 A 放行记录 |
| B 实际成品历史 | B-v2 | **用户要求线稿修订** | “所以线稿没搞？”与“三渲二，线稿，三档明暗”；旧冻结文件保留，未获B通过 |
| B 实际成品历史 | B-v3 | **用户要求降低线稿含量** | “线稿含量低一些，保留基础轮廓”；旧冻结文件保留 |
| B 当前实际成品 | B-v4 | **导入放行** | 用户“导入至ue”，明确针对当前简线版；独立记录锁定源清单和Blender哈希，预算仍超标 |
| UE 正式副本 | UE-v1 / B-v4 | **已保存** | 16生产资产+2展示资产，实际渲染及源/正式组件三动作9个检查点通过；性能未验收 |

对应文件版本和 SHA256 见[冻结清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v1/reference_manifest.json)，用户决定单独保存于[决定文件](../../ArtSource/Mechs/ControlRigMechStyle_20261004/review_decisions.json)，不修改冻结参考。

## 2026-10-04 四色修订 A-v2

用户最新要求“改成这种配色”，色板标注 `#2C3735 / #8E3A2A / #557B78 / #D5C09C`。当前[效果、正/左/背三视图与说明](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v2/README.md)采用灰青主装甲、铁锈红检修护甲/炮口外罩、深青灰骨架/软管、沙米色关节盖/金属件；原功能镜片也使用沙米色亮档。线稿用深青灰派生的 `#1B2422`，三档亮度、线宽和固定艺术光向沿用 A-v1。

在 A-v1 实际源网格场景中修改材质后原生渲染四张 2048×2048 图；四个相机参数、尺度、参考姿态、原件分色边界和同机位灰模均不改变。实际查看四图：四足、单炮、左右主要色区和装配关系一致，曲面与圆关节保持完整。位置/拓扑/权重哈希与 A-v1 相同；另核对物体变换、骨骼层级/姿态、自定义角法线及逐面材质索引未改变。没有主动减面、重制生产骨架或导入 UE。

[A-v2 冻结清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v2/reference_manifest.json)包含 14 个新参考或共享灰模文件，SHA256 为 `ea47e95d2e5ee1f8d2bfe3ccd1fd9b249873e99fe31d93f9c9935b72af1ed9ac`。发布前重新核对 A-v1 全部 27 个冻结文件及清单哈希，均保持原样。用户色板已按原字节复制到 `References_A_v2/Inputs/UserPalette_20261004.jpg`，与临时附件哈希相同；准确来源、文件版本和验证边界见[本轮归档](../Archive/20261004-ControlRig机甲四色参考A_v2.md)。

决定文件保留 A-v1 的修改要求历史，当时 A 指向 A-v2 且待审核；后续用户已明确通过 A-v2。配色修改授权本身不视为 A 或 B 通过；全局美术规范仍为 v1.2。未来 Blender 制作、动画兼容和正式 UE 交付继续遵循原计划。

## 后续预算与交付

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。


B 审核实际成品及完整三动画后，先导出回读，再建立 `/Game/GuLiStrike/Mechs/ControlRigMech` 正式副本；保留源模型、骨架、CR 和三个动画。源无物理资产/挂点的事实已登记，不虚构已有配套。UE 的三档指挥官镜头核验在 B 后执行。

## 任务与验证边界

- [x] 恢复只读源查询，登记实际资产、骨架、CR 和源动画。
- [x] 导出原源 LOD，采集灰模、结构和部件基线。
- [x] 完成并查看四张一致的 2048 参考及灰模。
- [x] 冻结 A-v1 哈希，登记需求、阶段归档和 A/B 决定状态。
- [x] 按用户色板完成 A-v2 四图和材质研究场景，冻结版本并确认 A-v1 27 个文件未变。
- [x] 用户审核通过 A-v2；[放行记录](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Approvals/Approval_A_v2_20261004.json)锁定清单 SHA256 `ea47e95d2e5ee1f8d2bfe3ccd1fd9b249873e99fe31d93f9c9935b72af1ed9ac`。
- [x] 用户B-v4导入放行后的导出回读、正式UE副本及三档指挥官镜头独立预览。

技术验证当前为 `partial`：覆盖源资产读取、参考分辨率、相机、几何/权重一致性和哈希；新增 Blender 骨架、关键姿态与实际视频核验；未声称最终 UE 动画兼容、预算、UE 外观或实战帧率通过。没有 C++、玩法引用、地图、正式资产目录改动。

## 2026-10-04 A 放行与实际 Blender B-v2

用户明确“审核通过，blender已开，根据参考图和源模型一比一制作”，据此通过 A-v2；[独立放行记录](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Approvals/Approval_A_v2_20261004.json)锁定已审清单，不改写旧冻结参考。

[实际 B-v2 交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/README.md)与[对照/视频](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/Review_B_v2.html)为当时提交候选；[Blender 源](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/ControlRigMech_B_v2_Production.blend)已打开，保留源四足单炮、152骨骼层级、原动作、761个可编辑分件、86个核对过的镜像和两个32分段剖面旋转件。主视图与材质/描边壳预览分别登记，前者原生 Freestyle，后者内线仍偏软，未冒充 UE 等效通过。


B-v2清单SHA256 `79018077be8ed6c1ce1d1679594fcb8b01040dd85918d169251f1629818d6fa9`，Blender SHA256 `8e94a4547875ba54b731770b77297a229fbe36e53112a634624bb6502dee88ef`。A-v1/A-v2共41个冻结文件保持原样；[用户决定](../../ArtSource/Mechs/ControlRigMechStyle_20261004/review_decisions.json)将B-v2记为待审核。导出回读、正式UE副本、ControlRig副本、三档指挥官UE镜头和实战帧率尚未运行；全局规范仍v1.2。


## 2026-10-04 B-v3 实际线稿与三档明暗

用户连续指出“所以线稿没搞？”并明确“三渲二，线稿，三档明暗”。B-v2主展示使用Freestyle，实际shader内线未完成到同样效果；此前直接回答“做了”不准确。本版把选定装甲/装配边缘的距离字段与独立原生2K遮罩接入真实生产材质，并制作随原骨骼运动的反向轮廓壳。所有实际图、完整动作和材质预览均关闭Freestyle；不再用主展示图代替真实shader完成证据。

[B-v3完整交付与检查范围](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3/README.md) · [实际线条开关/参考/动作对照](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3/Review_B_v3.html) · [当前Blender截图](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3/Blender_Live_B_v3.png)。当前文件默认 `STYLE_REALTIME_B_v3`，两个可旋转3D材质窗口；B-v3文件内旧Freestyle展示场景已移除。三档线性系数0.42/0.74/1、原四色分区与功能机构保留。


B-v3清单SHA256 `7b9196d4c356751e7eeef763a4a4e3c57966161e132ea7324aef648eebe0e2bf`；Blender SHA256 `6ceea9e11e49203cd00e8cdfbd24f75de99c6ff2af6ba5239a641d3b08b037c1`。A-v1/A-v2共41个冻结文件、B-v1共352个、B-v2共355个文件全量哈希核对未变。B-v2历史记为用户要求实时线稿修订，当前B-v3待审核，未把重申风格当作B通过。预算、UE导出回读/ControlRig副本/指挥官镜头及实战帧率尚未通过或执行；无UE源包、玩法身份、C++和战斗引用改动。全局规范仍v1.2，具体记录见[本轮归档](../Archive/20261004-ControlRig机甲B_v3实际线稿与三档明暗.md)。


## 2026-10-04 B-v4 减少线稿，保留基础轮廓


四张同姿态/尺度的原生2K图和全部283帧实际shader动作重新渲染。完整部署/待机/行走MP4已解码核对尺寸、帧数及9个首/中/尾检查点，RGB与对应PNG误差在有损编码容差内；本轮不声称全部解码帧逐帧比对。未改变源部署位移、伸缩或缩放。


B-v4清单SHA256 `802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16`，Blender SHA256 `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`；旧B-v3全部341个冻结文件哈希核对未变。当前B-v4待用户针对本版决定，A-v2放行有效，B-v3记为用户要求减少线稿的历史。规范仍v1.2，本次只针对ControlRig机甲，不改UE正式目录、源包、玩法身份或C++；[增量归档](../Archive/20261004-ControlRig机甲B_v4减少线稿保留轮廓.md)。

## 2026-10-04 B-v4 正式 UE 导入

用户“导入至ue”明确放行当前B-v4，[独立记录](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Approvals/Approval_B_v4_Import_20261004.json)锁定清单SHA256 `802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16`及Blender SHA256 `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`。冻结B-v4全部333个文件核对未变；前文及旧冻结文件的B待审核/UE未导入是历史状态。最新源与决定入口为[CURRENT](../../ArtSource/Mechs/ControlRigMechStyle_20261004/CURRENT.md)。

正式根 `/Game/GuLiStrike/Mechs/ControlRigMech` 保存16个生产资产：模型、兼容骨架、部署/待机/行走、ControlRig、2母材质+4LOD实例、4贴图；另存展示地图和背景材质。当前UE打开正式模型，展示地图 `/Game/GuLiStrike/Mechs/ControlRigMech/Preview/LVL_ControlRigMech_B_v4` 中Actor `CRM_B_v4`、相机 `CRM_ReviewCamera` 已保存。原包、玩法身份、C++与战斗引用未改；源physics=null、sockets=0不虚构配套。


导出副本只清理同顶点索引重复面0/4/26/33，源Blender不改。全部LOD超预算，不认定批量性能通过。2K源纹理RGBA8合计64MiB，原生查询时资源尺寸可随加载变化，实际查询值/压缩与依赖见[最终原生清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/final_live_inventory.json)，未测驻留显存。颜色仍由顶点色驱动，功能/ORM配套未新增改变已审外观的PBR效果。


UE无法直接改AnimSequence只读Skeleton关系，最终通过AnimationDataController在正式骨架上重建源30fps全部152骨骼局部T/Q/S；保留部署位移、伸缩、缩放、动作时长/速率/压缩设置，不进行通用归一化。真实组件与源组件9×152骨骼最大位置差0.00184006cm、旋转0.00031597°；行走中点原始轨道与播放约0.8mm差异，源播放也存在，排除导入误差。额外事件数据另列清单，ControlRig 31图/811节点/1057连接/默认值匹配，全部运行时控制未逐一操作。

项目近景35m/25°、战术300m/55°与90°全览按实际Camera JSON/资源战场范围独立预览，无HUD；实际HUD构图、连续LOD过渡、完整运行时穿插/接地及实战FPS未验收。纯美术交付不编译、不运行PIE。验证状态保持partial（预算/实战余项），本轮UE导入已完成；[完整交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/README.md)、[增量归档](../Archive/20261004-ControlRig机甲B_v4正式UE导入.md)。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../ArtSource/CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。


