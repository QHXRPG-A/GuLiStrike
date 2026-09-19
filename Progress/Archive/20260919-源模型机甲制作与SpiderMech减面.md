---
schema: guli-progress/v1
id: ARC-20260919-007
work_id: WORK-20260919-004
kind: archive
role: root
title: 源模型机甲制作与SpiderMech减面
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-09-19'
updated: '2026-09-19'
summary: 基于 10 个实际源网格完成八个 Blender 成品候选，SpiderMech 839778→19656 三角面，轻型机甲封舱，红蓝降低饱和度。
next_action: 用户审阅真实模型 B；按具体意见继续调整。
relations:
  work_items: [WORK-20260919-004]
status_note: A 已由开始制作指令放行，实际 B 尚待审核；原 UE 资产只读，未导入或替换。
---

# 源模型机甲制作与SpiderMech减面

用户明确：“blender已开，开始制作，根据源模型制作，SpiderMech 也是，在原模型和参考图基础上减面并制作”。该指令放行现有六张参考进入实际 Blender 制作，不预填尚未展示的 B 通过。

从源码版 UE5.7 只读导出 10 个原网格、材质与纹理、骨架及轻型机甲组件/挂点。四机体、三武器和独立导弹均沿用源网格作为基础。SpiderMech 先减至 21,999，再整理上部单骨骼装甲大面，最终 19,656 三角面，减少 97.66%；保留六条附肢、长尖足、尾部、外露液压结构、299 根骨骼及 10 个材质槽。保留原 UV 层，局部整理新面投射回原 UV；高密源、可编辑减面和装甲整理过程另存。

轻型机甲移除驾驶员及舱内小件，新增封闭装甲、散热片和接缝；保留原组件变换、单枪/肩甲的不对称装配。Mecha_01 与蓝色武器使用灰蓝，红色武器使用灰红；其他色系保留。Blender 表面使用对应原受光类别的 Principled 预览，原 UE 材质网络未修改。

源结构勘误：FireWeapon_01 实际为三管，二维参考少画一根。按本轮源模型优先的指令保留三管，旧二维图作为风格和颜色证据保留。

交付：[真实效果、三视图及同角度灰模对照](../../ArtSource/Mechs/StyleUnification_20260919/Production_v1/Review_B_v1.html)、[Blender 源文件](../../ArtSource/Mechs/StyleUnification_20260919/Production_v1/Mechs_Style_SourceBased_v1.blend)、[说明](../../ArtSource/Mechs/StyleUnification_20260919/Production_v1/README.md)、[交付清单](../../ArtSource/Mechs/StyleUnification_20260919/Production_v1/production_manifest.json)。全部成品展示图为实际 Blender 渲染。

七套独立骨架名称与父子关系匹配原 UE 快照；补回 FBX 根骨对象化造成的骨名和权重遗漏。当前成品无无效骨骼组或无权重顶点，基本姿态检查后恢复原姿态。SpiderMech 5037 点单向采样表面距离中位数 1.84 mm、P95 15.20 mm，最大 158.09 mm；大偏差来自装甲凹槽简化，不等于完整动画或双向误差验收。

B 待用户审阅；完整源动画、碰撞、UE 材质显示、玩法和性能未验证。没有修改正式 UE 包、C++、构建配置或玩法，未运行编译。规范仍为 v1.2。持续状态见[制作记录](../DevelopmentDocumentation/20260919-两组机甲资源风格统一参考.md)。
