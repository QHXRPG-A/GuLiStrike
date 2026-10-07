# SSF 建筑蓝红阵营配色 · 实际 Blender B_v2

当前版本 **SSF_TeamPalette_B_v2** 已在 Blender 5.2.2 LTS 打开，**待用户审核**。本轮按用户“蓝色方所有建筑改成图1配色，红色方把所有建筑改成图二配色，先放Blender给我审核”制作六座建筑各一份蓝方、一份红方，共十二个实际模型。配色来自附图对应的原版 AirBase / CommandCenter 材质色值，保留各建筑既有功能分区。

[打开 Blender 文件](SSF_TeamPalette_B_v2.blend) · [两队配色总览](Sheets/Blue_Red_Buildings_Overview.png) · [实际尺寸组合](Renders/Blue_Red_Assembly_Compare.png)

## Blender 查看入口

默认场景 `Review_Blue_Red_Buildings`：**左侧蓝方、右侧红方**，按原建筑尺寸摆放，使用真实渲染视口。切换顶部 Scene 选择器可进入 `Blue_Buildings_Assembly` / `Red_Buildings_Assembly`，或 `Blue_AirBase` / `Red_AirBase` 等单建筑场景近看。

各单建筑场景默认显示 LOD0；`<Team>_<Asset>_LOD0/1/2` collection 可在 Outliner 分别启用以检查对应档位。图板提供已实际渲染的同机位对照，无需改变已打开场景。历史制作场景仍保留792个可编辑分件及原修改器；新配色的三档成品网格也可编辑。

## 两套基础配色

| 阵营 | 主色与配色 | 阴影 / 结构颜色 |
|---|---|---|
| 蓝方 · 图1 | 橙 `#EE9D58`、蓝灰 `#274E61`、奶油白 `#FEE4D9` | 原空军基地少量框架 `#1A182F`；军工厂/战略中心框架使用蓝灰 |
| 红方 · 图2 | 莓红 `#A34053`、浅粉 `#E3B6B1`、橙 `#EE9D58` | 原指挥中心少量框架 `#662249`；军工厂/战略中心框架使用莓红 |

原独立内部线稿、近中档真实描边壳、三档明暗因子 `0.40 / 0.72 / 1.0`、阈值 `0.38 / 0.68`、固定光向及 `Base Color` / `Team Color` 接口保留。十二张新2K基础色图集已打包入当前 blend；原线稿遮罩、UV、ORM与半透明标识继续使用现有版本。

## 逐建筑审核图板

| 建筑 | 蓝方效果 / 三视图 | 红方效果 / 三视图 | 蓝方 LOD | 红方 LOD |
|---|---|---|---|---|
| 空军基地 · AirBase | [图板](Sheets/Blue_AirBase_Views.png) | [图板](Sheets/Red_AirBase_Views.png) | [三档](Sheets/Blue_AirBase_LODs.png) | [三档](Sheets/Red_AirBase_LODs.png) |
| 克隆中心 · CloningCenter | [图板](Sheets/Blue_CloningCenter_Views.png) | [图板](Sheets/Red_CloningCenter_Views.png) | [三档](Sheets/Blue_CloningCenter_LODs.png) | [三档](Sheets/Red_CloningCenter_LODs.png) |
| 指挥中心 · CommandCenter | [图板](Sheets/Blue_CommandCenter_Views.png) | [图板](Sheets/Red_CommandCenter_Views.png) | [三档](Sheets/Blue_CommandCenter_LODs.png) | [三档](Sheets/Red_CommandCenter_LODs.png) |
| 军工厂 · MilitaryFactory | [图板](Sheets/Blue_MilitaryFactory_Views.png) | [图板](Sheets/Red_MilitaryFactory_Views.png) | [三档](Sheets/Blue_MilitaryFactory_LODs.png) | [三档](Sheets/Red_MilitaryFactory_LODs.png) |
| 反应堆 · Reactor | [图板](Sheets/Blue_Reactor_Views.png) | [图板](Sheets/Red_Reactor_Views.png) | [三档](Sheets/Blue_Reactor_LODs.png) | [三档](Sheets/Red_Reactor_LODs.png) |
| 战略中心 · StrategyCenter | [图板](Sheets/Blue_StrategyCenter_Views.png) | [图板](Sheets/Red_StrategyCenter_Views.png) | [三档](Sheets/Blue_StrategyCenter_LODs.png) | [三档](Sheets/Red_StrategyCenter_LODs.png) |

75张原生 Blender 图片包括48张2K效果/正交图、24张2K LOD1/2补图、两张4K单队组合、一张4096×2600两队对照。图板只排版原生渲染。

## 保持与检查

- 独立打开保存后的候选 blend，60个本体/描边网格与来源的几何、法线、UV、权重摘要一致；36档调色检查及36档三档/线稿节点检查通过。十二套骨架继续沿用各自原骨名、层级和参考姿态。
- 22个原 Action 留存在文件中；六类建筑共21个动作由两队各自骨架继续兼容使用，无人机原动作保留。本轮未重制、删减或重新烘焙动画；既有完整视频见[原B_v1图板和动画](../review_B_v1.html)。
- 助手已查看两队全套48个核心视图及36个同机位LOD样本；色块、结构和视图关系一致。助手视觉检查与用户审核分别记录。
- 本轮沿用B_v1几何，原[24项本体预算差额](../Production_B_v1/Budget_Exceptions.md)和部分真实壳覆盖弱于参考的差异继续登记，原面数上限保持。源UV线稿中的部分不规则纹理没有在此次调色中修复。
- Floor、Lamp、Light、Drone颜色保持；本轮没有导入UE或接入玩法。`/Game/GuLiStrike/Buildings/SSFStylized` 仍是已完成存储交付的B_v1。

## 版本与决定

来源 blend SHA256：`6f386c2e4782ce6ab6e81ae0f2380a6345fa05cdc6fc3dcf9d260c13ed3a806e`。当前候选 SHA256：`5cee0988de1a4201cd9eb60de9cbd30ccddd108090b99b3e86f50c4bf2d584b6`。当前仅有调色与Blender预览指令，**B_v2待用户针对实际成品决定**；先前B_v1正式存储放行不覆盖本候选。

本次停在用户指定的Blender审核阶段。[制作技能](../../../../.agents/skills/guli-model-production/SKILL.md)与[美术规范](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md)的对应流程是“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”。当前已完成可审产物，用户决定后才推进本版正式UE更新。

[用户附图与指令](user_reference_instruction.json) · [候选清单](candidate_manifest.json) · [独立回读](native_validation.json) · [助手视觉记录](visual_qa.json) · [全部原生图片](preview_inventory.json) · [Blender已打开](interactive_open_status.json)
