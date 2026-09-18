# Ship 第三批 — 机库与支援组件最终交付

无人机发射舱、电子干扰装置、护盾发生器的材质v3均获用户审核B通过：“三件均通过 B，导出 FBX”。三套静态FBX、可编辑Blender、贴图与预览已导出并通过回读。

- [完整交付ZIP](ShipComponentStyle_Batch03_20260917_v3_Delivery.zip)
- [最终交付总览](Delivery/v3/Review.html) · [使用说明](Delivery/v3/README.md)
- [总览Blender](Delivery/v3/Blender/ShipComponentStyle_Overview.blend)
- [文件清单](Delivery/v3/Delivery_Manifest.json) · [FBX回读](Delivery/v3/Validation/FBX_Readback_Summary.json)
- [B通过记录](approval_B_20260917.json) · [ZIP核对](delivery_validation_v3.json)

交付包含三套静态FBX、四份Blender、九张2K贴图及材质参数和实际预览。原网格、尺寸、法线及安装原点保持；无人机僚机标识在薄壁外侧、干扰装置深蓝白黄、护盾绿色。UE正式接入未运行；完整三渲二与框线在Blender保留，后续引擎按参数重建。

获批制作源：[原材质总览Blender](Production/v3/ShipComponentStyle_Batch03_Overview.blend)，各件单独源和九张内嵌/外置2K贴图位于`Production/v3/`，哈希保持。最终可迁移副本位于`Delivery/v3/`，只调整批准元数据及相对资源路径。总览用三个Scene维持原始安装原点和尺寸；主体无几何修改器，原顶点、拓扑、法线、物体矩阵和原有UV回读一致。`SC_PaintUV`用于色块、`SC_LineUV`用于独立内线，外轮廓在`OUTLINE_TOGGLE`，原分件与分色属性可编辑。

证据：[A通过记录](approval_A_20260917.json)、[B成品与哈希清单](Production/v3/Review_B_Manifest.json)、[保存后回读](Production/v3/saved_blender_readback.json)、[打包核对](review_B_package_validation_v3.json)。每件主体一个材质槽，保留各三张2K BaseColor/ORM/LineMask；三档明暗来自实际EEVEE场景光照和自阴影，后续目标引擎按所附参数重建等效材质。

| 组件 | 参考版本 | 功能色 | 原网格三角面 | 静态模型尺寸（米） |
|---|---|---|---:|---|
| 无人机发射舱 | v2 | 赭黄 | 1254 | 15.55 × 50.23 × 15.19 |
| 电子干扰装置 | v3 | 深蓝 / 白 / 黄 `#23384A / #DDE6E6 / #E0BC49` | 550 | 12.91 × 7.41 × 5.39 |
| 护盾发生器 | v3 | 叶绿 `#65955D` | 312 | 7.47 × 5.87 × 9.98 |

以上尺寸从当前原型读取，不是按设计图推测。三件网格自身均无Socket和骨骼，安装原点及Blueprint相对变换保持原值。Drone的`compatible_sockets`为`wingman_bay_1/2`，另两件当前Blueprint该数组为空；功能配置表中的舰体插槽与网格自带Socket分别保留，不混为一类。

## 无人机飞出标识

用户明确要求“无人机发射舱需要有僚机飞出标识”，出口沿导轨指向原型效果图的右上方远端。[方向标注](References/Drone_LaunchBay_User_ExitDirection_20260917.png)已原样归档。实际制作期间又标注“这里是朝外的，图标放这里”：v3把白色僚机剪影和赭黄箭头移到薄长侧壁朝外面，移除原顶面与厚側壁旧标识，飞出方向保持Blender +Y / UE -Y。

见[位置标注](References/Drone_LaunchBay_User_ExteriorMarking_20260917.png)、[位置修订](surface_revision_20260917_exterior_marking.json)、[外侧效果](Previews/v3/Drone_LaunchBay_exterior_hero.png)、[外侧正视](Previews/v3/Drone_LaunchBay_left.png)。标识只写入BaseColor，原舱体、轨道、夹具不变；附[可编辑SVG](Production/v3/Drone_LaunchBay_launch_marking.svg)与[实际落点记录](Production/v3/Drone_LaunchBay_launch_markings.json)。

## 依据与源文件

