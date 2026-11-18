# 游骑兵五张肉鸽牌独立图标

2026-11-18 v1，以用户本轮五张附件为题材参考、当前 `SingleIcons_A_v4` 的折射子弹和灼烧爆裂子弹为表现参考，使用内置 `image_gen` 分别生成五张图。简画、粗描边、大色块和三档明暗为制作要求；结果视觉验收待用户，不把提示词要求登记成已验收结论。

[五图预览](index.html)支持浅色/深色/蓝色底与64/128/256显示尺寸。PNG保留原始1254×1254 RGBA像素和真实透明通道，未用脚本重绘、抠图、重配色或缩放；UE纹理构建尺寸为256×256。

| 图标 | IconId | 源图 | 正式纹理名 | 当前引用 |
|---|---:|---|---|---|
| 无人生还 | 9 | [PNG](Icons/09_NoSurvivors.png) | T_GroundIcon_NoSurvivors | 独立资源；Artwork13尚无玩法卡行 |
| 强化子弹 | 10 | [PNG](Icons/10_EnhancedBullet.png) | T_GroundIcon_EnhancedBullet | Card10.01 |
| 爆破套餐 | 11 | [PNG](Icons/11_ExplosivePackage.png) | T_GroundIcon_ExplosivePackage | Card11.01 |
| 增加攻速 | 12 | [PNG](Icons/12_FireRate.png) | T_GroundIcon_FireRate | Card09.01 |
| 强化爆炸弹 | 13 | [PNG](Icons/13_ExplosiveEnhancement.png) | T_GroundIcon_ExplosiveEnhancement | Card12.01；正式Artwork14 |

正式目录为 `/Game/GuLiStrike/GroundMech/UI/Icons/`。数据由 `data/Excel/GuLiStrikeIcons.xlsx/Icons`、`data/Excel/GuLiStrikeRogueCards.xlsx/Cards` 经正常导出接入，未修改机制、数值或前置。旧2/3/4图标保持；没有复用已退役ID7。

## 配色与生成追溯

配色依据[项目统一色库](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md#11-全项目统一色库v192026-10-08)。本批所选基色记录在[制作计划](generation-plan.json)与[最终清单](manifest.json)：增加攻速选蓝色组，强化子弹选黑红组，其余三张选紫色组。由内置生图执行配色；没有将像素色差或视觉效果记为助手验收通过。

[输入清单](inputs.json)保留原始附件、项目副本和SHA256；`Inputs/`含五张题材参考与两张当前风格参考。`Prompts/`保存每次完整提示词及工具请求。增加攻速第一次生成被工具输出审核拦截；第二次进一步简化为虚构卡通多管机枪后生成成功，实际使用 `12_FireRate.retry1.txt`。其余四张均使用首次输出。

[生成结果](generation-results.json)记录内置生图原始保存路径和最终采用的提示词，五张生成图均原样复制到项目。`manifest.json`记录尺寸、alpha、哈希、图标注册及绑定。数值检查与UE保存回读见[导入报告](../../../../Artifacts/GroundRogueCardIcons_20261118/install-report.json)；未进行结果读图、截图、PIE或原生构建。

强化爆炸弹的图标缺口补齐后，Cards12.01从占位Artwork9接到已正式入库的Artwork14，原六层r5像素不变。[同一评审地图](../../../../Artifacts/GroundRogueCardIcons_20261118/review-report.json)已刷新并保存。五图外观、缩小后的识别度和游戏实际显示由用户核验。
