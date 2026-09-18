# Ship 三组件材质样板 v4 — 最终交付

2026-09-17，三件均获用户审核 B 通过：“三件均通过 B，导出 FBX”。规范版本 1.0。原模型的顶点、拓扑、法线、尺寸、机械绑定及安装原点保留；本轮修改渲染、贴图、结构内线和可关闭外轮廓。

打开 [交付总览](Review.html) 查看实际渲染、三视图、灰模、光向对照和俯仰演示。

## 文件

| 组件 | LOD0 主体三角面 | FBX | 可编辑源 |
|---|---:|---|---|
| 双联炮 | 1680 | [骨骼 FBX](FBX/SKM_SC_Twin_Barrel_Turret_Styled.fbx) | [Blender](Blender/SC_Twin_Barrel_Turret_Styled.blend) |
| CIWS | 1066 | [骨骼 FBX](FBX/SKM_SC_CIWS_Styled.fbx) | [Blender](Blender/SC_CIWS_Styled.blend) |
| Thor 一级导弹舱 | 2080 | [静态 FBX](FBX/SM_SC_Thor_MissilePod_Styled.fbx) | [Blender](Blender/SC_Thor_MissilePod_Styled.blend) |

[总览 Blender](Blender/ShipComponentStyle_Overview.blend) 含三个独立 Scene；在 Scene 选择器切换，保留各自原尺寸和原点。两门炮使用已冻结的既有骨骼源，含之前完成的俯仰避让；本轮没有新增几何改造。以上面数对应该既有骨骼源，不能与最早静态参考面数混用。每份 FBX 只有一个主体网格、一个材质槽。

## Blender 与贴图

使用 Blender 5.2.2 LTS、EEVEE 实际渲染。材质依场景光照与遮挡形成三档明暗，BaseColor 没有烘入光影。所有贴图已打包到 Blender，同时在 Textures 目录提供独立 PNG；保持交付目录结构即可迁移。

- 每件各有 2048×2048 BaseColor（sRGB）、ORM（Non-Color）、LineMask（Non-Color），共 9 张。
- ORM：R=1（未烘焙 AO），G=粗糙度，B=金属度。LineMask 白色为内线；材质节点 `Line_Strength` 默认 0.85，设为 0 可关闭。
- 外轮廓在 `OUTLINE_TOGGLE` 集合，关闭渲染或视口可隐藏。它是单独的描边壳，三角面数量与对应主体相同，不改变主体网格。
- Blender 保留原 UV 顺序并新增 `SC_PaintUV`、`SC_LineUV`；材质按名称引用。FBX 统一以 UV0 为绘制 UV、UV1 为内线 UV，后续通道保留全部原 UV。
- 两门炮 Blender 第 0 帧为静止展示；第 1–97 帧为连续俯仰，24 FPS。Previews 内含 MP4 和 −15°、0°、30°、75° 彩色/灰模图。FBX 输出静止绑定，不烘入演示动画。

## 骨骼、挂点与材质移交

两门炮保持 `Root → BarrelPitch`，刚性零件单骨骼权重 1。双联炮保留 2 个炮口、CIWS 保留 3 个；Thor 保留 5 组 × 3 孔及 5 个逻辑 Socket。

挂点名称、所属骨骼、原 UE 厘米坐标、旋转与缩放保存在每件 `Parameters/*_Sockets.json`，并原样嵌入 FBX 主体自定义属性 `GuLiStrike_Sockets_JSON`。Blender 内保留可编辑挂点标记。FBX 没有额外的 Socket 骨骼；后续 UE 接入时按该接口数据恢复引擎 Socket。[完整接口](Parameters/InterfaceSpec_v1.md)和[只读源快照](Parameters/source_snapshot_v1.json)包含轴心及坐标系约定。

FBX 提供 BaseColor 的基础材质预览；完整三渲二光照阴影、内线和外轮廓以 Blender 实际渲染为准，需按 [材质参数](Parameters/MaterialParameters_v4.json) 在后续 UE 材质中重建。本轮 UE 正式导入和运行验证未运行。

## 核对与审核证据

[FBX 回读](Validation/FBX_Readback_Summary.json)复用项目已有读取工具，三件尺寸、原点、UV、三角面、材质槽、骨骼层级、轴心、刚性权重及挂点元数据通过。最大回读几何误差约 0.0000069 m，原点误差为 0，UV 数值误差为 0。[可迁移 Blender 回读](Validation/Portable_Blender_Readback.json)验证原几何哈希、内嵌和外部贴图、三渲二节点及审核记录路径。

[审核 B 决定](Parameters/approval_B_20260917.json)冻结获批源与效果图哈希；[最终文件清单](Delivery_Manifest.json)记录最终交付 SHA-256。最终 Blender 只作交付元数据、相对资源路径和未使用 ORM 纹理的保留调整，获批节点与几何未改变，制作根下获批源不覆盖。Validation 中 `ApprovedSource` 报告及候选清单保留审核前时点，最终状态以本交付清单和 B 决定为准。

References 保留原型视图、已通过 A 的配色参考、风格锚点及 CIWS 连接处分色修正依据。A 图只作配色与光影参考；原模型是几何依据。
