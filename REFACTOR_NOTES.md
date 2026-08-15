# 重构说明：test1 → GuLiStrike（2026-08-15 最终状态）

## 完成的重构

1. **玩法三合一**：TopDown / Variant_Strategy / Variant_TwinStick → 以 TwinStick 为核心的单一玩法，内容统一在 `/Game/GuLiStrike/`
2. **主地图**：`/Game/Maps/LVL_Main`（复制自 Scifi_Skies ScifiSkies_Map_03），竞技场含 PlayerStart、NavMeshBoundsVolume、4 刷怪点、4 拾取物，PIE 验证通过
3. **项目改名**：test1 → GuLiStrike（模块/类/宏/日志/CoreRedirects 链 TP_TopDown→test1→GuLiStrike）

## 最终内容结构

```
/Game
├── GuLiStrike/          # 玩法内容（原 Variant_TwinStick + MI_Colorway）
│   ├── Blueprints/ (+AI/)
│   ├── Input/ (+Actions/ +Touch/)
│   ├── FX/  Meshes/ (+GC/)  Spell/  UI/  Materials/
├── Maps/LVL_Main        # 主地图
├── Scifi_Skies/         # 场景素材包
├── LevelPrototyping/ Characters/ Blueprints/  # 模板与共享资源
└── Collections/ Developers/
```

## 引用重定向

`Config/DefaultEngine.ini` 的 `[CoreRedirects]` 含 32 条 PackageRedirects（/Game/Variant_TwinStick/* 与 /Game/TopDown/MI_Colorway → /Game/GuLiStrike/*）。旧资产引用在加载时自动解析到新路径；各资产下次保存时将写入新路径。

## 已删除（git 历史可恢复）

- Content/TopDown（除 MI_Colorway，已移至 GuLiStrike/Materials）
- Content/Variant_Strategy、Content/Variant_TwinStick（除已移动的 31 个资产）
- Content/Cursor（全项目无外部引用的孤儿）
- LVL_TwinStick / Lvl_TopDown / LVL_Strategy 三张旧地图及其 754 个外部 Actor/Object 文件

## 孤儿 C++（保留编译，无蓝图引用）

- Variant_Strategy/：AStrategyGameMode、AStrategyPawn、AStrategyPlayerController、AStrategyUnit、AStrategyHUD、UStrategyUI
- 模板类：AGuLiStrikeCharacter、AGuLiStrikeGameMode、AGuLiStrikePlayerController
