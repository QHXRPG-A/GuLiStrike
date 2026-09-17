---
schema: guli-progress/v1
id: ARC-20260916-005
work_id: WORK-20260916-004
kind: archive
role: root
title: Ship动漫低模风格Blender样板交付
areas: [ship, assets, art]
status: recorded
verification: passed
created: '2026-09-16'
updated: '2026-09-16'
summary: 保留当前Dreadnought主形状，完成13,314三角面、六色与三档明暗的独立Blender样板，并在当前Blender中显示供用户评审。
next_action: 用户评审样板；按反馈调整配色、细节或三渲二强度。
relations:
  work_items: [WORK-20260916-004]
status_note: 已完成本轮Blender制作和视觉检查；用户美术定稿与UE接入不在本次完成范围内。
---

# 2026-09-16：Ship 动漫低模风格 Blender 样板

用户要求保留目前 Ship 的大致形状，将其改为动漫化、低模、简约纹理、大色块、清晰轮廓及适度三渲二，并放入已经打开的 Blender。

通过 UE 资产接口读取当前玩法蓝图 `BP_CombatAvatarFly01`，导出实际引用的 `SM_Dreadnought_Hull` 与原参考贴图。基于独立副本减少内部微小装饰和共面边，三角面由 16,312 降到 13,314。长宽高保持约 193.780 × 450.232 × 69.847 m；保留顶点对源网格的最大位置偏差为 0 m。

新版采用浅色、蓝色、青灰、深蓝、琥珀色和青色六种材质，规则条带代替细密表面纹理。EEVEE 三档明暗强化平面与体积。当前 Blender 显示 `Ship_Stylized_Study`，同一文件保留原模型和并排对照场景；原启动场景仍在文件内。

- [工程和说明](../../ArtSource/Ships/ShipStylizedStudy_20260916/README.md)
- [主预览](../../ArtSource/Ships/ShipStylizedStudy_20260916/Previews/01_stylized_hero.png)
- [新旧对照](../../ArtSource/Ships/ShipStylizedStudy_20260916/Previews/06_original_stylized_comparison.png)
- [实施与证据](../DevelopmentDocumentation/20260916-Ship动漫低模风格Blender样板.md)

已查看斜视、俯视、侧视、尾部和对照渲染，并读取当前 Blender 视口确认实际展示。工程由 Blender 5.2.2 LTS 保存，参考贴图已打包。UE 中原模型、材质和玩法资产未替换；三渲二材质仅完成 Blender 评审版本。未新增测试或修改原生代码，因此未触发 UE 编译。
