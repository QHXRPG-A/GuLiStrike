# 扫荡者橙区队色 B_v1

用户已取消新增参考图并指定原模型全部橙色为队色区。本版本只改色，不改网格。用户2026-10-09明确要求“导入 UE、接入自动改色。”，对应[正式导入放行记录](formal-import-authorization.json)。

- 原源：`D:\UE5.7\test1\ArtSource\CommanderLOD_20261005\SweeperSummon\SweeperSummon_3Tier.blend`；SHA256 `5acba9f1de505bafcb005645cdcacb5691f7ae5baef301e9cd547ce60a57fe11`。
- 成品：[Sweeper_OrangeTeamColor_B_v1.blend](Sweeper_OrangeTeamColor_B_v1.blend)；SHA256 `6ff9ea43b21702b6a820bafbca0e69e5e715ec5302c28128086af431ba938144`。
- 蓝方 `#6AA4BE`，红方 `#A34053`，全部原橙色随 TeamPrimary 变化；其他纹理、轮胎、炮管、白板、黄灯保持原版。
- [成品画廊](index.html)提供原版、蓝红与实际遮罩同机位四视图；这些是 Blender 成品渲染，不是生成参考图。
- [保存回读](blender-saved-readback.json)检查三档源几何、UV、法线、原色数据、权重、层级与持久变换；蓝红实例共享一个网格。
- [固定区检查](visual-qa.json)仅检查同机位实际成品渲染，避开纹理边界及抗锯齿外延；不视为UE运行测试。
- 遮罩使用原UV `UVmap_0`：新角点Alpha编码角色3，必须与[精确橙区UV遮罩](Masks/T_Sweeper_OrangeTeamMask.png)共同判断。简化LOD的跨色面不能只按顶点Alpha整面换色。原 `Attribute` RGB/Alpha及全部动画UV保持原样。
- 保留扫荡者既有无附加线稿/描边例外与原三档明暗参数。
- 克隆兵营占位2004和领地据点占位2007不制作。它们的模型ID和原资源保留，改色开关已关闭，八条未使用CPD绑定及两条待制作区域已从源表移除；历史归档不改写。

A：用户明确取消出图，并直接确认现有橙色分区。B：用户已明确放行此版本正式导入和接线；最终引擎显示反馈另记。没有重新制作或导入网格，正式材质在原稳定路径增加UV0精确遮罩与CPD8／16，原三档明暗、刚性WPO及固定底色不变。正式网格顶点Alpha保持原样，运行时使用精确UV遮罩，不依赖候选面Alpha。

[UE实际四视图、LOD与运行画廊](UE/index.html)记录25张实际静态图和4张实际召唤批次图。[正式入库](formal-ue-import.json)、[DataTable回读](datatable-saved-readback.json)、[双方客户端实际参数](UE/runtime-local-team-readback.json)、[未知身份与Tick恢复](UE/runtime-tick-recovery.json)、[地图保存回读](UE/acceptance-scene-readback.json)均已完成。原网格、UV、法线及原三档LOD完全一致；本次不涉及原生代码或编译，不新增自动化测试框架。