- [UE只读快照](Source/source_snapshot_v1.json)：当前三件Blueprint和StaticMesh、尺寸、原始网格、材质、安装变换、Socket及导出FBX哈希；保护包前后文件状态一致。
- [提取方法与源工具](Source/capture_method_v1.json)：源码版UE5.7、只读提取及适配记录；早期采集失败日志仅保留诊断，最终快照`success=true`。
- [原型渲染清单](Source/prototype_render_manifest_v1.json)：原型FBX、三个中性检查`.blend`和十五张实际五视角图的哈希。检查副本不是正式材质成品。
- [参考与审核清单v3](References/reference_A_manifest_v3.json)：具体参考版本、输入哈希、用户标注、配色指示和A/B状态。
- [候选打包核对v3](reference_package_validation_v3.json)：原型FBX、检查Blender与十五张视图哈希核对。

源FBX保留于`Source/FBX_static_v1/`，原型检查Blender位于`Source/`，原型视图位于`Source/Previews/`，设计板位于`References/`。

## 设计版本与边界

设计采用**内置image_gen**，以每件实际效果角度及正/右侧/背视为结构依据，首批已审双联炮v4和CIWSv4提供表面风格。提示词保留于`prompts_v1.json`、`prompts_v1_launch_marker.json`、`prompts_round2.json`、`prompts_round3.json`；护盾v2将效果图中央外壳斜面改为浅色，与正视图一致，青蓝仍保留在中央头罩和两个前置小模块。

无人机首两次请求因参考数量超过工具五张限制未产生图片，随后将用户标注图作为效果角度输入，保留三张正交视图及一张风格参考。所有成功图像原样保留，不用程序重绘生成板。二维参考只决定表面风格，真实网格的细节与尺寸保持原值。

无人机v2统一效果图与右视图的深色长侧壁，并将同向僚机标识补到效果图侧壁；顶面主标识保持。该修订提示词见`References/prompts_drone_v2.json`，成功输出的原始路径见[生成记录](References/generated_outputs_final_candidates.json)。初版候选集为无人机v2、干扰装置v1和护盾v2。

2026-09-17用户要求三类设备色系区分并明确CIWS不改。参考阶段用内置image_gen对现有图板局部改色：干扰装置v2的双阵面和前盖板为紫晶紫，护盾v3的核心头罩及两个小模块为叶绿；CIWS维持已审青蓝`#348B9F`，无人机v2保持。效果图与三视图的对应色区同步，浅甲、深结构、线稿及光影沿用。当时的紫色候选随后被深蓝白黄替代，不是当前采用版本。

本轮[提示词](References/prompts_color_separation_v1.json)、[原始输出路径与SHA-256](References/generated_outputs_color_separation_v1.json)已保存。新图为`Electronic_JammingDevice_Reference_v2.png`和`Shield_Generator_Reference_v3.png`；旧图、旧清单和旧页面均保留。CIWS参考图哈希前后一致，CIWS源文件及已交付FBX未修改。

用户随后指定“干扰装置 改成深蓝、白、黄”，因此干扰v3取代紫色v2作为当前候选：深蓝阵面，白色外框与支座，黄色前盖板及两侧连接护罩；背筋保留钢色。使用同一内置工具编辑图板，对应色区在四视角同步，见[三色提示词](References/prompts_jammer_navy_white_yellow_v1.json)和[三色输出记录](References/generated_outputs_jammer_navy_white_yellow_v1.json)。CIWS、护盾v3、无人机v2保持，旧紫色图只保留历史。

当前参考为[审核A页面v3](Review_A_v3.html)中的无人机v2、干扰v3、护盾v3，均已A通过；旧[页面v2](Review_A_v2.html)、[页面v1](Review_A_v1.html)和所有原图保留。生产v1/v2为内部迭代，材质v3是B通过版本，已完成最终FBX与Blender交付。[审核前候选页面](Review_B_v3.html)和历史清单保留当时状态；当前批准及交付状态以B决定与最终清单为准。第二批B状态独立，开始第三批不代表第二批通过。

关联：[需求](../../../../Progress/RequirementDocument/20260917-Ship第三批支援组件贴图与框线制作.md)、[开发与审核](../../../../Progress/DevelopmentDocumentation/20260917-Ship第三批支援组件贴图与框线制作.md)。
