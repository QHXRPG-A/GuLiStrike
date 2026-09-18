# Ship 三组件风格样板制作

**v4 三件均通过审核 B，最终 Blender、FBX、贴图和预览已交付。** 保留原模型几何、法线、骨骼和安装接口，仅修改色块、三渲二光影与框线；CIWS 连接处误分色已修正。

- [完整交付 ZIP](ShipComponentStyle_20260917_v4_Delivery.zip)
- [最终效果与交付总览](Delivery/v4/Review.html)
- [使用说明](Delivery/v4/README.md)
- [总览 Blender](Delivery/v4/Blender/ShipComponentStyle_Overview.blend)
- [最终文件清单与哈希](Delivery/v4/Delivery_Manifest.json)
- [FBX 回读结果](Delivery/v4/Validation/FBX_Readback_Summary.json)
- [审核 B 决定](approval_B_20260917.json)

交付含两套骨骼 FBX、一套静态 FBX、三个单件 Blender 和一个总览、九张 2K 贴图、材质参数及两门炮的连续俯仰演示。FBX 挂点按原数据嵌入自定义属性并提供 JSON；完整三渲二光影与外轮廓保留在 Blender，后续 UE 按参数重建。本轮 UE 正式接入与运行验证未运行。

## 来源与版本记录

规范 1.0；双联炮赭黄、CIWS 青蓝、Thor 砖红。Thor 保留原型 5 组 × 每组 3 孔，另有 5 个逻辑 Socket。用户先通过参考 A，随后明确要求原模型不改，因此原 A 只继续作为配色与光影依据。

`Source/FBX_static_v1/` 保存本次只读静态参考；`Source/ExistingAuthoring/` 保存两门炮已有骨骼源和历史验证。原型来源、安装尺寸、骨骼和挂点以 [源快照](Source/source_snapshot_v1.json) 与 [接口清单](InterfaceSpec_v1.md) 为准。未修改或保存 UE 源资产。

`Production/v1` 几何重建和 `v2` 新增装甲已停止采用；`v3` 保留连接处错误分色与诊断证据；`Production/v4` 是获批材质源，不再覆盖。`Delivery/v4` 是最终可迁移交付。旧骨骼源已有历史俯仰避让，本轮没有新增几何改造。

- [历史 A 参考总览](Review_A_v1.html) · [A 决定](approval_A_20260917.json)
- [审核前 v4 候选页面](Review_B_Materials_v4.html) · [候选文件哈希](review_B_material_manifest_v4.json)
- [范围调整和连接修正](material_scope_and_connection_fix_v4.json)
- [当前状态索引](review_manifest_v1.json) · [源文件哈希](source_file_manifest_v1.json)

历史候选页面和报告保留当时状态；后续 B 决定与最终交付清单记录完成状态。

## 全组件接入与临时文件清理

首批后续与第二、三、四批合并完成13件正式UE资源替换，14个蓝图沿用引用；当前结果见[全组件总览](UE_Integration/Review_Formal_v1.html)。上文UE未运行是首批模型交付时的阶段事实。

用户要求删除本会话临时资源。逐帧PNG在成品MP4完整解码后清理，Blender自动备份、闲置日志、缓存和一次性探测辅助文件按清单删除；原型、每轮审核版本、生产/交付模型、贴图、视频、验证JSON及UE回退保留。清单与保留文件核验保存于`UE_Integration/SessionCleanup_20260917/`。

临时视频编码器目录`CodexShipVideo`随本次清理移除。日后重新生成俯仰视频，需将FFmpeg加入PATH，或设置`GULI_FFMPEG_EXE`为其可执行文件路径；也可使用已安装的imageio-ffmpeg。现有成品视频播放不依赖该编码器。制作脚本已去除固定系统临时目录依赖。
