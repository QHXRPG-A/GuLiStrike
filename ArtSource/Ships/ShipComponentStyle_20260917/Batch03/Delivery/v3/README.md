# Ship 第三批支援组件 v3 — 最终交付

用户已明确“三件均通过 B，导出 FBX”。无人机保留薄壁外侧白色僚机与赭黄箭头，干扰装置采用深蓝白黄，护盾采用绿色；原模型几何、原点、法线和安装尺寸保持。规范1.0，交付范围为Blender与FBX。

[交付总览](Review.html) · [文件与哈希清单](Delivery_Manifest.json) · [审核B决定](Parameters/approval_B_20260917.json)

| 组件 | LOD0主体三角面 | FBX | 可编辑源 |
|---|---:|---|---|
| 无人机发射舱 | 1254 | [静态 FBX](FBX/SM_SC_Drone_LaunchBay_Styled.fbx) | [Blender](Blender/SC_Drone_LaunchBay_Styled.blend) |
| 电子干扰装置 | 550 | [静态 FBX](FBX/SM_SC_Electronic_JammingDevice_Styled.fbx) | [Blender](Blender/SC_Electronic_JammingDevice_Styled.blend) |
| 护盾发生器 | 312 | [静态 FBX](FBX/SM_SC_Shield_Generator_Styled.fbx) | [Blender](Blender/SC_Shield_Generator_Styled.blend) |

[总览Blender](Blender/ShipComponentStyle_Overview.blend)包含三个独立Scene，切换Scene查看各件，保留各自原始原点、物体变换及尺寸。每份FBX只有一个主体网格、一个材质槽，没有额外描边壳、摄影棚、骨骼或演示动画。

## 贴图、框线和光影

- 每件BaseColor、ORM、LineMask各一张2048×2048，共九张，内嵌Blender并在Textures另存PNG。材质与贴图相对路径可随整个交付目录搬移。
- BaseColor为sRGB纯色及标识，不包含场景阴影；ORM为Non-Color，R=1（没有烘焙AO）、G=粗糙度、B=金属度；LineMask为Non-Color独立内线。
- Blender使用EEVEE场景光照和自阴影驱动三档明暗。内线节点`Line_Strength`可关闭；外轮廓在`OUTLINE_TOGGLE`集合，描边壳面数与各主体相同，单独计数。
- Blender保留全部原UV并追加SC_PaintUV和SC_LineUV；FBX以UV0=SC_PaintUV、UV1=SC_LineUV，其后保留原UV。护盾有四个原UV通道，另两件各三个。
- FBX基础材质用于BaseColor预览。完整三渲二光影、内线和外轮廓保留在Blender，后续引擎按各件`Parameters/*_material_parameters.json`及贴图重建。

## 无人机标识与静态接口

标识在薄长侧壁朝外的-X面，原顶面和厚侧壁旧标识已移除；飞出方向为Blender +Y / UE -Y。见[外侧效果](Previews/Drone_LaunchBay_exterior_hero.png)、[用户标注](References/Drone_LaunchBay_User_ExteriorMarking_20260917.png)、[可编辑SVG](Parameters/Drone_LaunchBay_launch_marking.svg)及[落点记录](Parameters/Drone_LaunchBay_launch_markings.json)。图标只在贴图中，不增加模型或Socket。

三件均为静态模型，原网格没有骨骼或Socket。每件`Parameters/*_Sockets.json`与[原型快照](Parameters/source_snapshot_v1.json)保留原Blueprint、模型引用、安装变换和兼容挂点。无人机的`wingman_bay_1/2`是Blueprint兼容舰体挂点名，不是网格Socket；另两件该数组为空。FBX在`GuLiStrike_Sockets_JSON`记录空Socket列表，在`GuLiStrike_CompatibleSockets_JSON`保留兼容名。

## 检查与版本

[FBX回读](Validation/FBX_Readback_Summary.json)复用既有项目工具，三件尺寸、原点、几何、UV、面数、材质槽和静态接口均通过。最大几何误差0.000002384米，原点误差和UV数值误差均为0。[可迁移Blender回读](Validation/Portable_Blender_Readback.json)确认三件与总览、内嵌/外部贴图和三渲二节点有效。

获批Production/v3源与审核页保持原哈希。交付Blender仅调整批准元数据及相对资源路径；Parameters中的历史候选清单和ApprovedSource报告保留当时状态，当前状态以B决定及Delivery_Manifest为准。历史清单路径相对于项目Batch03制作根，交付本身的当前路径以本页和最终清单为准。Tools保存制作与导出脚本副本，重建时从项目Scripts入口运行。

本轮UE正式接入及运行验证未运行。CIWS与第二批成品未修改。
