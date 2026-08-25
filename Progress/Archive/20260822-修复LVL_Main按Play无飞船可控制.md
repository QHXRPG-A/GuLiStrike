# 2026-08-22 解决了：LVL_Main 按 Play 不生成可控飞船

- 对应开发文档：无（独立修复，诊断+关卡配置）
- 变更类型：内容资产（关卡 WorldSettings + 删 Actor）

## 变更清单

| 文件/资产 | 变更 |
|---|---|
| `Content/Maps/LVL_Main.umap` | WorldSettings 增加 GameMode 覆盖 = `/Game/GuLiStrike/Ship/BP_ShipGameMode.BP_ShipGameMode_C` |
| `Content/Maps/LVL_Main.umap` | 删除手摆的 `BP_GuLiStrikeShip_C_0`（原位于 (0,0,1000)，Auto Possess=Disabled，PIE 中只是无人摆设） |

## 做了什么

诊断（先定位链条断点，再动手）：

1. **C++ 侧不存在任何"生成飞船"代码**。`Source/` 全部 SpawnActor 调用只涉及 NPC/投射物/特效；飞船生成 100% 依赖引擎标准流程：GameMode 的 `DefaultPawnClass=BP_GuLiStrikeShip`（配在 `BP_ShipGameMode` 蓝图上）在 PlayerStart 生成 DefaultPawn。
2. **三个根因叠加**（日志与活编辑器均实锤）：
   - 编辑器当前/默认启动图是 LVL_Main，其 WorldSettings **无 GameMode 覆盖**；
   - 回落全局 `GlobalDefaultGameMode=BP_TwinStickGameMode_C`，但 `Content/GuLiStrike/Blueprints/BP_GuLiStrikeGameMode.uasset` 是 **LFS 指针 stub**（日志：`Invalid value for PACKAGE_FILE_TAG`、`Game class is 'GameModeBase'`），引擎只能用裸 GameModeBase → 生成 DefaultPawn 观战胶囊；
   - 场景里手摆的飞船 Auto Possess=Disabled，永远不会被控制。
3. **飞船链路本身完好**（先行验证再改）：LVL_ShipTest 覆盖→BP_ShipGameMode（DefaultPawnClass=BP_GuLiStrikeShip_C、PC=BP_PC_Ship_C）均在；飞船 BP CDO DefaultParts 4 部件齐全。

修复（经 UnrealMCPython 在活编辑器执行）：

1. `unreal.find_object(None, '/Game/Maps/LVL_Main.LVL_Main:PersistentLevel.WorldSettings')` → `set_editor_property('default_game_mode', BP_ShipGameMode_C)`，回读确认。
2. `delete_by_label('BP_GuLiStrikeShip')` 删手摆船（避免引擎另生成一艘后场景出现双船）。
3. 保存 LVL_Main。
4. LFS 损坏资产**本次不修**（用户决策；给 LVL_Main 加覆盖后该资产不再影响本图 PIE）。

## 验证

- 日志：`[2026.08.22-14.18.28] LogLoad: Game class is 'BP_ShipGameMode_C'`（修复前同图为 `GameModeBase` + 资产加载报错）。
- PIE game world 实测：PlayerController=`BP_PC_Ship_C`；Player Pawn=`BP_GuLiStrikeShip_C_0` 且被占有；`tuning_preset=Default`、`base_max_speed=1200`（调参已应用）；已装 4 部件（2×标准引擎 thrust 600、1×重型引擎 thrust 1000、1×激光炮 damage 10）；场景飞船总数=1（无重复）。
- 已知无害警告：`LogGameMode: FindPlayerStart: PATHS NOT DEFINED or NO PLAYERSTART with positive rating`——飞船仍正常生成被控制，原因未查，不影响功能。

## 遗留问题

- `BP_GuLiStrikeGameMode.uasset` 等 twin-stick 资产仍为 LFS 指针 stub，需 `git lfs pull` 修复；在那之前**清掉 LVL_Main 的 GameMode 覆盖会让该图回落到损坏的全局 GameMode**（又是裸 GameModeBase）。
- FindPlayerStart 评级警告原因未查（不影响出生，待以后顺手看）。
- 飞船出生点沿用 twin-stick 竞技场旁的 PlayerStart（约 -198420,-23480,-8061）；要改出生位置挪/加 PlayerStart 即可。
