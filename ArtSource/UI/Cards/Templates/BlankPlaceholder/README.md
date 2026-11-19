# 空白牌面占位

用于游骑兵 09.01 增加攻速、10.01 强化子弹、11.01 爆破套餐。模板仅提供有效的透明牌面材质；卡框、标题和说明由现有卡牌系统呈现，不制作插画、图标或六层图片。

- Excel：GuLiStrikeCardArtwork.xlsx 的 Templates/BlankPlaceholder、Artworks/GroundWeaponPlaceholder。
- 父材质：`/Game/GuLiStrike/Cards/Shared/Placeholder/M_CardBlank`。
- 共用实例：`/Game/GuLiStrike/Cards/Shared/Placeholder/MI_CardBlank`。
- Layout=None；Layers 与 Parameters 为空；HUD IconId=0，保留空槽、等级和卡名提示。
- 制作入口：`Scripts/GroundMech/Rogue/author_initial_bullet_assets.py`；定向导入：`install_initial_bullet_cards.py`。

后续绘制正式卡图时，在源表中替换 ArtworkId/IconId，不更改卡牌效果类和已获等级。
