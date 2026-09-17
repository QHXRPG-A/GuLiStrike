# Ship 动漫低模风格样板

入口：`GuLiStrike_Ship_AnimeStudy.blend`。Blender 5.2.2 LTS，EEVEE 渲染。

文件包含三个评审场景，可通过 Blender 顶部的场景选择框切换：

- `Ship_Stylized_Study`：默认打开的新版，六种可编辑材质，三档明暗。
- `Ship_Original_Reference`：原网格和从 UE 导出的原贴图重建材质，用于参考；不等同于 UE 实机截图。
- `Ship_Compare_Original_Stylized`：同灯光并排对照，左原版、右新版。

鼠标中键可退出摄像机角度并自由旋转；小键盘 0 返回摄像机。请使用“渲染”视口着色查看三渲二效果。普通实体模式只显示网格。

## 改动

- 来源：当前 `BP_CombatAvatarFly01` 引用的 `SM_Dreadnought_Hull`，经 UE 资产接口导出 FBX。
- 保持船体比例和主要轮廓；删除一部分内部微小装饰，合并两度内的共面边。
- 三角面：16,312 → 13,314，减少约 18.4%。
- 尺寸：约 193.780 × 450.232 × 69.847 米。保留顶点没有移动，整体尺寸差仅为浮点误差。
- 六色：浅色装甲、蓝色装甲、青灰结构、深蓝凹槽、琥珀色标记、青色喷口。
- 大色块与规则条带由材质生成，装甲内部线条使用独立灰度遮罩；不采样原版细密纹理和凹凸贴图。
- 漫画明暗由 EEVEE `Shader to RGB` 和三档色阶完成。材质可继续调整，当前未制作 UE 对应材质。

## 文件

- `Source/`：原舰体 FBX 及四张导出的原参考贴图。
- `Previews/01_stylized_hero.png`：新版主要预览。
- `Previews/02_stylized_top.png`、`03_stylized_side.png`、`04_stylized_rear.png`：轮廓评审。
- `Previews/05_original_textured.png`：原贴图重建参考。
- `Previews/06_original_stylized_comparison.png`：并排对照。
- `source_manifest.json`：UE 来源与材质引用。
- `study_report.json`：几何和尺寸检查结果。

制作脚本：项目 `Scripts/Blender/export_ship_style_source.py`、`build_ship_stylized_study.py`、`render_ship_stylized_study.py`。

此版本供 Blender 美术评审。原 UE 模型、材质和玩法资产未替换。风格最终认可由用户评审决定。

## 实体线稿版（历史备份）

首轮按用户追加要求加入较细的深蓝线稿：主要舰体外轮廓、装甲折边和可见接缝。该阶段的 `SS_03_Lineart` 集合包含三个独立辅助对象，完整工程已保存在 `GuLiStrike_Ship_AnimeStudy_GeometricLines.blend`。

- `SS_Ink_Outer_Contour`：外轮廓，Displace 修改器的 Strength 控制宽度；当前为 -0.42 m。
- `SS_Ink_Armor_Creases`：主要折线，Curve 的 Bevel Depth 当前为 0.135 m。
- `SS_Ink_Panel_Boundaries`：装甲边界，Bevel Depth 当前为 0.19 m。

线条使用独立几何辅助层，可随自由视角查看，不依赖固定摄影机。实体舰体的顶点和拓扑没有修改，仍为 13,314 三角面；线稿辅助层另有渲染成本。线稿不参与投影，摄影棚补光关闭投影以保持主光阴影清楚。

该阶段预览为 `Previews/07_lineart_hero.png` 至 `11_lineart_comparison.png`。未加线稿的工程保存在 `GuLiStrike_Ship_AnimeStudy_NoLines.blend`，原预览也保留。追加脚本为项目 `Scripts/Blender/add_ship_stylized_lineart.py`，几何一致性与线条参数记录在 `lineart_report.json`。

## 当前版本：内部线稿遮罩 + 独立外轮廓

当前入口仍为 `GuLiStrike_Ship_AnimeStudy.blend`。装甲折线和边界线已烘焙为 `Textures/T_Ship_Internal_LineMask_4K.png`，4096 × 4096、8 位灰度、Non-Color，白色表示深蓝线稿，黑色保留原色块。贴图另存为 PNG，并打包在 Blender 工程内。

- 使用第四个 UV 通道 `SS_LineartUV`。仅 626 个带线条的表面分配图集空间，其余表面采样保留的黑色区域；原三个 UV 通道保留。
- 从原有 1,048 段结构线的位置与半径生成遮罩，每像素 16 次覆盖采样、8 像素岛外扩展，不烘焙场景灯光或阴影。
- 材质的 `SS_Lineart_Strength` 控制内部线条强度，0 关闭、1 为当前值；`SS_Internal_Line_Mask` 指向贴图。内部曲线对象已从当前样板及并排场景移除。
- `SS_03_Lineart` 现在只包含外轮廓壳 `SS_Ink_Outer_Contour`，仍可独立关闭；其 Displace Strength 保持 -0.42 m。关闭该集合不会关闭贴图内线。
- 主体 13,314 三角面 + 描边壳 3,088 三角面 = **16,402 三角面**，Blender 多边形数为 6,143。相对实体线稿版 37,362 三角面减少 20,960（56.1%）。统计不含地面和对照模型。
- 舰体顶点、拓扑与外轮廓壳几何哈希均与实体线稿版一致；并排场景共享舰体网格和材质，同步更新。

当前预览为 `Previews/12_baked_lineart_hero.png` 至 `16_baked_lineart_comparison.png`。检查记录在 `lineart_bake_report.json`。项目 `Scripts/Blender/bake_ship_lineart_mask.py` 从实体线稿备份生成遮罩、UV 数据和候选工程；`apply_ship_baked_lineart.py` 将检查后的结果接入当前 Blender，保留用户视角。

新增一个贴图采样取代内部曲线几何，实际游戏成本还取决于材质和目标引擎设置；本次仍只交付 Blender 样板。
