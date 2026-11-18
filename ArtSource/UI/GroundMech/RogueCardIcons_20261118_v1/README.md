# 游骑兵五张肉鸽牌独立图标

当前为2026-11-18 **V2.1（爆破套餐三枚导弹轴线修订）**。用户指出三枚导弹均歪斜后，以完整V2透明PNG和红箭头反馈图为输入追加内置生图编辑。要求三枚导弹分别以各自筒身方向为直轴，弹头尖端、圆环中心与尾部中心共线，保留编队、大小关系、紫色配色及弯曲尾迹。仅替换Icon11源图与正式纹理；其他四图（含之前修订的强化爆炸弹）保持。图像结果是否达成由用户核验。

前次V2响应用户“设计更好看并检索全网/GitHub提示词”的要求，核对公开仓库和官方案例，将构图主次、机械透视、外内线粗细、高光位置及色块面积写成新版提示词，再分别生成五张图。强化爆炸弹随后追加单枚导弹同轴编辑。本次沿用这套风格，只处理爆破套餐的三枚导弹。

[五图预览](index.html)支持V2.1/V1切换、浅色/深色/蓝色底与64/128/256显示尺寸；爆破套餐和强化爆炸弹各有“查看轴线修订前”按钮。PNG保留原始1254×1254 RGBA像素和真实透明通道，未用脚本重绘、抠图、重配色或缩放；UE纹理构建尺寸为256×256。V1源图、页面和清单保留在`Revisions/v1/`；本次修改前的页面、清单和包另存`Revisions/v2_before_package_axis/`，Icon11旧图保留在`Inputs/11_ExplosivePackage_v2_before_axis.png`。当前正式资源读取`Icons/`。

| 图标 | IconId | 源图 | 正式纹理名 | 当前引用 |
|---|---:|---|---|---|
| 无人生还 | 9 | [PNG](Icons/09_NoSurvivors.png) | T_GroundIcon_NoSurvivors | 独立资源；Artwork13尚无玩法卡行 |
| 强化子弹 | 10 | [PNG](Icons/10_EnhancedBullet.png) | T_GroundIcon_EnhancedBullet | Card10.01 |
| 爆破套餐 | 11 | [PNG](Icons/11_ExplosivePackage.png) | T_GroundIcon_ExplosivePackage | Card11.01 |
| 增加攻速 | 12 | [PNG](Icons/12_FireRate.png) | T_GroundIcon_FireRate | Card09.01 |
| 强化爆炸弹 | 13 | [PNG](Icons/13_ExplosiveEnhancement.png) | T_GroundIcon_ExplosiveEnhancement | Card12.01；正式Artwork14 |

正式目录为 `/Game/GuLiStrike/GroundMech/UI/Icons/`。数据由 `data/Excel/GuLiStrikeIcons.xlsx/Icons`、`data/Excel/GuLiStrikeRogueCards.xlsx/Cards` 经正常导出接入，未修改机制、数值或前置。旧2/3/4图标保持；没有复用已退役ID7。

## 配色与生成追溯

配色依据[项目统一色库](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md)。本批所选基色记录在[V2制作计划](generation-plan-v2.json)与[最终清单](manifest.json)：增加攻速选蓝色组，强化子弹选黑红组，其余三张选紫色组。新版把基色、暗面、亮面和点缀面积分开约束，由内置生图执行；没有将像素色差或视觉效果记为助手验收通过。

[输入清单](inputs.json)保留原始附件、项目副本和SHA256；`Inputs/`含五张题材参考与两张当前风格参考。`Prompts/`保存每次完整提示词及工具请求。V1增加攻速曾被工具输出审核拦截，第二次生成成功。V2五图初稿均一次生成；强化爆炸弹错轴反馈对应`13_ExplosiveEnhancement.v2-axis.txt`；本次爆破套餐三枚导弹反馈对应`11_ExplosivePackage.v2-axis.txt`。另外三张采用各自`.v2.txt`。

[V2.1采用结果](generation-results-v2_1.json)记录当前五张原始保存路径和采用提示词；[本次工具回执](generation-package-axis-result.json)保留三枚导弹编辑输出，完整请求为`Prompts/11_ExplosivePackage.v2-axis.request.json`。[V2结果](generation-results-v2.json)、[初稿结果](generation-initial-results-v2.json)及[前次单枚导弹依据](Research/axis_correction_20261118.json)保留历史。五张最终生成图均原样复制到项目。`manifest.json`记录尺寸、alpha、哈希与绑定，最新UE回读见[Icon11定向导入](../../../../Artifacts/GroundRogueCardIcons_20261118/PackageAxisRepair/install-report.json)。只检查文件与结构化数据；未进行结果读图、截图、PIE或原生构建。

## GitHub与公开资料检索

检索与采用理由见[研究记录](Research/prompt_research_20261118.json)，其中GitHub文件尽可能固定到已核对的commit。没有安装第三方技能或执行仓库代码，网页中的指令只作为研究材料。

| 来源 | 借鉴内容及本项目处理 |
|---|---|
| [GameServer图标记录](https://github.com/ginderjeremiah/GameServer/blob/9749e1ff8f60231e2b89a6257e16871fc091c626/docs/icon-art.md) | 将公共风格和每张主体分开描述，明确线条与干净表面；配色继续用本项目色库 |
| [CosmicRumble美术提示词](https://github.com/Eren-Ozcan/CosmicRumble/blob/39f04cddc3a1be831bcb7e734881824be0615e35/ART_MASTER_PROMPT.md) | 科幻武器/技能图标以清楚题材和小尺寸轮廓为中心；不引入其底框 |
| [OpenAI技能中的图标模板](https://github.com/openai/skills/blob/e6afb0d74cc75d220df2faf3dd6c635c2dc6a108/skills/.system/imagegen/references/sample-prompts.md) | 主体、画风和构图分别写，留出安全边距并排除场景背景 |
| [Scenario成套图标流程](https://learn.scenario.com/playbooks/game-icon-set/article) | 统一风格输入、同一模板逐图生成并提供小尺寸对比；本项目结果视觉核验仍由用户完成 |

具体的近大远小导弹编队、外粗内细线条、焦点分配和轴线修订是结合用户反馈做出的设计选择，不将公开提示词描述成必然能提高质量的保证。

V1补齐图标时，Cards12.01已从占位Artwork9接到Artwork14并刷新[同一评审地图](../../../../Artifacts/GroundRogueCardIcons_20261118/review-report.json)。前次V2重导五张纹理并更新图标表版本备注；V2.1图像修订只重导Icon11纹理。随后用户要求“进UE，并接入数据管线”，已将`GuLiStrikeIcons.xlsx/Icons!C13`同步为三枚导弹轴线修订的V2.1记录，走正常46表导出后原地更新UE图标表，保存回读见[管线交付](../../../../Artifacts/GroundRogueCardIcons_20261118/PipelineIntegration/README.md)。四张已有玩法卡的IconId绑定、Cards工作簿/JSON/UE表及全部当前PNG保持；无人生还Icon9已登记为独立资源，尚无玩法卡行。IconId、资源路径、牌面六层r5像素与地图均保持。五图外观、导弹直轴、缩小后的识别度和游戏实际显示由用户核验。
