# 指挥官三档 LOD 当前入口

版本 `CommanderLOD_3Tier_v1`，规范 v1.3。六种单位总共 LOD0 近景、LOD1 中景、LOD2 远景；已审近景保留。

用户已通过本具体版本 B，六组正式资源已切换并保存回读。源码版 Editor 编译通过、UE 已重开；PIE、联机、移动/建造运行验收和 FPS 未运行。彼之矛攻击逻辑未实现。

[成品及动作](Review/index.html) · [B决定](approval_B.json) · [正式交付和当前引用](formal_delivery.json) · [冻结审核哈希](review_manifest.json) · [预算差额](Reports/metrics.json)

正式入口：

- `/Game/GuLiStrike/Commander/Units/DefaultSoldier/LOD_3Tier_v1`
- `/Game/GuLiStrike/Commander/Units/WM01/LOD_3Tier_v1`
- `/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/LOD_3Tier_v1`
- `/Game/GuLiStrike/Commander/Units/ConstructionVehicle/LOD_3Tier_v1`
- `/Game/GuLiStrike/Commander/Units/SweeperSummon/LOD_3Tier_v1`
- `/Game/GuLiStrike/Commander/Units/BiZhiMao/LOD_3Tier_v1`

当前脚本入口 `Scripts/CommanderLOD`。正式回读使用 `verify_formal_group.py --arguments '{"unit":"<Soldiers.Name>"}'` 经 `ue_rpc.py` 调用编辑器。文档/网页更新使用 `build_review.py`，沿用已批准哈希，不重写审核证据。

本目录的 Blender、FBX、VAT 纹理和审核证据已冻结；后续几何或动画修订必须建立新版本后再审核。原始源包和历史机器回读保持，旧脚本不是当前制作入口。

审核Map `/Game/Maps/LVL_CommanderMassPrototype`：`CommanderLOD_` 为18组三档正式样机；`BiZhiMaoQA_` 为13个已保存玩法审核实体，样机使用真实无骨骼VAT。先前阶段记录保留，当前状态以正式交付清单为准。
