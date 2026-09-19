---
schema: guli-progress/v1
id: ARC-20260919-009
work_id: WORK-20260919-004
kind: archive
role: root
title: SpiderMech恢复原网格并整理色块
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-09-19'
updated: '2026-09-19'
summary: 用户取消SpiderMech减面后恢复839778面原网格，仅整理连续色块和材质风格；几何、UV、权重、法线保存回读一致，v2成品待视觉审核。
next_action: 用户审阅原网格风格版v2。
relations:
  work_items: [WORK-20260919-004]
status_note: v1被用户反馈碎面，最新“不再减面，只改色块和美术风格”撤销旧低模预算。技术一致性通过，不代替B视觉审核；未导入UE或做动画、性能验证。
---

# 2026-09-19：SpiderMech恢复原网格并整理色块

用户给出四张近景，反馈“SpiderMech几乎不能用，全是碎面，看看是不是材质的问题”；后续明确“不再减面，只改色块和美术风格”。该指令替代此前SpiderMech低模要求。原19,656面v1及15,000–25,000预算撤回，历史[制作归档](20260919-源模型机甲制作与SpiderMech减面.md)保留，当前交付采用本记录的新版本。

实际诊断：旧成品材质Normal输入未连接；原贴图被转换为逐面颜色，长三角形分到不同黄、蓝、黑色，减面和平面着色又放大碎片外观。同机位灰模对照保存在新目录。未继续推进减面或用新的法线贴图掩盖原缺陷。

新文件`ArtSource/Mechs/StyleUnification_20260919/Production_v2_Spider/SpiderMech_SourceStyle_v2.blend`直接复制完整源网格到Spider工作对象，仅改颜色属性和材质参数。恢复839,778三角面、533,878顶点；10个材质槽、299骨骨架和原蒙皮保留。按原贴图确定完整表面零件的主色，以蓝灰、赭黄、深灰大色块替代磨损噪声与逐面异色。继续使用对应原Default Lit的Principled受光预览，保留原色系约束。

保存并在交互Blender重新打开后，顶点位置、拓扑、UV和材质槽索引哈希与源副本一致，权重和导入法线也一致；工作网格只有Armature修改器。旧v1主blend未覆盖，已接入Ground的轻型装甲与UE资产未修改。没有C++变更，无需本轮重新构建。

[效果与三视图](../../ArtSource/Mechs/StyleUnification_20260919/Production_v2_Spider/Review_Spider_v2.html)、[近景](../../ArtSource/Mechs/StyleUnification_20260919/Production_v2_Spider/SpiderMech_OriginalStyle_Close.png)、[保存回读](../../ArtSource/Mechs/StyleUnification_20260919/Production_v2_Spider/source_style_report.json)、[制作说明](../../ArtSource/Mechs/StyleUnification_20260919/Production_v2_Spider/README.md)。展示均为实际Blender渲染，交互窗口已打开`Review_SpiderMech`。v2仍待用户视觉审核，不记录为已通过B；不扩展其他候选审核或UE导入。
