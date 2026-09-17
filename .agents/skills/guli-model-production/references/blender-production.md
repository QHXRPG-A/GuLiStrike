# Blender 实施细节

实际建模、绑定或导出时读取。完整实例位于 `ArtSource/Buildings/IndustrialDefenseSet/scripts/`。这些是特定三模型的脚本，给新资产使用前应抽取或调整参数与轮廓，不要直接覆盖已验收实例。

## 几何和法线

- 通过尺寸、截面和矩阵构建平面零件。对称结构使用共同参数镜像/旋转，重复结构共用间距，不逐件凭视觉摆出不一致零件。
- “规则多边形”限制的是随意装饰碎片和歪曲结构。装甲可使用规则切角多边形，罐体和轴承保持同轴圆形，网格拓扑本身无需只有矩形和三角形。
- 大平面使用清晰的硬边/分裂法线，可用窄的 1 段倒角和 Weighted Normal 保持面感。按零件厚度调整倒角，圆柱分段数按轮廓需求选择。
- 倒角不要达到薄片半厚。V3 护盾厚 0.023 m 的板件曾使用 0.013 m 倒角，Clamp Overlap 压合边缘产生退化面；改为 0.006 m 后通过。修正参数，不忽略退化面或用整体平滑掩盖。
- 分件阶段保存命名零件、刚体归属及修改器；导出副本应用倒角/法线修改器，再按静态资产或运动组并件。封闭零件合理搭接无需一概布尔并集。

## UV 与材质

先用原生纯色材质槽制作。需要便携单材质资产时，将颜色、粗糙度、金属度和灯光标记烘焙到统一 atlas，不把场景灯光与阴影烘入 BaseColor。

V3 使用多物体 Smart UV Project，炮塔三个刚性网格共享不重叠 atlas。4K、约 0.003 的 island margin 和 6 px 烘焙扩展是本实例参数；新资产按密度、岛数和显示距离调整，检查岛间污染与边缘漏色。

在临时场景中复制网格及材质，将各通道接到 Emission 做无光照烘焙。旧 V3 实例使用 Principled PBR；当前新模型按[美术规范](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md)接入已审三渲二与线稿，不能把旧PBR节点当成新的最终风格。导出需保留基础颜色/遮罩等可移植数据，UE端重建等效分档结果。

| 通道 | 色彩空间 | 内容 |
| --- | --- | --- |
| BaseColor | sRGB | 纯色及规则标记 |
| Emissive | sRGB | 仅实际发光部分，青色涂装不自动变灯 |
| ORM | Non-Color | R=AO、G=Roughness、B=Metallic；没有 AO 时 R=1 |
| NormalGL / NormalDX | Non-Color | 与目标约定一致的切线法线 |

细节已经建在几何上时可省略凹凸纹理；交付约定需要法线文件时提供中性法线并说明。新模型不强制 4K；独立 Roughness/Metallic 仅在接收端需要时拆出。

## 绑定与导出

V3 炮塔实例：米制、Z 向上、炮口朝 +X；base_yaw 局部 Z 旋转，barrel_pitch 局部 Y 负值表示抬炮，限位为下压 10° 至抬高 40°。轴约定随资产文档交付，UE 中重新确认转换，勿直接照抄 Blender 欧拉角。

验证刚性权重，并比较蒙皮顶点与骨骼刚体变换结果。手动姿态测试前解除演示 action，避免关键帧覆盖设置，完成后恢复中立帧和动作。

FBX 实例采用 `use_selection=True`、`add_leaf_bones=False`、`use_armature_deform_only=True`，导入端保留法线与切线。网格和动画分别导出；控制圈、摄像机和灯光不进入网格文件。

GLB 同时设置 `use_selection=True` 和 `use_active_scene=True`，防止总览场景的重复模型进入导出。标准 PBR 节点和内嵌图片比复杂程序材质容易跨工具还原。

回读 FBX/GLB 检查尺寸、网格数、UV、材质、骨骼层级和权重。动画至少检查固定基础静止、活动件运动及合理时长。glTF 回读设置 `disable_bone_shape=True`，排除骨骼辅助网格对统计的干扰。

## 本机注意事项

- 当前 Blender：`D:/steam/steamapps/common/Blender/blender.exe`，环境变化时先检查路径。
- 批量构建、烘焙、导出可用后台 Blender；交互实例用于查看和调节。切换已打开文件前检查路径和脏状态，必要时保存当前文件副本。
- 后台 Python 异常仍可能返回进程退出码 0，同时检查日志和成功结果。
- 默认节点名会本地化，按 `node.type` 查 Principled/Output 等节点。
- 先设目标 scene，再选择对象和执行 bpy.ops。批量 join 前建立完整分组列表，避免访问被前一次 join 删除的对象。
- 本会话 CLI MCP 后台执行曾不可用，直接调用 Blender 可执行文件完成了生产；工具恢复后仍可使用 MCP。
