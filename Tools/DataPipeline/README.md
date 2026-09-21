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

### 特殊任务

[`data/Excel/GuLiStrikeSpecialTasks.xlsx`](../../data/Excel/GuLiStrikeSpecialTasks.xlsx) 的 `Tasks` 表维护自动工作定义，前三行仍为列名、类型、必要性。`ApplicableUnitIds` 引用 Soldiers.id，多个值用英文逗号分隔；匹配兵种获得资格，`AutoActivate` 决定是否自动激活，`LifetimePolicy` 只允许 `InitialOnce` 或 `Persistent`。持续停止、已消耗资格和执行进度属于各单位的对局状态，不回写共享表。

| ID | 行名 | 兵种 ID | 生命周期 | Tag | 原生执行类路径 |
|---|---|---|---|---|---|
| 1 | Mining | 3 | Persistent | Task.Special.Mining | /Script/GuLiStrike.GuLiMiningSpecialTaskExecutor |
| 2 | Construction | 4 | Persistent | Task.Special.Construction | /Script/GuLiStrike.GuLiConstructionSpecialTaskExecutor |
| 3 | StrongholdAdvance | 1,2 | InitialOnce | Task.Special.StrongholdAdvance | /Script/GuLiStrike.GuLiStrongholdAdvanceSpecialTaskExecutor |

`ExecutorClass` 类型为 `softclass`，填完整原生类路径，不填源文件名或 C++ 的 `U` 前缀。执行器必须继承 `UGuLiSpecialTaskExecutor`，是非抽象原生类，并支持配置 Tag 和每个绑定兵种；运行时每个 World 按类复用对象，各单位进度放在任务实例中。新增工作类型需注册 GameplayTag 并实现执行器，再由表选择具体类；目录不再维护另一套 ID → 类映射。

1. 保存 Excel 后运行 `python Tools/DataPipeline/export_data_from_excel.py`，导出器校验 ID、行名、字段、生命周期、Tag 格式、兵种引用和类路径格式。
2. 若新增表、列或原生反射类，关闭编辑器，正常编译源码版 `GuLiStrikeEditor`。
3. 通过 `python Scripts/ue_exec.py Scripts/import_special_tasks.py` 导入，或使用源码版 Editor Python commandlet 执行同一脚本。结果保存到 `TestResults/CommanderOrders/special_task_import.json`。
4. 原生目录在 World 初始化时加载 DataTable，校验 Tag 已注册、类可加载、继承关系、原生性、非抽象及兵种能力。配置错误明确报告并阻止目录启用，没有旧硬编码回退。

运行中的对局不热换配置；重新开始游戏后生效。客户端只提交任务 ID 和目标，执行类由服务端目录决定。实现与验收见[指挥官部队操作与特殊任务系统](../../Progress/DevelopmentDocumentation/20260920-指挥官部队操作与特殊任务系统.md)。

前三行是列名、类型、必要性元数据，第4行开始填数值。`id` 是稳定数字ID，`name` 是稳定UE行名，两者不要因显示文本变化而改写。
距离默认使用厘米，弹速用厘米/秒，`AttackRatePerSecond` 是每秒发数，`CooldownSeconds` 是秒/发。
`Soldiers` 新增的 `ModelWidthMeters`（模型宽度）和 `MinAvoidanceDistanceMeters`（最小避障距离）明确使用米；单元格保存数字，`m` 仅为显示格式。

### 指挥官单位体型与净距

两列均为 `float / Necessary`，模型宽度表示最终游戏尺度，不修改模型比例。

2026-09-19按用户要求撤回严格净距算法。当前只有战争机器（WM01 / Id 2）读取宽度12.5米，初始化时按宽度×50转换为625厘米Mass避障半径，不再乘 `PresentationScale`。其他兵种保持原有半径。

最小避障距离列及原始数值保留（扫荡者0.5米、战争机器2.5米、工程车1米），当前不参与运行时计算，不承诺硬净距。模型宽度无效时报告兵种ID和字段，使用625厘米战争机器回退半径；其他字段沿用既有目录校验。

正常修改数值后运行导出器，再通过
`python Scripts/ue_exec.py Scripts/import_commander_unit_footprints.py` 定向导入Soldiers。新增反射字段须先关闭编辑器、正常编译并重启。
导入会把四行回读结果写入 `TestResults/UnitSpacing/soldiers-import.json`；正在运行的实体使用创建时缓存的体型，重新开始游戏后使用新值。
当前实施与验收记录见[战争机器模型大小适配](../../Progress/DevelopmentDocumentation/20260919-战争机器模型大小适配.md)，前期取舍见
[代码接入与成本分析](../../Progress/DevelopmentDocumentation/20260918-指挥官单位体型与避障距离配置及接入分析.md)。

### 武器表导入

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

## 游戏文本（2026-09-20）

`Data/Excel/GuLiStrikeGameTexts.xlsx / Texts` 是本轮指挥官界面文案唯一编辑源。为直接满足文本维护需求，这张表采用三列特例，不要求其他表的 id/name/Note：

| 行 | A | B | C |
|---|---|---|---|
| 1 | 文本id | 介绍 | 内容 |
| 2 | str | str | str |
| 3 | Necessary | Necessary | Necessary |

文本id 为稳定英文标识（支持点、下划线），导出为 Name/TextId；介绍说明用途，内容为实际显示文案，可含换行和 `{0}`、`{1}` 等运行数据占位符。不得修改已引用 ID 或删除所需占位符。导出校验空值、元数据、大小写重复 ID 及 C++ 引用缺失。原有表规则不变。

导出仍运行 `python Tools/DataPipeline/export_data_from_excel.py`；结构变化后编译，再在 UE Python 执行 `Scripts/import_game_texts.py`。导入逐字段回读并保存 `DT_GuLiStrikeGameTexts_Texts`。运行时 `GuLiGameText` 只读加载 DataTable，FText 处理参数，缺失项显示 ID 并报告错误。数字格式、标识与兵种／建筑已有名称仍由各自事实源负责，不复制进第二份配置。


## 特效目录（2026-09-21）

`Data/Excel/GuLiStrikeVfx.xlsx / Effects` 统一维护视觉资源与三轴基础缩放。资源路径和缩放相同必须合并 ID，用途描述不参与去重。其他源表只存整数 `…VfxId`，可选为 0；必需字段拒绝 0。导出前校验目录、跨表和 INI 引用，错误时不写任何生成物，并生成 `GuLiVfxIds.h` 供 C++ 使用。

导入使用 `Scripts/Vfx/import_vfx_tables.py`，目录资源为真正软引用，DataTable 所在目录已纳入 Cook。完整 API、ID 维护、异步加载、缩放与蓝图说明见 [特效目录维护](../../Scripts/Vfx/README.md)。
