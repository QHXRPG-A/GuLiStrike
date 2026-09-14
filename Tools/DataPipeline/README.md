# GuLiStrike Excel 数据维护

次级单位（非玩家直接操控单位）的武器参数统一维护在
[`Data/Excel/GuLiStrikeSecondaryWeapons.xlsx`](../../Data/Excel/GuLiStrikeSecondaryWeapons.xlsx)。

| 工作表 | 维护内容 |
|---|---|
| `UnitSkills` | 指挥官兵种与武器槽关联、基础伤害、每秒发数、射程 |
| `Skills` | 技能执行方式、标签和“产生的法术场”（引用 Fields.id） |
| `WeaponMounts` | 兵种模型局部空间的枪口及瞄准点，厘米 |
| `WingmanWeapons` | 对空机枪、对地轰炸、基础自动枪、齐射导弹；武器资产、伤害、冷却、射程、弹速及攻击参数 |
| `WingmanTargeting` | 僚机索敌和释放半径、归队门槛、扫描间隔 |
| `Projectiles` | WM01实体导弹资产、兵种/槽/技能绑定及飞行、转向、碰撞、寿命参数 |

地面兵种ID仍在 `GuLiStrikeCommander.xlsx / Soldiers`；飞船及部件参数仍在
`GuLiStrikeShip.xlsx`。所有法术场/AOE的伤害、半径和时序统一在
`GuLiStrikeSpellFields.xlsx / Fields`，包括武器爆炸和传送；次级武器工作簿不再保留 `WeaponFields`。
枪火、尾焰、爆炸样式等纯美术表现继续由其美术DataAsset/Niagara维护。

## 日常修改

前三行是列名、类型、必要性元数据，第4行开始填数值。`id` 是稳定数字ID，`name` 是稳定UE行名，两者不要因显示文本变化而改写。
所有距离使用厘米，弹速用厘米/秒，`AttackRatePerSecond` 是每秒发数，`CooldownSeconds` 是秒/发。

`Skills` 和 `WingmanWeapons` 的“产生的法术场”列为 `int / Optional`，填写 `Fields.id`。
WM01导弹目前填 `1`，僚机对地导弹填 `2`；机枪等没有法术场引用的攻击留空或填0。
导弹发射与对地轰炸必须填写有效的Combat法术场ID；不存在的ID、类型错误或重复维护会在导出时拒绝。

1. 在武器表修改武器参数或法术场ID；在法术场表修改AOE参数，保存并关闭文件。
2. 在项目根目录运行 `python Tools/DataPipeline/export_data_from_excel.py`。
3. 停止PIE，运行 `python Scripts/ue_exec.py Scripts/import_secondary_weapon_data.py`。
4. 导入脚本先导入统一Fields，再导入武器表。检查 `TestResults/SecondaryWeapons/import-report.json` 的 `passed` 和逐行原生回读。
5. 下一次开始游戏时使用新快照；进行中的战斗/已发射弹体保留其原有冻结配置。

只有新增列、改变类型或增加工作表时需要在第2和第3步之间重新编译 `GuLiStrikeEditor`。
JSON、CSV、DataTable是生成物，不作为日常数值维护入口。

## 伤害与弹道规则

- 在 `Skills` 中填写了“产生的法术场”的武器，其 `UnitSkills/Damage` 必须为0；实际伤害在 `GuLiStrikeSpellFields.xlsx / Fields` 中维护。
- 僚机对地轰炸的 `Damage` 留空，使用“产生的法术场”所引用的全局法术场数值。
- 普通地面机枪与僚机 `WingmanMachineGun` 都是即时命中；对空弹速字段目前不造成伤害飞行延迟。
- 僚机 `WingmanBasicAuto`、`WingmanMissileSalvo` 的 `AttackPattern=Legacy` 表示保留既有武器执行链。它们也从此工作簿读取参数，不再依赖资产中的内联数值。
- WM01的 `MotionProfileRow` 由导入脚本自动绑定；发射时用原生解析器读取并冻结 `Projectiles` 行。配置无效时不会静默退回资产数值。

## 序列化兼容与全局法术场

迁移保留已有 DataTable 资产名和 USTRUCT 名。以 `DT_GuLiStrikeCommander_UnitSkills` 为例，其真实来源现在是
`GuLiStrikeSecondaryWeapons.xlsx / UnitSkills`，以 `Data/Json/manifest.json` 的 `sources` 为准。
旧名字仅表示序列化身份。

所有法术场只有 `GuLiStrikeSpellFields.xlsx / Fields` 一个维护入口，manifest也只记录这一来源。
导出器将“产生的法术场”的数字ID解析为该行的 `name`，写入既有运行时 `EffectConfigId` 字段；
这只是自动生成的兼容引用，无需在Excel同时维护两列。
武器上下文的引用优先于特效资产的默认ConfigId，因此改武器引用后，伤害、半径和时序按所选法术场解析，
美术资产继续提供表现。未提供武器引用的直接调用仍使用资产默认法术场。
所有工作簿通过校验后才写生成物，校验失败不更新JSON、头文件或manifest。

`Scripts/update_wingman_attack_source.py` 已改为普通导出兼容入口，不再覆写一套Python初始数值。
历史的 `migrate_unit_and_spell_field_tables.py` 在新工作簿存在时会退出，避免重新创建旧维护入口。
历史的 `migrate_secondary_weapon_tables.py` 识别统一法术场版本后会退出，避免再次拆出 `WeaponFields`。
本次归并脚本为 `Scripts/consolidate_spell_field_source.py`，完成后重复运行不覆写数值。
