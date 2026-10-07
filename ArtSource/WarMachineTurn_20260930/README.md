# WM01 重防号航母式转向

本目录保留本轮资源变更前备份、源资源回读以及已完成的UE资产/场景证据。原编辑源继续使用 `../MechanicalAnimation_20260929/WarMachine/WarMachine_RigidEditable.blend`；不改历史Cel原件或稳定生产资源路径。

- `before_*`：本轮首次修改前的blend、FBX、manifest；重跑不覆盖。
- `rig-source.json`：源网格的中立几何/UV0/材质分配签名、四组真实轴承/销轴坐标、分区与独立FBX回读。
- `ue-import.json`、`ue-materials.json`：导入与材质升级已执行并保存，均成功。
- `ue-asset-readback.json`：场景制作前的资源检查；最终资源状态以 `scene-readback.json` 为准。
指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。
- `scene-delivery.json`、`scene-readback.json`：原型地图保存成功，13组新预览、相机、说明及两组旧预览回读通过；最终材质渲染边界2000cm，网格玩法尺寸不变。
- `static-review.json`：源码人工静态核对、9个Python脚本AST与目标差异格式检查；没有原生编译或运行测试。
- `material-statistics.json`：实际调用材质的着色器统计；函数缩略图像素阶段警告与验证边界见开发文档。
- `build-editor-result.json`、`build-editor.log`：用户后续要求编译，源码版Editor目标成功，UBT耗时83.39秒。
- `build-id-check.json`、`build-source-before.json`：8份BuildId一致，14份功能原生/配置输入哈希构建前后一致。
- `build-editor-load.json`、`build-scene-readback.json`：可见编辑器已加载新DLL，重开后的13实例场景回读通过，说明已更新保存。
- 原生代码已获准编译加载；用户在幅度减半为6°/4°/7.5°后明确确认“验收通过”。专项联机覆盖与性能数据未单独提供。早期静态及场景记录中的未编译状态保留其当时事实，当前结论见[玩家验收归档](../../Progress/Archive/20260930-重防号航母式转向玩家验收通过.md)。

生产步骤（按依赖顺序）：

1. Blender后台运行 `Scripts/Blender/author_warmachine_turn_rig.py -- --apply`，保留原中立几何并更新机械分区。
2. 活跃UE中执行 `Scripts/import_mass_rigid_animation.py`，设置 `GULI_RIGID_UNITS=['WarMachine']`。
3. 活跃UE中执行 `Scripts/build_mass_rigid_materials.py`，同步共享函数调用者与覆盖材质。
4. 执行 `Scripts/inspect_warmachine_turn.py`，`GULI_TURN_INSPECT_SCENE=False`，导出真实渲染LOD；Blender执行 `Scripts/Blender/inspect_warmachine_turn_lods.py` 回读。
5. 静态检查完成后执行 `Scripts/author_warmachine_hover_scene.py`，保存指定原型图；再次执行 `Scripts/inspect_warmachine_turn.py` 回读场景实体。

已有旧预览脚本同步51-float布局，但本轮不执行截图、PIE或自动化验收。详细实现和玩家操作见[开发文档](../../Progress/DevelopmentDocumentation/20260930-重防号航母式转向表现.md)。


> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。
