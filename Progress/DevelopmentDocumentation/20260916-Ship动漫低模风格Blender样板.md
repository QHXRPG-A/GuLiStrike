---
schema: guli-progress/v1
id: DEV-20260916-004
work_id: WORK-20260916-004
kind: development
role: root
title: Ship动漫低模风格Blender样板 — 制作与交付
areas: [ship, assets, art]
status: done
verification: passed
created: '2026-09-16'
updated: '2026-09-16'
summary: 从当前Dreadnought舰体制作独立Blender样板，完成六色材质和三档明暗；内部线条改为4K遮罩，保留外轮廓壳，总计16,402三角面。
next_action: 等待用户美术评审；后续UE材质适配与模型替换另按确认范围实施。
relations:
  requirement: REQ-20260916-004
status_note: Blender样板交付完成；验证范围为来源、几何尺寸、视觉预览和当前编辑器展示，不包含UE风格材质适配。
---

# Ship 动漫低模风格 Blender 样板

## 实施

1. 通过 UE 实时脚本读取两种 Ship 蓝图的实际网格引用，以当前玩法使用的 `BP_CombatAvatarFly01` / `SM_Dreadnought_Hull` 为源；原资产经 `AssetExportTask` 导出。
2. Blender 启动场景为空白默认场景，另建原型和评审场景，保留已有默认场景。
3. 在独立网格副本中删除面积小于 25 平方米、最大尺寸小于 8 米且处于舰体内部区域的小连通件；不做全局减面或比例修改。将两度内共面边合并，保留材质边界。
4. 六个纯色材质槽组织装甲、结构、凹槽、标记和喷口。蓝色两侧装甲用规则条带；大面积表面不再采样原细密贴图。
5. EEVEE 的 `Diffuse → Shader to RGB → 三档色阶 → 颜色相乘 → Emission` 形成随光照变化的明暗层次。材质属于 Blender 样板，不能当作已完成的 UE 材质。
6. 原网格使用从 UE 导出的 BaseColor、ORM、Normal、Emissive 重建参考材质，原贴图打包到 `.blend`；源资产没有修改。

## 结果

| 项目 | 结果 |
|---|---|
| 原三角面 | 16,312 |
| 新三角面 | 13,314，减少约18.4% |
| 新模型尺寸 | 193.780 × 450.232 × 69.847 m |
| 保留顶点至源网格最近顶点最大距离 | 0 m |
| 尺寸最大差 | 约0.000031 m，浮点转换误差 |
| 渲染 | Blender 5.2.2 LTS / EEVEE |
| 预览 | 斜视、俯视、侧视、尾部、原贴图和并排对照 |

## 交付入口

- [Blender 工程](../../ArtSource/Ships/ShipStylizedStudy_20260916/GuLiStrike_Ship_AnimeStudy.blend)
- [使用说明](../../ArtSource/Ships/ShipStylizedStudy_20260916/README.md)
- [几何检查记录](../../ArtSource/Ships/ShipStylizedStudy_20260916/study_report.json)
- [来源记录](../../ArtSource/Ships/ShipStylizedStudy_20260916/source_manifest.json)
- [主预览](../../ArtSource/Ships/ShipStylizedStudy_20260916/Previews/01_stylized_hero.png)
- [新旧对照](../../ArtSource/Ships/ShipStylizedStudy_20260916/Previews/06_original_stylized_comparison.png)

制作脚本：[源导出](../../Scripts/Blender/export_ship_style_source.py)、[样板制作](../../Scripts/Blender/build_ship_stylized_study.py)、[渲染](../../Scripts/Blender/render_ship_stylized_study.py)。制作脚本要求源 FBX 已导入 `Ship_Original_Reference`，不在已有样板场景上盲目重复构建。

## 完成情况

- [x] 源资产确认与导出。
- [x] 原形保留、内部细节简化与可编辑色块材质。
- [x] 多视角和同灯光新旧对照视觉检查。
- [x] 工程保存并在已打开的 Blender 显示。

未新增自动化测试，未修改原生代码；不触发 UE 编译门禁。最终美术认可待用户评审。

## 追加：深蓝线稿

