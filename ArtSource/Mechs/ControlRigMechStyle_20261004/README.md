# ControlRig 机甲：审核 A-v1

2026-10-04。用途：指挥官批量单位。当前交付是基于实际源模型的参考设计，**A 待审核，B 未开始**。

本轮只导出源文件并做材质、线条及相机研究，没有重拓扑、主动减面、改骨架或制作正式成品。以下 `.blend` 是参考研究场景，不能用作 B 成品或正式导入文件。

## 参考图与同机位灰模

八张图均为原生 **2048 × 2048**。四张设计图共用源参考姿态、配色和正交尺度 16.14464 m；主炮保留源参考姿态的上仰。正、左、背视图无透视；三分之四图也使用正交相机，便于比例核对。没有 AI 生成或放大图片。

| 视角 | A-v1 设计 | 原模型灰模 |
|---|---|---|
| 三分之四 | [效果图](References_A_v1/ControlRigMech_A_v1_Hero.png) | [灰模](Baseline/ControlRigMech_Source_Hero_2048.png) |
| 正面 | [正视图](References_A_v1/ControlRigMech_A_v1_Front.png) | [灰模](Baseline/ControlRigMech_Source_Front_2048.png) |
| 左侧 | [左视图](References_A_v1/ControlRigMech_A_v1_Left.png) | [灰模](Baseline/ControlRigMech_Source_Left_2048.png) |
| 背面 | [背视图](References_A_v1/ControlRigMech_A_v1_Back.png) | [灰模](Baseline/ControlRigMech_Source_Back_2048.png) |

![A-v1 三分之四效果图](References_A_v1/ControlRigMech_A_v1_Hero.png)

## 配色与造型说明

| 色彩 | sRGB | 使用位置 |
|---|---|---|
| 暖白 | `#F1E7D5` | 主炮外壳、炮塔主装甲、四腿主承力护板、底盘上层 |
| 珊瑚红 | `#E97868` | 现有炮口外罩、左右检修/散热外罩、四腿胫部与踝部检修护甲、部分活塞护罩 |
| 深暖灰 | `#48413E` | 机械骨架、软管、底盘下层、孔洞和轴承内芯 |
| 暖钢灰 | `#756A60` | 圆形关节盖、活塞杆及金属装配件 |
| 琥珀 | `#F4B65C` | 仅源模型已有的功能镜片/指示灯，不把炮口和整块装甲当灯 |
| 线稿 | `#241F20` | 外轮廓与主装甲分界、真实凹槽和装配线 |

保留四足、单根长主炮、炮塔、底盘、圆形关节、足爪、活塞和软管。左右同类机构采用同色区规则。源单侧天线和源骨骼中的历史左右差异保留在参考基线中，不新增武器或改变接口。

三档艺术光照使用固定世界光向，亮部/中间色/阴影明确分档，不使用随机污渍或三角拓扑线。当前参考内线 1.4 px、外轮廓 2.25 px；正式实现仍按计划制作独立内线遮罩和简化外轮廓壳。参考的 Freestyle 渲染不代表正式描边成本已达标。旧贴花与污渍卡片仅在设计图中通过透明材质抑制，源文件及灰模保留。

## 实际源资产登记

来源根目录：`/Game/Assets/ControlRig/Characters/Mech`，只读查询得到 44 项资产。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。


完整骨骼名称、父子关系、局部/全局参考变换、材质和依赖见 [源清单](Source/source_manifest.json)。各源 LOD 导出回读见 [LOD 统计](Baseline/source_lod_inspection.json)。

FBX 包含 284,700 个三角面；Blender 导入回读为 284,660。已定位到 **40 个使用相同顶点索引的重复面**，见 [FBX 计数审计](Baseline/fbx_count_audit.json)。这是导入校验去重，不是已实施的减面方案；当前参考和灰模使用同一个回读网格。其位置、拓扑和权重在参考着色前后的哈希完全一致，见 [参考设置](References_A_v1/reference_setup.json)。

## 活动件与兼容要求

