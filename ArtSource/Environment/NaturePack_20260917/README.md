# GuLiStrike 自然资源包 · 设计 A v3

当前阶段：**参考A待审核**。按用户最新要求采用UE常规植被制作：**图集＋紧裁/弯折面片＋Masked双面＋实例化＋风摆LOD**。不再逐根草叶、逐片花瓣或每枚蕨小叶堆几何；岩石保持实体低模。

[打开v3审核页](Review_A_v3.html)：可旋转草、花、蕨和花毯工艺示意，支持遮罩/拓扑对照；下方包含完整34款外形参考与新预算。浏览器示意不是Blender或UE成品，SVG样板不是最终生产贴图。v2图仅用于剪影、色彩和尺寸参考，图中旧面数已被替代。

## 当前制作契约

- 34摆放款／33独立网格，蓝花毯与白花毯共用几何、UV、Alpha，花瓣区域遮罩控制颜色。
- LOD0上限：草簇6–24三角面，单花8，花穗12–20，蕨20–36，整块花毯16。预算不是最终网格实测，不代表性能通过。
- 十倍自然尺度烘入网格；UE导入和默认实例Scale1，根部／底部原点。
- 植被无线稿、岩石细轮廓；三档ArtLightDirection美术光保留。Masked+Two Sided不等于改用Two Sided Foliage受光模型。
- 风顶点Alpha根0尖1，与纹理裁剪Alpha分开；位移约高度2.5%、周期3–5秒、实例错相，强度0静止。
- 无植被碰撞或导航影响；岩石简单凸碰撞。每款植被1材质槽，Static Mesh Foliage实例化。
- 同时检查卡片空白面积、透明层叠、mip闪烁与GPU耗时，不只追求低三角数。

## 文件入口

- [当前34款尺寸、LOD、制作法和来源](asset_manifest.json)
- [v3工艺与逐款预算](Review/FoliageCards_v3.md)
- [可编辑透明图集技术样板](Design/Foliage_Atlas_Sample_v3.svg)
- [工艺示意源码](Design/foliage-cards-demo.js)
- [交付清单与哈希](Review/delivery_manifest_v3.json)
- [用户场景原图](References/User_Context.png)／[用户资源原图](References/User_AssetSheet.png)
- [项目风格锚点](../../Ships/ShipStylizedStudy_20260916/Previews/12_baked_lineart_hero.png)
- [项目美术规范](../../../Progress/RequirementDocument/GuLiStrike美术规范.md)

## 来源与历史

总览和7张分组视图使用内置image_gen制作，仅为二维设计。实际提示词：[总览v1](Review/Prompts_v1.md)、[总览v2](Review/Prompts_v2.md)、[分组视图](Review/Prompts_Orthographic_v2.md)、[花/岩石修正](Review/Prompts_Corrections_v2.md)。没有使用CLI图像API。v3透明SVG图集和构造示意是可编辑代码原生技术样板，不是AI生成的网格。

[v2审核页](Review_A_v2.html)、[v2规格快照](Review/asset_manifest_v2_superseded.json)与[v2预算](Review/LowGeometry_v2.md)只留作反馈历史。

## 后续交付

具体参考A通过后，以图集/面片路线制作N03/N18/N19/N21/N31/N33六件Blender样板，核验构造与形态后扩齐全套。真实Blender成品经B后才导出FBX、烘焙生产PNG/TGA、导入源码版UE5.7并创建独立展示关卡。最终输出还包括固定200/800/1800米镜头截图、风摆视频、尺寸/LOD读回与样板性能记录。正式地图铺设另行安排。

当前没有自然包Blender、FBX或UE资源；没有风摆、碰撞、LOD或GPU性能通过记录。不会把实施授权写成未展示产物的A/B通过。
