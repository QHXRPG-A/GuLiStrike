# 松树林风格适配 v1

2026-09-18，现有供应商资源的**隔离材质 / 散布候选**，待用户视觉审核。不是整包完成，不是新模型 B 审核通过，不替换正式关卡。

[同机位滑杆对比](Review_v1.html) · [总览](Previews/03_overview.png) · [树冠近景](Previews/10_tree_detail.png) · [草与扫荡者](Previews/11_grass_detail.png)

![实际UE候选](Previews/03_overview.png)

## UE 打开路径

`/Game/GuLiStrike/Environment/ArtReview/PineStyleAdaptation_20260918/LVL_Pine_Style_v1`

原对照关卡保留：`/Game/GuLiStrike/Environment/ArtReview/PineStyleComparison_20260917/LVL_Pine_Units_Comparison`。

供应商资源来源：`D:/BaiduNetdiskDownload/塞尔达松树林/StylizedPineEnvironment/StylizedPineEnvironment`。项目中的原包路径 `/Game/StylizedPineEnvironment/Assets` 只读使用；所有修改均在上述 ArtReview 新目录。Ship、扫荡者、战争机器、岩石及灯光仍使用原对照场景版本。

## 已制作

| 对象 | 本轮变化 | 保留的内容 |
|---|---|---|
| 三种活松树 / 场景五棵 | Leaf / Bark 三档材质，冷绿暗部和枝簇根部色阶；树皮去除颜色贴图细纹与法线采样 | 原轮廓、拓扑、UV、层叠树冠；三网格分别1770 / 2138 / 2472三角面 |
| 松针 | 原图集 mip bias +2、覆盖阈值0.28，减弱细碎孔隙；颜色不再乘原画入的叶片明暗 | 两张原图集只读复用，未重绘贴图 / 改造叶片卡片 |
| 草 | 三档绿色，去掉偏黄叶尖；草簇XY比例12–15、Z比例6–8；1247簇实例 | 单簇37三角面、原拓扑；不是新增宽叶模型 |
| 草动态 | 材质按原模型局部高度计算根到尖权重；默认周期4秒、峰值位移6cm、世界位置错相位；WindStrength=0完全无位移 | 无需宣称已存在顶点Alpha；没有新写顶点颜色 |
| 草坪覆盖 | 逐叶投影关闭；独立展示地面局部草色承接，单位周围保留空地 | 树、岩石及单位投影保留；原地面在草坪区域之外不改色 |
| 渐隐 | 新FoliageType从220000cm开始，到260000cm结束 | 原FoliageType不改动；不把原远景变淡全部归因于模型 |

三个母材质、五个实例、四个网格副本、一个FoliageType、一个草底色地面材质及独立关卡。没有新增纹理资产、FBX或Blender源工程；本轮不是模型重新制作。

草场景LOD0理论三角面从84,841降至46,139（约45.6%）；这是实例数量核算，不是GPU加速比或实际每帧绘制面数。

## 着色与制作边界

- 沿用Ship的Unlit + `ArtLightDirection(.35,-.55,.76)`美术光向三档路线；颜色、阈值、根部明暗、风强度可调。不会自动接收场景动态阴影或随太阳自动变色；太阳与材质方向只在本展示关卡对齐。
- 草坪底色是评审地面材质中的局部色区，非草网格的一部分；正式地图应在地形 / 地表材质中另行适配，不能仅复制FoliageType就获得相同地面覆盖。
- 本隔离候选不新增植被线稿，保持供应商原件无附加轮廓的状态。没有把NaturePack的专属例外扩展为全项目规则，美术规范仍为v1.1。
- 原供应商这四款网格仍只有LOD0；尚未新增LOD、重绘图集、改草叶拓扑、扩充枯树 / 断树 / 树桩、录制风摆或测GPU / DrawCall。不得写成全包终验完成。
- 新设计 / 新网格仍按[地编技能](../../../../.agents/skills/ue5-scene-building/SKILL.md)和[项目美术规范](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md)执行A/B；本轮直接在UE进行已授权的隔离材质与场景候选制作，不改正式美术引用。

## 检查与证据

- [制作范围及初始脏包](scope.json)、[材质清单](materials.json)、[场景改动](scene_changes.json)。
- [九机位截图](gallery.json)：总览、低机位、俯视、指挥官200 / 800 / 1800m、单位近景、树近景和草近景。指挥官俯角55°、FOV45°；编辑器CameraActor截图，不冒充PIE实战画面。
- [读回](readback.json)、[保存内容核对](saved_readback.json)：最终使用UE直接读取两个明确命名的World资产，不再切换活动关卡；草1247/原版2293簇，单位与岩石引用及变换一致，四个副本网格三角面与源一致，检查前后脏包均为空。
- 首轮三套植被母材质编译诊断通过。最终渲染后，当前编辑器上下文的材质诊断服务返回不可用；最终读取了已保存的三档/WPO代码与ShadingModel，但不把不可用误写成新的编译通过。完整结果见上述JSON；远景闪烁与抗锯齿一致性未做连续验证。
- [制作脚本](../../../../Scripts/adapt_pine_style_review.py)，实际顺序为 `setup → materials → apply → refine → refine_grass → gallery → read_saved_packages`；脚本保护原资源和非目标关卡，禁止重复覆盖其他资产。`reload_saved` 为需要活动评审世界的可选路径，本轮最后未完成该切换式检查。
- 第一轮将树冠简化得过平，已追加枝簇根部色阶；保留[首轮总览](Iteration01/03_overview.png)作为内部迭代证据，不作为当前候选。

本轮检查发现编辑器在截图后被重启，旧评审进程不再存在。一次后续写入被关卡保护拒绝，未改动指挥官原型关卡。读取新进程确认源码引擎、PIE停止、脏包为空后，才重开本候选继续工作。

## 下一步

用户评审树冠色块和草坪方向；根据反馈决定是否重绘松针图集 / 修改草叶。方向通过后再适配其余七种树变体并补LOD与动态 / 性能验证。当前“开始修改 / 继续”是实施授权，不是对尚未展示成品的视觉通过记录。

[开发记录](../../../../Progress/DevelopmentDocumentation/20260917-松树林与三单位同场景对照.md)
