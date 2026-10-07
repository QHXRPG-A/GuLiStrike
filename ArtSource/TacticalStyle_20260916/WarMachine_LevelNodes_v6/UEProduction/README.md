# 重防号 v6 正式 UE 资产

2026-09-29 交付。用户指令“重防号模型导出至UE成为正式资产”放行当前 v6 成品进入正式目录。保持已确认的等长30°下倾主支架、倾斜前叉耳、水平节点方块和四盘水平同高。没有重新建模；从已审 Review.blend 导出。

## 正式路径与操作

- 战斗静态网格：`/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Cel`
- 同步骨骼版本：`/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SK_WarMachine_Cel`
- 材质与贴图：同根目录下的 `Materials`、`Textures`。沿用现有两份三渲二/描边材质，更新对应 BaseColor 和 LineMask。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。

`DT_GuLiStrikeCommander_Soldiers` 的 `WM01` 行继续引用同一静态网格，因此使用该行的重防号会取得新资源。`PresentationScale=0.2`、移动速度、血量和12.5米名义占地配置均未改变。当前关卡仍为 `/Game/Maps/LVL_CommanderMassPrototype`；未启动游玩。用户后续可在既有重防号入口核验战斗中的模型、炮口和导弹发射位置。

## 文件

- [可编辑279分件源](../WarMachine_LevelNodes_Editable.blend)、[原审核版](../WarMachine_LevelNodes_Review.blend)：哈希保持原样。
- [生产绑定版](WarMachine_Production_Rigged.blend)：仅完整机体、轮廓壳与既有刚性骨架；不包含局部重复件或相机。
- [静态FBX](SM_WarMachine_Cel.fbx)、[骨骼FBX](SK_WarMachine_Cel.fbx)。
- [BaseColor](T_WarMachine_BaseColor.png)、[LineMask](T_WarMachine_LineMask.png)。
- [UE正式资源预览](Previews/UE_ProductionHero.png)：1536×1536，实际正式网格、既有材质和0.2展示缩放，固定曝光的SceneCapture；不是游戏运行截图。助手未读取图像内容，由用户核验。

## 技术回读

| 项目 | 结果 |
|---|---|
| FBX回读 | 两份均为完整机体+轮廓壳；最大边界误差0.00000191米；原源文件未改 |
| UE边界尺寸 | 5207.016 × 5790.170 × 4203.237 cm；与FBX一致，沿用原生产单位，不额外缩放 |
| WM01按0.2展示 | 1041.403 × 1158.034 × 840.647 cm；尺寸变化来自已审几何姿态 |
| 静态LOD三角面 | 38,843 / 9,711 / 299 / 69；含描边；切换阈值仍为1 / 0.32 / 0.09 / 0.025 |
| UV/绑定 | 保留3套UV；Blender root→Body刚性权重1；UE保留Rig_WarMachine→root→Body及原Skeleton路径 |
| 材质 | 原材质网络和槽顺序保持；BaseColor sRGB，LineMask非sRGB/Masks；轮廓各LOD不投影、不参与碰撞 |
| 挂点 | 原4个名称保留；随机身上移754.952源cm；导弹俯仰45°保留 |
| 碰撞 | 原简单碰撞0、凸包0与CollisionTraceFlag保持；不新增物理资产 |
| 引用 | 原7个静态网格引用包保持；WM01完整数据行未改；正式资产均已保存 |

LOD0比旧网格增加了11,540三角面，来自已确认的连接结构和轮廓；本轮没有性能实测。文件导出、导入保存与技术回读通过；用户对UE外观及实战效果的确认独立记录。按用户要求未读图，未运行PIE、自动化测试、原生编译或卡图生成。

## 记录与回退

[导出与源哈希](export-manifest.json) · [导入](import-manifest.json) · [最终独立回读](final-readback.json) · [UE预览参数](Previews/capture-manifest.json) · [导入前配置](before-ue.json) · [旧文件备份清单](backup-before.json)。

`BackupBefore/Content/Commander/Units/Tactical/Cel/WarMachine` 保留导入前7个完整包，包含两网格、骨架、两贴图和两材质；仅文件复制，没有解析二进制内容。需要回退时先协调保存并关闭加载这些包的编辑器，再按清单将整组备份恢复到原路径，重新打开项目。不要在编辑器仍持有资源时从文件系统覆盖。

可复现脚本在 `SourceSnapshot`，当前入口为项目 `Scripts/Blender/export_warmachine_production_v6.py`、`Scripts/import_warmachine_production_v6.py` 和 `Scripts/verify_warmachine_production_v6.py`。导入使用当前编辑器、显式旧FBX工厂；临时关闭的Interchange开关已恢复。仅保存本轮五个变更包，旧材质网络没有重建，未保存玩法地图。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。
