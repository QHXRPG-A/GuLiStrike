# Ship 第二批三组件 — 实际成品审核 B v2

当前状态：三件原模型材质v2已完成，提交[实际成品审核B](Review_B_v2.html)。用户于2026-09-17回复“开始制作”，[A通过记录](approval_A_20260917.json)冻结了参考版本与输入哈希。[参考总览](Review_A_v1.html)保留A提交时状态；原模型仍是形体、尺寸和机械接口的依据。

| 组件 | 本次审核版本 | 设计板 | 状态 |
|---|---|---|---|
| 自动炮 Autocannon | v2 | [效果图 + 正/右侧/背](References/Autocannon_Reference_v2.png) | A 通过 |
| 三联炮 Triple_Barrel_Turret | v1 | [效果图 + 正/右侧/背](References/Triple_Barrel_Turret_Reference_v1.png) | A 通过 |
| 单管炮 Single_Barrel_Turret | v2 | [效果图 + 正/右侧/背](References/Single_Barrel_Turret_Reference_v2.png) | A 通过 |

三件共用浅色装甲、深蓝灰机械层、钢件与窄线条。自动炮采用赭黄护罩和侧甲，三联炮将赭黄集中在原有上盖和侧板，单管炮采用青蓝护罩及侧甲。框线依附原结构，光影按三档明暗表现。原几何、炮管数、安装接口、骨骼及炮口坐标均不随参考设计重建。

## 审核与制作边界

用户明确要求先审核效果图与三视图，再根据图纸和原模型调整贴图和框线，随后审核实际成品。本批 A、B 独立记录，首批通过记录只作为风格参考。

- A：三件通过，用户消息“开始制作”，具体版本见上表。
- 原模型贴图/框线生产：v2完成，原顶点、拓扑、法线与骨骼接口保持一致。
- B：v2待用户审核；页面提供实际Blender渲染、灰模及连续俯仰演示。
- 最终FBX：待B通过后导出并回读。
- UE：本阶段仅运行源码版命令行只读提取，正式导入和运行验证未运行。

## 来源与版本

[原型快照](Source/source_snapshot_v1.json)记录当前 Blueprint、SkeletalMesh、Root → BarrelPitch、安装变换和三/三/一个炮口 Socket。三件骨骼源与 FBX 位于 `Source/ExistingAuthoring/`，原始静态参考 FBX 位于 `Source/FBX_static_v1/`。

[原型视图清单](Source/prototype_render_manifest_v1.json)登记三件各五张中性视图（效果角度、正、右侧、背、俯视）及哈希。检查副本使用中性显示材质，不改动原源文件或当前交互 Blender 工作文件。

设计板采用**内置 image_gen 模式**生成，输入为实际原模型视图与首批已审风格。提示词完整保存在[首版提示词](References/prompts_v1.json)、[修订提示词](References/prompts_v2.json)和[自动炮局部配色修订](References/prompts_v2_autocannon_retry.json)。内置生成原始路径见 `References/generated_outputs_v1.json`、`References/generated_outputs_v2.json`，项目副本和每次成功输出保留。

自动炮 v1 的右视侧甲、正视套环与效果图存在分色差异，v2 已统一；单管炮 v1 的侧盖、连接处和后部色块在 v2 统一。三联炮使用 v1。旧版仅留档，本次审核只针对表中所列版本。

[审核来源清单](References/reference_A_manifest_v1.json)冻结A提交时的候选、来源、尺寸、骨骼、挂点和 SHA-256；[打包核对记录](reference_package_validation_v1.json)核验三件原骨骼源及全部十五张中性原型视图哈希。A决定随后单独写入通过记录，历史候选文件保持原样。二维图纸不授权改造原网格。

## 实际成品与检查

[总览Blender](Production/v2/ShipComponentStyle_Batch02_Overview.blend)包含自动炮、三联炮、单管炮三个场景。每件另有可编辑单件源，主体分别1700/2497/1268三角面，各1材质槽、3张2K贴图。文件和图像SHA-256见[成品清单](Production/v2/Review_B_Manifest.json)，[保存后回读](Production/v2/saved_blender_readback.json)确认原主体几何与内嵌贴图。

`Production/v2/Textures/`保留BaseColor、ORM及独立LineMask。原UV保留，新增`SC_PaintUV`和`SC_LineUV`；`SC_PaintPaletteIndex`、`SC_EditablePaintColor`及各件`paint_regions.json`用于继续编辑分色。外轮廓位于`OUTLINE_TOGGLE`，可关闭且后续不导出。各件`material_parameters.json`记录配色、三档明暗、线宽与后续材质重建所需参数。

`Previews/v2/`包含实际效果、正/右侧/背/俯视、灰模、关闭框线、换光和缩小图，以及−15°、0°、30°、75°的彩色/灰模姿态。三段连续运动视频各97帧、24fps、960×720，[视频回读](Production/v2/motion_video_readback.json)均通过。`Root → BarrelPitch`、单骨骼权重1和3/3/1个炮口保留；逐度核对91个姿态通过。

内部v1保留，B只审v2；v2调整单管炮连接凹槽与护罩下方机械面的分色。成品按真实原网格渲染，二维参考的笔触或透视差异不转化为几何修改。文档检查见`documentation_validation_B_v2.json`；B和最终FBX尚未放行，UE正式验证未运行。

关联：[需求](../../../../Progress/RequirementDocument/20260917-Ship第二批三组件贴图与框线制作.md)、[开发与审核](../../../../Progress/DevelopmentDocumentation/20260917-Ship第二批三组件贴图与框线制作.md)。
