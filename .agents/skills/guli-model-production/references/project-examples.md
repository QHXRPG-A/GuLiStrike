# 项目已认可实例

当前统一方向与新模型默认线稿/三渲二见[《GuLiStrike 美术规范》](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md)。下列早期建筑实例主要作为几何、装配和源文件经验；其旧PBR外观不能覆盖当前规范、兵种参考或用户专属要求。

## 风格基准

用户指定资产：`/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory`。

主体网格为 `.../Meshes/SM_RPF_Body`；原生源文件在 `D:/UE5.7/test1/ArtSource/Buildings/ResourceProcessingFactory/`；脚本为 `Scripts/Blender/build_resource_processing_factory.py`。

参考预览在 `outputs/resource-processing-factory-20260909/01_closed_hero.png` 和 `UE_01_Closed.png`。提取平整板面、直线折角、窄倒角、规则重复关系，不要求新资产复制该模型轮廓、迷彩和全部零件。

## 2026-09-14 验收的 V3 三模型

用户反馈 V1/V2 的纹理含义不明、随机碎块复杂，网格波浪、鼓包且软糊。仅整理贴图没有解决几何问题。V3 使用确定性截面和基础形体重建主要结构后，用户确认“符合预期”。

项目源文件：`D:/UE5.7/test1/ArtSource/Buildings/IndustrialDefenseSet/delivery_hardsurface/`。

| 模型 | 保留的主要特征 | V3 三角面 |
| --- | --- | ---: |
| RedOreRefinery 红矿精炼厂 | 八边形平台、双斜坡、红青罐体、立柱护栏 | 19,180 |
| ShieldGenerator 护盾生成器 | 三向柱体、浅色装甲、青色筋条、红色识别块 | 7,492 |
| HeavyDefenseCannon 重防炮 | 双炮轨、赭黄侧甲、圆形转台、四支脚 | 9,872 |

入口为 `GuLiStrike_ThreeModels_HardSurface.blend`，`Construction/*_EditableParts.blend` 保留分件。各模型目录内含合并文件、FBX/GLB、PBR 贴图和灰模对比。

实现位于 `ArtSource/Buildings/IndustrialDefenseSet/scripts/`：

- `hardsurface_common.py`：盒体、截面、环、圆柱、梁、倒角、UV 工具。
- `build_hardsurface_models.py`：三个具体形态的构建示例。
- `finish_hardsurface.py`：并件、图集烘焙、绑定、导出、几何检查。
- `finish_assets.py`：V3 复用 make_rig、pose、validate_rig、create_demo、export_asset。文件中的旧生成网格修补函数不是 V3 流程。
- `validate_exports.py`：FBX/GLB 回读。

UE 导入脚本：`Scripts/import_industrial_defense_models.py`，机械动画单位处理为 `Scripts/normalize_industrial_defense_animation.py`。引擎报告及画面证据：`outputs/hardsurface-models-20260914/`。

历史图纸与原始交付在 `D:/UE5.7/美术草图/Models_20260913/`。优先使用项目源文件，仅在历史对比时访问外部目录。