| 机构 | 源骨骼/控制依据 | 后续制作约束 |
|---|---|---|
| 整机与底盘 | `root → base → turret_base` | 部署底盘会抬升，保持原骨骼参考变换与动画位移 |
| 主炮俯仰 | `main_gun`、`piston_01_l/r`、`piston_02_l/r` | 保留炮轴、成对活塞和动作两端空间 |
| 炮管分段 | `main_gun → cannon_01`；`cannon_02/03` 均为 `cannon_01` 子节点 | 三个名字对应同一主炮的分段机构；保留部署伸缩与缩放 |
| 四条腿 | `legbase_fr/bk_l/r`、`leg_fr/bk_01..04_l/r`、`piston_*`、`legpump_*` | 刚性护甲，保留圆关节、内部活塞和运动时可见结构 |
| 足爪与软管 | `foot_*`、`toe_*`、`toecord_*`、`topcord_*` | 足爪独立活动；软管保留必要柔性蒙皮 |
| 舱盖与天线 | `hatch_*`、`antenna_01_l/02_l` | 保留源层级和现有机构，不强行增删骨骼 |
| ControlRig | 原 CR_Mech 图表、函数模型和 preview/skeleton 引用 | 保留原构造与控制关系，正式副本只在 B 放行后迁移引用 |

[源动画采样](Source/rig_animation_baseline.json)保存了三个动画起点、中点、终点的全部 152 根骨骼局部变换。部署中 `base` 局部 Z 从 **87.38 cm → 192.8959 cm**，`cannon_02` 局部 X 缩放从 **0.516426 → 1**；这些属于源动作，不得“修复”成常量。三点采样只证明源运动基线，不替代 B 阶段完整动画预览和兼容验收。

CR_Mech 的静态编辑器默认 hierarchy 目前只有 `mech` 控制项；源图表含构造、腿、活塞、炮塔、映射等函数模型。已记录 31 个图表/函数模型、811 个节点和 1057 条连接，其中构造事件含 `HierarchyImportFromSkeleton`。这个默认 hierarchy 列表不是构造后运行时的控制数量，不能用它删减控制。

Blender 默认 FBX 导入把 UE 的 `root` 表示为名为 `root` 的 Armature 对象，骨骼列表只有 151 个名字。**当前参考场景不能当作已兼容的导出骨架**。B 阶段须依据源 152 骨骼清单恢复完整导出层级，并检查原位移、缩放和参考姿态；本轮没有做生产绑定。

## B 阶段预算与交付范围

以下是用户已确认的目标，尚未实现或验收。


源本体高于 LOD0 本体目标 **252,700 三角面**，所以仅小幅全局压面不足以达到预算。A 放行后优先去除确认的隐藏/重复面、降低冗余圆柱和小装配件分段、整理 UV 接缝与贴花冗余，并保留主机构、曲面和运动时可见内部结构。若仍超预算，提交差额及同机位视觉对比。


UE 三档指挥官镜头与动画/尺寸/法线/缩放一致性检查在 B 后进行。本轮没有改变玩法身份、C++、Ground、重防号或战斗引用。

## 版本、审核与复现

- [A-v1 文件及 SHA256 清单](References_A_v1/reference_manifest.json)：发布后冻结；修改另建 A-v2。
- [审核决定](review_decisions.json)：当前 A 待审核、B 未开始，用户确认本计划不等于 A/B 通过。
- [源导入 Blender](Baseline/ControlRigMech_SourceImported.blend)和[参考研究场景](References_A_v1/ControlRigMech_A_v1_ReferenceStudy.blend)。
- [UE 源查询/FBX 导出](Scripts/export_source_ue.py)、[源动画采样](Scripts/query_rig_animation_ue.py)、[Blender 灰模采集](Scripts/inspect_source_blender.py)、[参考渲染](Scripts/render_reference_a.py)、[FBX 计数审计](Scripts/check_source_fbx.py)。
- 现有源码引擎 `D:/UnrealEngine-5.7` 的普通 Editor 启动脚本恢复只读查询；FBX 导出使用图形后端并关闭材质烘焙。没有修改持久插件配置、编译原生代码或保存 UE 资产。
- Blender 5.2.2 LTS / EEVEE / Standard 色彩管理；单图输出原生 2048，保留源导入自定义法线。
- [正式需求](../../../Progress/RequirementDocument/20261004-ControlRig机甲美术统一.md)与[实施和审核台账](../../../Progress/DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md)。

审核依据是用户当前计划及[模型技能](../../../.agents/skills/guli-model-production/SKILL.md)和[美术规范 v1.2](../../../Progress/RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)的“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”。请针对 **A-v1** 确认通过或指出修改位置，取得具体版本决定后再重制。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。

