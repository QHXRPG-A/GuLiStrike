# 强化爆炸弹正式牌面与游骑兵卡牌图标清单

**会话状态：已完成。** 2026-11-18按用户要求结项并清理临时文件；当前资源与技术证据保留，历史视觉/运行验证范围不变。

2026-11-17，用户明确要求将当前r4作为正式资源接入，并列出现有游骑兵肉鸽牌Icon。初次入库时Gameplay卡12.01等待图标。2026-11-18用户要求制作五张独立图标后，已完成IconId13及ArtworkId14的正式绑定；[最新源表与UE保存回读](../../GroundRogueCardIcons_20261118/saved-readback.json)记录本次接入，游戏实际显示待用户核验。

随后用户标注地面光环两处断口，已用内置生图补绘r5并同步正式第4层。其余五层、卡框和运动参数保持；[该次保存回读](RingRepair_r5/saved-readback.json)记录唯一重导纹理、来源哈希、透明度设置和同地图引用。当前HTML为r5，初次r4登记证据仍保留。2026-11-18图标接入没有改动任何r5图层像素。

## 已入库资源

- ArtworkId：**14**，TemplateId：**7**。
- 正式材质：`/Game/GuLiStrike/Cards/GroundMech/ExplosiveEnhancement/Materials/MI_GroundMech_ExplosiveEnhancement_Artwork`。
- 六张正式贴图：同效果目录 `Textures/T_GroundMech_ExplosiveEnhancement_Layer1` 至 `Layer6`。
- 共享目录中的独立父材质：`/Game/GuLiStrike/CardSystem/RevealDemo/Materials/M_CardArtwork_RegisteredSixProjectileFront_v1`，按原HTML顺序6→5→4→1→2→3合成，未重建另外四张牌的父材质。
- 输入为[当前r4页面](../../../ArtSource/UI/GroundMechRogueCards/EnhancedExplosive_Card_Production_20261117_v1/index.html)的六张1504×1376 RGBA。源文件哈希、深度、运动归属和窗口来自其最终清单。

原卡框1280×1556和图片区1.0889386177:1保持。Scale=(1.0597759793,0.6463881860)，Offset=(0,-0.0883066255)，物理像素X/Y等比；本张授权窗口完整映射并铺满开口。爆炸随大陆移动，弹体和尾焰共用整层；最大运动范围与滤波内衬均有余量。详见[登记和映射](registration.json)。

源引擎UE5.7编辑器原本未运行，本轮启动已匹配BuildId的现有可执行程序，没有C++改动或原生构建。专用55557连接失败后使用项目12029编辑器Python接口。材质编译、六纹理尺寸/Alpha/颜色空间、MI参数及四张目录表保存回读通过，当前没有脏内容或脏地图。

## 评审入口

编辑器当前地图：`/Game/GuLiStrike/CardSystem/RevealDemo/Maps/LVL_CardArtworkCatalogReview`。

`CardArtwork_CatalogDirector`已保存并重新加载。点击Play后默认显示强化爆炸弹，顶部可切换强化爆炸弹、增加攻速、强化子弹、爆破套餐、无人生还；移动鼠标查看视差，单击翻面，再次单击退场，点击重播。此评审不发奖励。2026-11-18绑定后已刷新同一评审列表，由正式公共文本填充首次获取说明。

本轮未启动PIE、未截图或读图，玩家外观与交互待验。原HTML的74.6638%相似度记录保持；用户随后明确授权正式入库，并不把旧数值改记为通过。

## 当前图标

以下来自Excel和UE实际表/纹理回读。资源目录统一为 `/Game/GuLiStrike/GroundMech/UI/Icons/`。

| CardId | 卡牌 | IconId | 纹理资产 | 状态 |
|---|---|---:|---|---|
| 06.01 | 折射子弹 | 2 | T_GroundIcon_Ricochet | 独立图标，资源可加载 |
| 07.01 | 灼烧子弹 | 3 | T_GroundIcon_Burning | 独立图标，资源可加载 |
| 08.01 | 灼烧爆裂子弹 | 4 | T_GroundIcon_BurningBurst | 独立图标，资源可加载 |
| 09.01 | 增加攻速 | 12 | T_GroundIcon_FireRate | 独立蓝色图标，资源可加载 |
| 10.01 | 强化子弹 | 10 | T_GroundIcon_EnhancedBullet | 独立黑红图标，资源可加载 |
| 11.01 | 爆破套餐 | 11 | T_GroundIcon_ExplosivePackage | 独立紫色图标，资源可加载 |
| 12.01 | 强化爆炸弹 | 13 | T_GroundIcon_ExplosiveEnhancement | 独立紫色图标，已绑定正式Artwork14 |

无人生还（ArtworkId13）已新增独立紫色IconId9、纹理`T_GroundIcon_NoSurvivors`，仍没有玩法卡行，本次没有虚构其玩法配置。喷气容量、瞄准、闪现虽在同一图标目录，但不属于当前肉鸽卡绑定清单。

## 图标缺口与后续完成

项目[图标校验](../../../Tools/DataPipeline/icon_catalog.py)要求非占位的正式Ground牌面必须配置非零IconId。首次将12.01的ArtworkId从9改14时，导出报“必须配置 IconId”，没有输出新的JSON或表头。彼时恢复该单元格为9，保持IconId=0及其余玩法值不变；新Artwork14目录单独导出、部署。这段历史仅解释初次绑定为何未完成。

用户随后明确要求按五张参考制作独立简画图标并指定色系。2026-11-18已新增IconId9–13，四张已有玩法卡按各自独立图标绑定，12.01的ArtworkId从9改14；正常46表导出通过，生成头文件无变化，UE只更新四张卡的IconId及12.01的FrontMaterial。详见[五图制作与接入](../../GroundRogueCardIcons_20261118/README.md)。

## 证据

- [导入前编辑器与图标](editor-before.json)、[图标清单](icons-before.json)
- [材质编译](material-report.json)、[定向部署](deploy-report.json)
- [保存重载评审](review-report.json)、[最终资源回读](readback-report.json)
- [注册脚本](../../../Scripts/Cards/register_enhanced_explosive_artwork.py)、[定向绑定入口](../../../Scripts/Cards/install_enhanced_explosive_artwork.py)
