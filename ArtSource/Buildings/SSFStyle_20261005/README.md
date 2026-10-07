# SSF 建筑制作源目录

当前 **SSF_TeamPalette_B_v2** 已完成正式入库，并获用户明确“审核通过”。蓝方、红方各六座建筑，48个新增正式资源，三档LOD、线稿与三档明暗保持；原B_v1和商城包保留，暂不接入游戏。

- [当前最终审核通过记录](Acceptance_B_v2_20261007/README.md)
- [本会话临时文件清理记录](SessionCleanup_20261007/README.md)
- [B_v1已审源文件保持说明](Production_B_v1/FrozenSource/SOURCE_VERSION_NOTE.md)
- [当前正式资源交付与路径](UE_Delivery_Team_v2/README.md)
- [当前实际UE总览](UE_Delivery_Team_v2/Sheets/Blue_Red_UE_Overview.png)
- [正式机器清单](UE_Delivery_Team_v2/formal_delivery.json)
- [B_v2具体存储放行](approval_B_v2_import_20261007.json)
- [原提交Blender文件](TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend)
- [此前B_v1正式交付及共用依赖](UE_Delivery_v1/README.md)

蓝方目录 `/Game/GuLiStrike/Buildings/SSFStylized/Blue`，红方目录 `/Game/GuLiStrike/Buildings/SSFStylized/Red`。复用原正式6骨架、6物理资产和21建筑动画，原无人机及第22个动作不改。新资源已独立重载与实际预览，原预算差额继续记录；原提交文件与当时待审记录为冻结历史，最新状态以本入口和决定登记为准。

## 历史参考阶段入口

以下按发生时的阶段状态保留，当前候选状态以上方B_v2及决定登记为准。

当前审核版本为 **SSF_Reference_A_v7**，A待审核、B未开始。保持A_v6全部配色和原结构，响应“线稿和三档明暗好像没加？”修正线稿与色阶可见性。A_v6已有两者，但军工厂该机位暗部仅0.70%、总览线条偏细；A_v7调整阈值和线条粗细，全部40项HEX、90材质基础RGB与三档RGB、光向、法线、机位均保持。

- [完整参考图与三视图](Review_A_v7.md)
- [本地图板页](review_A_v7.html)
- [同机位风格前后对照](References_A_v7/Style_Comparison_A_v6_to_A_v7.png)
- [A_v6线稿与三档拆分核对](References_A_v7/Style_Breakdown_A_v6.png)
- [版本及SHA256](References_A_v7/reference_manifest.json)
- [技术检查](References_A_v7/validation.json)
- [助手视觉检查](References_A_v7/visual_qa.json)
- [本轮线条与色阶设置](References_A_v7/style_revision.json)
- [实际三档可见分区](References_A_v7/style_visibility_validation.json)
- [配色、结构及机位保持](References_A_v7/style_preservation_validation.json)
- [历史版本保存与本轮修改对照](References_A_v7/revision_validation.json)
- [全部色值沿用A_v6](References_A_v7/palette_revision.json)
- [保存后实际Blender颜色与色阶回读](References_A_v7/palette_preservation_validation.json)
- [当前交付与SHA256核对](DeliveryValidation_A_v7.json)
- [A_v6附图选择A_v3及两座提亮依据](References_A_v6/baseline_selection.json)
- [源模型、骨架、动画和导出文件](Source/source_manifest.json)
- [制作开发记录](../../../Progress/DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md)

42张2K单图覆盖六座建筑与四件配套，十套四视图图板、两张4K组合图。参考blend打包所需10张纹理，几何仍为原模型，仅用于审核参考设计。源FBX、源动画与骨架基线保留。

历史A_v1至A_v6原样保留；此前A_v6仅提亮军工厂和战略中心四项深色，无人机不跟随，本版配色全部沿用。当前统一审核A_v7，参考内线/轮廓仍是遮罩和Freestyle，生产图集与真实描边壳在A通过后制作。方案实施、基准选择和风格问题反馈不等于A/B通过；取得具体版本A决定后，开始真实成品重制与减面，B通过后才建立正式UE副本。
