> 当前指挥官三档制作入口：[../../CommanderLOD_20261005/README.md](../../CommanderLOD_20261005/README.md)。以下是冻结来源与历史操作记录，旧脚本已撤出生产默认入口。

# 彼之矛 v1 顶点动画审核版本

使用已审 ControlRigMech B_v4，正式单位为静态网格＋逐顶点 VAT。派生 UE 目录为 `/Game/GuLiStrike/Commander/Units/BiZhiMao`。源骨架仅供 Blender 动作制作和烘焙，运行资源不包含骨架、骨骼动画组件、骨骼数据贴图或 ControlRig。

冻结源文件：`../ControlRigMechStyle_20261004/Production_B_v4/ControlRigMech_B_v4_Production.blend`，SHA256 `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`。

`BiZhiMao_v1.blend` 是可编辑四足步态制作文件；`vertex_metadata.json` 是当前正式顶点动画清单。`vat_metadata.json`、旧骨骼贴图、原动画 T/Q/S 和旧加权 VAT 脚本为改用逐顶点路线前的制作记录，已被当前清单取代，不应执行旧的骨骼资源导入脚本。

制作与安装脚本都在 `Scripts/BiZhiMao`：

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。
2. `readback_vertex_export.py`：回读 FBX 索引、分区和 LOD 标签。
3. `import_vertex_assets.py`：导入静态模型、位置/旋转贴图及三档明暗和稀疏线稿，清理本任务拥有的旧骨架派生资源。
4. `readback_vertex_assets.py`、`readback_texture_source.py`：回读 UE 原生网格数据及导出的贴图源。
5. `capture_art_preview.py`、`convert_native_captures.py`：真实 UE 材质变形的 SceneColor HDR 截取和标准 sRGB 编码；不启动游戏。
6. `deploy_runtime_assets.py`、`readback_runtime_assets.py`：仅在用户批准编译并加载新模块后安装 VAT DataAsset、三张源表和 UI 引用。旧模块下在任何写入前拒绝执行。
7. `prepare_acceptance_scene.py`：最后布置并保存 `/Game/Maps/LVL_CommanderMassPrototype`，再读取本次实体数据。

默认尺度2倍，移速144 cm/s，生命1000、防御0；工程车施工60蓝/30红/60工作量，占普通人口1。底座固定出生朝向，炮台30°/s，俯仰15°/s、范围−10°至+45°。首轮正式单位不索敌、不攻击，审核样机的目标跟随只用于动作展示。


成品版本仍待用户审核；制作预览、原生资产回读、编译、施工与移动运行验收分别记录。攻击逻辑须在本次实际成品通过后另行开发。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。
