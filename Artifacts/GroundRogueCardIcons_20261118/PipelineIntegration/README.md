# 游骑兵图标UE与数据管线接入

2026-11-18，按用户“进UE，并接入数据管线”的要求，核对五张当前图标，完成爆破套餐V2.1源表记录同步、正常导出与UE图标表原地更新。当前五张正式Texture2D均已存在，源PNG、SHA256、纹理版本、UI压缩、无Mip、sRGB、透明设置及256×256尺寸回读通过。

数据链为`GuLiStrikeIcons.xlsx/Icons → DT_GuLiStrikeIcons_Icons.json → UE同名DataTable → GuLiIconCatalog → 游骑兵HUD`；卡牌通过`GuLiStrikeRogueCards.xlsx/Cards.IconId`引用图标。当前运行设置CDO的Cards指向正式`/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards`，不是演示表。静态代码中的HUD预载与已获卡显示使用这条IconId链，无需另改C++或Blueprint。

| 图标 | IconId | 玩法绑定 |
|---|---:|---|
| 无人生还 | 9 | 独立资源已登记；当前无玩法卡行 |
| 强化子弹 | 10 | Card10.01 |
| 爆破套餐 | 11 | Card11.01；三枚导弹修订V2.1 |
| 增加攻速 | 12 | Card09.01 |
| 强化爆炸弹 | 13 | Card12.01；Artwork14保持 |

[源表登记](registration.json)只改`Icons!C13`版本备注，没有改图标ID、正式路径、卡牌数值或机制。[46表导出核对](export-verification.json)确认仅Icons的JSON/CSV变化，Cards工作簿/JSON和全部生成头文件保持。[UE部署回读](install-report.json)确认图标表与当次导出逐行一致，卡牌表没有变化，五张纹理均指向当前源图；原有折射/灼烧/灼烧爆裂三个图标继续使用2/3/4。

当前源图与下载包仍在`ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1/`。美术清单和总目录的Icon11备注已与Excel一致；PNG像素没有重新生成或处理。

用户验证沿用`/Game/Maps/LVL_CommanderMassPrototype`的游骑兵Ground流程，获得对应肉鸽卡后查看HUD卡牌图标。已有`LVL_CardArtworkCatalogReview`继续用于牌面，不代表已验证实战图标。本次是现有资源和表的接入核对，无新增场景对象；未切换地图、启动PIE、编译、截图或助手结果读图，实际显示由用户核验。
