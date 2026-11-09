# Ground旧模型清理

用户明确要求保留本轮卡图造型、删除旧机甲模型避免后续误路由。当前正式模型及两个角色蓝图引用保持A4B2。

- 删除31个已审计旧资产：9个旧网格及其旧材质、纹理、蓝图等关联资源；删除旧展示图中的MSS_Machinegun_lvl1。
- Excel Models移除4002–4004；Parts移除3行，MaterialParameters移除9行；正式数据表与AcceptanceModels同步。
- published-model-ids保留已发布编号历史，retired-model-ids显式锁定退役编号，导出器拒绝重新启用或复用。
- AGENTS与卡牌美术流程增加当前模型路由规则，历史截图和本轮插画不得用来推断运行时模型。
- 正式及审核角色回读为SK_GroundMech_A4B2与ABP_GroundMech_A4B2；Ground目录只剩这一份网格。36表静态校验及200/600 Manifest校验通过。
- 两处既有后坐夹具移除旧网格加载路径，使用当前模型与数据化参考位移；未新增测试用例，未构建或执行测试。

[执行记录](cleanup_result.json)、[最终回读](final_readback.json)、[Excel修改](excel_retirement.json)、[删除前引用](preflight.json)。

`Retired_GroundModels_SourceRestoreOnly.zip`是Content外的冷恢复备份，包含删除前的旧包及本次修改的旧表／展示图，不是可用模型目录或新美术参考。恢复须根据用户明确的恢复要求执行，日常路由不得扫描或导入该包。

本次仅资源维护与静态核对，未构建、PIE或读图测试。原生夹具源码变动尚未编译。
