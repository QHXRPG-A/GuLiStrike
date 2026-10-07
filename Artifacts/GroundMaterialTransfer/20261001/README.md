# 指挥官关卡地表材质迁移（2026-10-01）

已将 `/Game/Maps/LVL_GroundMech_Demo` 的 `MI_Landscape` 应用到
`/Game/Maps/LVL_CommanderMassPrototype` 的 `Landscape_2300`（对象名 `Landscape_1`）。

目标专用实例为 `/Game/GuLiStrike/Environment/Materials/MI_CommanderGround_Pine`，
父级是 `/Game/StylizedPineEnvironment/Assets/Materials/Landscape/MI_Landscape`。
唯一参数覆盖是 `Spawn_Grass? = false`。复用源材质的六个 LayerInfo，保留原三层
注册与资产；全地形 `AutoLayer=1`、其余八层为零。

## 结果和证据

- [修改前基线](baseline.json)：257 个 Actor、变换、植被实例摘要、碰撞、边界、源文件元数据、初始脏包及视口。
- [绑定结果](bound.json)、[全范围导入结果](painted.json)、[最新实体读回](readback.json)。
- [材质包与地图重载结果](reload.json)、[重载后逐像素核对](pixel-verification.json)、[最终保存结果](saved.json)。
- [编辑器画面检查](visual-verification.json)：无默认网格、黑块或异常组件接缝。用户视觉终验未被代替。
- [俯视](previews/top.png)、[地面](previews/ground.png)、[指挥官 300 米观察镜头](previews/commander.png)。
- [视口恢复记录](capture-settings.json)：原位置、旋转、50° FOV、曝光、Game View 和显示模式已恢复。

`4081 × 4081` 个高度样本在修改及包重载后逐像素一致；9 层所有样本均已核对。
目标仍为 256 个地形组件，Actor、植被、碰撞和边界未改变。
源地图、源实例、源母材质及六个 LayerInfo 的大小/修改时间未变更，未读取 UE 二进制内容。
只保存目标地图与新实例；本次未启动 PIE 或构建原生代码。

## 备份与回退

`before/LVL_CommanderMassPrototype.live_before.umap` 是完整地图备份，包含任务开始时
该关卡已有的未保存改动。`before/LVL_CommanderMassPrototype.disk_before.umap` 是保存这些
改动之前的磁盘版本，两者不可混用。

完整回退步骤：先保存需要另行保留的后续改动并关闭编辑器，复制
`before/LVL_CommanderMassPrototype.live_before.umap` 到
`D:/UE5.7/test1/Content/Maps/LVL_CommanderMassPrototype.umap`，再打开目标地图。
这会恢复本任务开始时的材质（未绑定）、三层绘制分布和地图对象。
新建的 `MI_CommanderGround_Pine` 可保留；源资源没有被覆盖。

`before/height.png` 与 `before/weight-Base_Layer.png`、`before/weight-Layer_02.png`、
`before/weight-Layer_03.png` 是单独导出的原始高度和三层权重。
如需只恢复权重，使用项目 `GuLiLandscapeAuthoringLibrary` 一次导入完整层数组，
避免逐层导入导致权重重新归一化；完整地图恢复优先使用上述 live 备份。

## 实施脚本

[transfer_ground_material.py](../../../Scripts/Environment/transfer_ground_material.py)
通过 `Scripts/commander_editor_python.py` 运行，依次执行 `backup`、`bind`、`weights`、
`verify`、`save` 阶段。它拒绝覆盖既有备份，检查当前 Map 和 PIE 状态，并限制保存范围。
`weights-auto-interleaved.raw` 是本次完整九层导入数据（149,891,049 字节）。

UE 5.7 的 `SetMaterialInstanceStaticSwitchParameterValue` 在成功时仍返回 false；
脚本以有效参数回读确认关闭草簇，未修改引擎源码。
