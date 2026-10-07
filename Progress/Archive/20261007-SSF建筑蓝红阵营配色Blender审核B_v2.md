---
schema: guli-progress/v1
id: ARC-20261007-001
work_id: ''
kind: archive
role: root
title: SSF建筑蓝红阵营配色Blender审核B_v2
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-07'
updated: '2026-10-07'
summary: 十二个蓝红建筑配色副本已在实际Blender打开，75张原生渲染和独立回读完成；B_v2待用户审核，UE保持B_v1。
next_action: 用户审核SSF_TeamPalette_B_v2两队配色，版本通过后再处理正式UE更新。
relations:
  work_items: [WORK-20261005-003, WORK-20260917-001]
status_note: 本次是Blender配色交付和审核记录，非UE更新或玩法接入；既有预算差额保留，不将技术检查记为用户通过。
---

# 2026-10-07：SSF 六座建筑蓝红阵营配色候选

用户明确“蓝色方所有建筑改成图1配色，红色方把所有建筑改成图二配色，先放Blender给我审核”，以两张实际建筑截图指定两队配色。已使用此前B_v1生产模型建立`SSF_TeamPalette_B_v2`，没有以生成图片代替模型。来源SHA256 `6f386c2e4782ce6ab6e81ae0f2380a6345fa05cdc6fc3dcf9d260c13ed3a806e`，候选SHA256 `5cee0988de1a4201cd9eb60de9cbd30ccddd108090b99b3e86f50c4bf2d584b6`；原附件及哈希见[指令记录](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/user_reference_instruction.json)。

## 变更清单

| 文件 / 资产 | 变更 |
|---|---|
| `ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend` | 蓝红各六座，36本体LOD、24描边网格、12原兼容骨架；保留历史分件源 |
| `TeamPalette_B_v2_20261007/Textures` | 十二张新2K基础色图集，打包入候选blend |
| `TeamPalette_B_v2_20261007/Renders`、`Sheets` | 75张实际模型渲染，完整效果/正/左/背及三LOD图板 |
| `TeamPalette_B_v2_20261007/Review_B_v2.md`、JSON记录 | 查看入口、指令来源、候选哈希、独立检查、视觉证据、未审状态 |
| 当前需求、开发、规范、台账及索引 | 登记新队色方向和待审版本，保留B_v1已完成交付事实 |

## 配色与实现

蓝方使用图1橙`#EE9D58`、蓝灰`#274E61`、奶油白`#FEE4D9`；红方使用图2莓红`#A34053`、浅粉`#E3B6B1`、橙`#EE9D58`。从对应原材质读取色值，保留各建筑功能分区；蓝方空军基地、红方指挥中心是原色锚点。其他机械框架使用原参考少量暗色，军工厂/战略中心框架映射为蓝灰/莓红。线稿遮罩、真实近中描边壳、固定光向、三档因子及阈值保持；没有进行新减面。

默认Blender场景`Review_Blue_Red_Buildings`按实际尺寸左蓝右红；各单建筑场景可近看。实际交互窗口已加载候选文件并启用渲染视口，见[打开记录](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/interactive_open_status.json)。

## 资源清单

| 最终项目内文件夹 | 来源 | 内容与用途 | 依赖与边界 |
|---|---|---|---|
| `ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/Inputs` | 用户Temp目录两张原PNG附件 | 原样保留蓝红配色参考与哈希 | 只作参考，非生产贴图；无外部Actor/Object或示例地图迁移 |
| `ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007` | 项目内冻结`Production_B_v1` | 两队实际Blender、图集、渲染与审核记录 | 原遮罩/ORM已在blend中保留；无新增UE导入 |
| `/Game/GuLiStrike/Buildings/SSFStylized` | 上一轮已完成的B_v1正式交付 | 现有105正式资源保持 | 本轮未重导入、替换或接入游戏；Floor/Lamp/Light/Drone未调色 |

## 验证与审核

[独立Blender回读](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/native_validation.json)核对60网格的几何/UV/法线/权重摘要、36档配色及36档三阶着色和线稿节点、12套参考骨架与22个保留Action。保存后来源与候选哈希一致，Blue AirBase/Red CommandCenter原色保持。

[原生渲染清单](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/preview_inventory.json)保存75图的尺寸和哈希；[助手视觉检查](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/visual_qa.json)查看48核心视图与36LOD样本、两队总览及组合图，视图色块与结构保持一致。没有重新做22动作完整视频、FBX回读、UE检查或实战性能测量，因为当前指令为先审核Blender配色，几何和原动作未改。

**用户对B_v2尚未给出通过决定。** 原B_v1正式存储放行继续归属于其具体版本，当前UE保持旧交付。当前[审核入口](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/Review_B_v2.md)与[开发](../DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md)已同步为待审核。项目规范仍v1.3。

## 遗留边界

原24项本体预算差额、预算内描边壳局部覆盖较弱和源UV线稿少量不规则保持记录，未宣称预算全部达到原目标。下一步是用户审实际B_v2配色，决定后再推进正式UE更新；不修改玩法或运行PIE。

既有美术规范需求与台账超过建议文档预算，可另按资产/审核历史拆分；本轮仅登记当前修订，不自动改写冷归档或拆分历史。
