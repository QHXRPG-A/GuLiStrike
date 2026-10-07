# SSF_Production_B_v1

实际 Blender 5.2.2 LTS 成品，提交审核B；24项本体预算例外待决定。

[完整审核](../Review_B_v1.md) · [图板与全部视频](../review_B_v1.html) · [预算差额](Budget_Exceptions.md)。

打开 `SSF_Production_B_v1.blend`：初始场景 `Production_Assembly` 为原尺寸组合。每资产有 `Production_<Key>` 场景；`<Key>_EDITABLE_MECHANICAL_PARTS` 保留分件/镜像/减面修改器，默认隐藏供编辑；`<Key>_LOD0/1/2_BAKED` 是三档渲染/后续导出副本。使用 `Rig_<Key>` 的22个同名Action查看原动作，30fps时间轴；未经审批不得直接替换正式UE资产。

半透明Logo和灯片使用源UV，内部线稿使用 `SSF_AtlasUV` 的独立RG遮罩；`SSF_PaletteLinear` 控制已审色块，保留Base Color和Team Color节点参数。原Root与骨骼层级恢复；金属刚性，克隆中心软管保留源柔性。

所有预览来自实际成品；无Freestyle代替真实壳。文件打包贴图，外部38张新纹理另交付。`Renders/`为原始2K/4K像素；`Sheets/`和`QA/`仅布局这些真实图；`AnimationPreviews/`为全部原动作三档同播视频及回读证据。

原单位/轴心/骨骼运动在Blender检查通过；FBX导出回读、UE压缩/引用/物理资产/无人机蓝图/自动LOD及项目镜头验收尚未执行，实战帧率未测量。

`production_manifest.json` 发布后冻结本版；后续修改创建新版本，工作中间文件与Logs不属于清单交付。
