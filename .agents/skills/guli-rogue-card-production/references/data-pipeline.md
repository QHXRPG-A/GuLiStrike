# 数据、文案与效果类

项目根默认 `D:/UE5.7/test1`；以下为2026-09-28实际表头/代码核对结果。编辑前读当前源表及相关实现，不把本文当生成数据的第二事实源。

## 唯一源表与字段

`data/Excel/GuLiStrikeRogueCards.xlsx / Cards`：第1行字段名、第2行类型、第3行必要性，第4行起是数据。`_说明`不导出。使用excelize-cli；ID写入必须显式 `--type string`，保留 `01.01`、`01.10`。

| 字段 | 类型 | 契约 |
|---|---|---|
| id | str | 两位卡族.两位等级，正则 `[0-9]{2}\.[0-9]{2}`；等级不能00；唯一，不能按小数计算 |
| name | str | 唯一稳定UE行名，例如WM01_FireRate_Lv1，与显示标题无关 |
| Note | str | 策划说明，Optional，不直接显示 |
| Type | int | 1指挥官部队、2Ship、3地面机甲、4建筑；当前运行时只支持1 |
| TextIds | str[] | JSON字符串数组，**恰好两项**：标题文本id、说明文本id |
| ImplementationClass | softclass | 具体非抽象GuLiRogueCardEffect子类，完整软类路径 |
| UnitTypeId | int | 当前引用GuLiStrikeCommander.xlsx/Soldiers.id；WM01=2 |
| BonusPercent | float | 有限正数，0.2=20%，0.5=50%；不是每秒发数或绝对速度 |
| FrontMaterial | softobject | 完整卡面材质路径，`/Game/.../Asset.Asset` |
| UpgradeVfx | softobject | 完整项目NiagaraSystem对象路径 |
| UpgradeVfxScale | float | 有限正数，按单位实际尺寸适配；WM01当前6 |
| UpgradeVfxColor | str | 线性HDR `(R=20,G=6.930114,B=0.933554,A=1)`；RGB非负有限，A在0–1 |

除Note外当前均Necessary。Type枚举完整不代表所有类型已实现；`GuLiRogueCardSubsystem::LoadCatalog`目前拒绝非1类型，导出器的单位引用也按Soldiers检查。扩展Ship/机甲/建筑时先实现目标解析与结算，再填正式行。

### 当前三行

| id / UE行名 | BonusPercent | 效果类后缀 | 美术标识 / 目录 |
|---|---:|---|---|
| 01.01 / WM01_FireRate_Lv1 | 0.2 | GuLiRogueCardFireRateEffect | FireRate / FireRate |
| 02.01 / WM01_MoveSpeed_Lv1 | 0.5 | GuLiRogueCardMoveSpeedEffect | HighSpeed / MoveSpeed |
| 03.01 / WM01_MissileDamage_Lv1 | 0.2 | GuLiRogueCardMissileDamageEffect | MissileDamage / MissileDamage |

类路径前缀为 `/Script/GuLiStrike.`。HighSpeed仍是已存在的美术和文本键标识，不因为目录叫MoveSpeed就重命名键。候选展示顺序来自Settings，当前是01.01、03.01、02.01，不能依赖Excel行顺序。

新增同类等级通常复用实现类和可兼容牌面，添加如01.02及新行名/数值；不自动把玩家重复取得01.01升级为01.02。

## 公共文本与本地化

源表 `data/Excel/GuLiStrikeGameTexts.xlsx / Texts` 的三列是 **文本id／介绍／内容**，类型均str，同样前三行元数据。`TextIds` 引用第一列，不是数字行号、Excel地址、卡牌id或“介绍”。例：

```json
["Card.WM01.FireRate.Title", "Card.WM01.FireRate.Description"]
```

| 文本id | 内容示例 |
|---|---|
| Card.WM01.FireRate.Title | 增加射速 |
| Card.WM01.FireRate.Description | 战争机器的普攻射速提高 {0}%。 |
| Card.WM01.HighSpeed.Description | 战争机器的移动速度提高 {0}%。 |
| Card.WM01.MissileDamage.Description | 战争机器的导弹伤害提高 {0}%。 |