用户要求增强动漫感后，在 `SS_03_Lineart` 集合建立独立线稿辅助层：反向壳体形成随视角变化的外轮廓；对主要结构的真实折边与开放边界建立三维曲线。过滤面积不足350平方米的碎小构件，折线夹角超过28度、长度至少2.8米；深蓝凹槽不再加密内部折线。最终为893条折线、155条边界线，半径分别0.135m和0.19m，外轮廓偏移0.42m。

舰体网格哈希在追加前后一致，实体模型仍为13,314三角面；线稿辅助层另计渲染成本。材质对阴影光线透明，摄影棚补光关闭投影，保留主光投影。原版与新版的并排场景同步追加线稿辅助层。

- [线稿主预览](../../ArtSource/Ships/ShipStylizedStudy_20260916/Previews/07_lineart_hero.png)
- [线稿检查记录](../../ArtSource/Ships/ShipStylizedStudy_20260916/lineart_report.json)
- [线稿追加脚本](../../Scripts/Blender/add_ship_stylized_lineart.py)
- [无描边工程备份](../../ArtSource/Ships/ShipStylizedStudy_20260916/GuLiStrike_Ship_AnimeStudy_NoLines.blend)
- [线稿追加归档](../Archive/20260916-Ship风格样板追加线稿.md)

## 追加：装甲内部线条烘焙遮罩

用户确认采用“内部线条贴图 + 独立外轮廓”。从实体曲线恢复全部1,048段原始结构边及0.135m/0.19m半径，在专用UV上按面内距离生成线稿覆盖率。直接曲线投射在折角处出现断线，最终使用逐面几何距离栅格化避免该问题；没有将灯光或阴影烘入遮罩。

- `T_Ship_Internal_LineMask_4K.png`：4096×4096、8位灰度、Non-Color；白色为线、黑色为原配色。16次/像素覆盖采样，8像素岛外扩展。文件已打包到主工程，同时保留独立PNG。
- 新增第四个UV通道 `SS_LineartUV`，只为626个带线条的表面分配空间，其余表面采样保留的黑色区域；原有三个UV通道未替换。
- 内部线色仍为深蓝 `#213D50`，在三档光照计算后混合，与原线稿色保持一致。五个舰体材质共享同一遮罩，喷口材质保持原样；材质内的 `SS_Lineart_Strength` 可调强度。
- 当前场景和并排场景的两个内部曲线对象均已移除。外轮廓壳及其0.42m偏移保留。
- 主体13,314三角面，描边壳3,088三角面，总16,402三角面 / 6,143多边形。相对实体线稿版本减少20,960三角面（56.1%），不含地面与对照模型。
- 舰体几何SHA-256仍为 `6b52d923c3564482e2dc6f574cd63c1be7afa331830f711b03aa64e2231ae92a`；外壳几何哈希也保持一致。

已检查斜视、俯视、侧视、尾部和原版并排渲染。结果通过脚本接入已打开的Blender并保存，保留用户视角；当前场景、材质采样、打包图像、面数及并排场景共享网格均已核对。

- [当前主预览](../../ArtSource/Ships/ShipStylizedStudy_20260916/Previews/12_baked_lineart_hero.png)
- [遮罩贴图](../../ArtSource/Ships/ShipStylizedStudy_20260916/Textures/T_Ship_Internal_LineMask_4K.png)
- [烘焙与几何记录](../../ArtSource/Ships/ShipStylizedStudy_20260916/lineart_bake_report.json)
- [实体线稿备份](../../ArtSource/Ships/ShipStylizedStudy_20260916/GuLiStrike_Ship_AnimeStudy_GeometricLines.blend)
- [遮罩生成脚本](../../Scripts/Blender/bake_ship_lineart_mask.py)、[实时接入脚本](../../Scripts/Blender/apply_ship_baked_lineart.py)
- [本次增量归档](../Archive/20260916-Ship内部线稿烘焙遮罩.md)

本次交付仍为Blender样板；未进行UE导入、材质适配或运行性能测试。减少几何的同时新增贴图采样，不将三角面减少比例等同于帧率提升。

## 关联

- [需求](../RequirementDocument/20260916-Ship动漫低模风格Blender样板.md)
- [阶段归档](../Archive/20260916-Ship动漫低模风格Blender样板交付.md)