`{0}`由BonusPercent×100格式化，当前最多两位小数；不在两个表各维护一份数值。可复用同一参数化说明，不为每个等级复制相同句子。界面提示使用 `UI.RogueCards.*`。

导出文本DataTable后，还要重建 `/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts`，Namespace `GuLiStrike.GameTexts`，Key取行的TextId。运行时 `FText::FromStringTable` → `FText::Format` → `WBP_CardText` 的 `CardTitle`/`CardDescription`。不要用FString替换本地化身份，不烘焙中文字，不把旧评审用DT_CardText/CSV变成实战文案第二源。

新增语言走UE本地化收集与翻译资产；保持Key、占位符、顺序稳定。没有要求时不生成翻译或增加语言列。

## 管线操作

1. 用 `xlsx info/read` 确认表和元数据，再使用 `write --data-file` 批量原地编辑。不要重跑初始化脚本覆盖已有行。`Tools/DataPipeline/author_rogue_cards.py`仅初次建表；已存在时会拒绝。`extend_rogue_upgrade_fields.py`是已有字段迁移器，使用前检查其范围。
2. 执行 `python Tools/DataPipeline/export_data_from_excel.py`。这是项目导出器，不臆造单表命令行开关；它校验跨表文本/单位引用、两项数组、正数、完整软引用和HDR格式。
3. 生成物为 `data/Json/DT_GuLiStrikeRogueCards_Cards.json`、文本JSON、manifest及 `Source/GuLiStrike/Gameplay/Data/Generated/*TableRows.h`。**不手改生成物**。查看导出报告；只加行通常不改变反射结构，结构变化则按本轮授权编译加载再导入。
4. UE空闲、所需原生类已加载后，定向导入卡牌和公共文本表，并重建StringTable。已存在的 `Scripts/Cards/author_rogue_card_entry.py` 中 `tables()` 就是该步骤；不要为改表执行整个部署脚本，它还迁移资产、重写图表和保存地图。

可在 `Scripts/ue_exec.py` 运行的范围明确入口：

```python
import runpy
from pathlib import Path
import unreal
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
entry = runpy.run_path(str(root / 'Scripts/Cards/author_rogue_card_entry.py'))
report = entry['tables']()
print(report)
```

通用导入器是 `Scripts/import_data_to_engine.py`，通过 `GULI_TABLE_FILTER` 限定两张表；内部走CSVImportFactory。沿用该接口，避免已知会挂起的 `fill_data_table_from_json_string`、`fill_data_table_from_csv_string` 路径。单独运行 `Scripts/import_game_texts.py` 不等于已经刷新卡牌用StringTable。

5. 保存并重新读取三类资产：卡牌DataTable、文本DataTable、StringTable。比较ID字符串、TextIds顺序和引用、效果类继承关系、UMaterialInterface及NiagaraSystem类型、数值。记录源表→导出→导入的结果，不只看命令退出码。

## 效果类与权威规则

实现入口在 `Source/GuLiStrike/Gameplay/Cards/GuLiRogueCardEffect.*`、`GuLiRogueCardSubsystem.*`。一个**机制类**可服务多张配置卡，不是每卡每等级生成C++类或蓝图。

- 射速：ArmySkill修饰WM01的 `BasicAttack.AttackRate`，不改主动导弹冷却。
- 导弹：修饰 `MissileLauncher.Damage`，进入真实法术场/爆炸结算；已经发射的快照不回改。
- 移速：权威阵营＋兵种倍率，固定步同步速度、加速度、Mass参数；不改全军公共基准。
- 每次成功取得独立SourceId，重复取得逐次乘算。两次射速2→2.4→2.88，两次移速720→1080→1620，导弹30→36→43.2。
- 本局己方现有和后续同兵种受益；换任保留、新局清空。会话确认幂等，客户端不能提交任意倍率或替服务器写属性。
- 配置错误、超现有数值上限、目标不支持时拒绝本次提交并保留旧值。不要为展示卡图顺手改变平衡、候选规则或联网权限。
