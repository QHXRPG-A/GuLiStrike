# 数据、文案与效果类

项目根默认 `D:/UE5.7/test1`；以下为2026-09-29源表/代码契约。反射字段/效果类的编译、加载与导入状态以对应Progress记录为准，不能将源代码状态当成运行验证。编辑前读当前源表及相关实现，不把本文当生成数据的第二事实源。

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
| BonusPercent | float | Optional；百分比效果为有限正数，0.2=20%；解锁/整数效果为0 |
| RequiredCardIds | str[] | 全部依赖至少获得一次；空数组表示无依赖 |
| ExcludedCardIds | str[] | 任一互斥已获则不合格；单向填写按双向生效 |
| MaxAcquisitions | int | 非负整数；0无限叠加，1每局每队一次 |
| BonusCount | int | Optional；整数效果正整数，其他效果为0 |
| FrontMaterial | softobject | 完整卡面材质路径，`/Game/.../Asset.Asset` |
| UpgradeVfx | softobject | 完整项目NiagaraSystem对象路径 |
| UpgradeVfxScale | float | 有限正数，按单位实际尺寸适配；WM01当前6 |
| UpgradeVfxColor | str | 线性HDR `(R=20,G=6.930114,B=0.933554,A=1)`；RGB非负有限，A在0–1 |

Note、BonusPercent、BonusCount 为 Optional，后两项依效果类校验；依赖/互斥无配置填写 `[]`。Type枚举完整不代表所有类型已实现；`GuLiRogueCardSubsystem::LoadCatalog`目前拒绝非1类型，导出器的单位引用也按Soldiers检查。扩展Ship/机甲/建筑时先实现目标解析与结算，再填正式行。

### 当前五行

| id / UE行名 | BonusPercent | 效果类后缀 | 美术标识 / 目录 |
|---|---:|---|---|
| 01.01 / WM01_FireRate_Lv1 | 0.2 | GuLiRogueCardFireRateEffect | FireRate / FireRate |
| 02.01 / WM01_MoveSpeed_Lv1 | 0.5 | GuLiRogueCardMoveSpeedEffect | HighSpeed / MoveSpeed |
| 03.01 / WM01_MissileDamage_Lv1 | 0.2 | GuLiRogueCardMissileDamageEffect | MissileDamage / MissileDamage |
| 04.01 / WM01_MissilePod_Lv1 | 0 | GuLiRogueCardMissilePodEffect | MissilePod / MissilePod |
| 05.01 / WM01_RainSalvo_Lv1 | 0 | GuLiRogueCardMissileCountEffect | RainSalvo / RainSalvo |

类路径前缀为 `/Script/GuLiStrike.`。HighSpeed仍是已存在的美术和文本键标识，不因为目录叫MoveSpeed就重命名键。03.01和05.01依赖04.01；04.01的MaxAcquisitions=1；05.01的BonusCount=1、MaxAcquisitions=0。

候选从有效卡表按本队已获记录筛选，等概率不重复抽取最多三张；不再由Settings维护固定候选顺序。同一未确认选择重开不重抽；只有显式“重选”请求才替换，优先抽未显示的合格牌并从合格旧牌补足，无其他合格牌则保留原候选并提示。重选免费且不发奖励，新Session使旧确认失效。确认和固定步结算均复核资格，并考虑正在提交的奖励；不足三张显示实际数量，零张走公共文本提示。

新增同类等级通常复用实现类和可兼容牌面，添加如01.02及新行名/数值；不自动把玩家重复取得01.01升级为01.02。

## 公共文本与本地化

源表 `data/Excel/GuLiStrikeGameTexts.xlsx / Texts` 的三列是 **文本id／介绍／内容**，类型均str，同样前三行元数据。`TextIds` 引用第一列，不是数字行号、Excel地址、卡牌id或“介绍”。例：

```json
["Card.WM01.FireRate.Title", "Card.WM01.FireRate.Description"]
```

| 文本id | 内容示例 |
|---|---|
| Card.WM01.FireRate.Title | 增加射速 |
| Card.WM01.FireRate.Description | `<Unit>重防号</> <Gain>普攻射速+{0}%</>` |
| Card.WM01.HighSpeed.Description | `<Unit>重防号</> <Gain>移动速度+{0}%</>` |
| Card.WM01.MissileDamage.Description | `<Unit>重防号</> <Gain>导弹伤害+{0}%</>` |
| Card.WM01.MissilePod.Description | `<Unit>重防号</> 解锁<Gain>导弹技能</>` |
| Card.WM01.RainSalvo.Description | `<Unit>重防号</> <Gain>导弹齐射数量+{0}</>` |

百分比效果的`{0}`由BonusPercent×100格式化（最多两位小数）；整数效果直接格式化BonusCount；解锁文案无数值占位。不在两个表各维护一份数值。可复用同一参数化说明，不为每个等级复制相同句子。界面提示使用 `UI.RogueCards.*`。

导出文本DataTable后，还要重建 `/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts`，Namespace `GuLiStrike.GameTexts`，Key取行的TextId。运行时 `FText::FromStringTable` → `FText::Format` → `WBP_CardText` 的 `CardTitle`/`CardDescription`。不要用FString替换本地化身份，不烘焙中文字，不把旧评审用DT_CardText/CSV变成实战文案第二源。

2026-09-29用户确定效果说明最多一行。单位、增益属性及数值采用黄色 `#FFD84A` 加粗；`Unit`/`Gain` 样式相同，连接词用默认暖白。不在玩家牌面或悬浮提示添加“可重复叠加”“每局仅可获取一次”等机制说明；内部Note和玩法数据继续保留真实规则。`Tools/DataPipeline/author_rogue_card_text.py` 只修改五条已有说明，逐行回读并核对卡表未变；初始化/迁移脚本共用其模板，运行时仍以Excel为唯一数据源。

说明控件改为RichTextBlock后必须同步原生赋值类型和蓝图SetContent，并使用共享样式表；不要先把富文本标签导入旧TextBlock。关闭AutoWrapText且将WrapTextAt设为0，现有ScaleBox仅向下等比缩放。构建、加载和UE接入的当前完成状态以[单行文案实施记录](D:/UE5.7/test1/Progress/DevelopmentDocumentation/20260929-肉鸽卡牌单行文案与高亮.md)为准。

### 高亮样式的Excel唯一来源

`data/Excel/GuLiStrikeRogueCardUI.xlsx / TextStyles`遵循标准前三行元数据。`id/name/Note`为标准列，`name`对应富文本标签或Default。其他列为`FontAsset`、`FontSize`、`Typeface`、`ColorSRGB`、`OutlineSize`、`OutlineColorSRGB`。颜色使用`#RRGGBB`；当前Unit/Gain为`#FFD84A`、Bold、38号、1像素同色描边；Default为暖白Regular、无描边。标题不使用这张说明样式表。

正常管线为Excel → 项目导出器 → `DT_GuLiStrikeRogueCardUI_TextStyles.json`及生成行类型 → 同名UE数据表 → 自动转换为UMG的`DT_CardTextStyles`。转换脚本只做字段映射和sRGB转线性，不写死设计数值。导出器校验颜色、字号/描边范围、字体引用、说明单行及样式标签引用/闭合。表结构变化才需要原生编译。

日常修改文案/样式后先运行项目导出器，再在空闲编辑器执行`Scripts/Cards/apply_single_line_card_text.py`，同步公共文本、StringTable、样式源表和UMG派生表，保存并逐项回读。该脚本读取当次导出，不以首次迁移记录为数据源。`author_rogue_card_styles.py`仅初始化已有三种样式，新工作簿存在时拒绝覆盖；`author_rogue_card_text.py`仅用于本次已批准文案迁移，不当作日常导出入口。

对已加载的文本/样式表使用`fill_data_table_from_json_string`原地更新；不要通过CSVImportFactory替换被引用的DataTable对象，已确认会在结束PIE后因残留强引用触发断言。

新增语言走UE本地化收集与翻译资产；保持Key、占位符、顺序稳定。没有要求时不生成翻译或增加语言列。

## 管线操作

1. 用 `xlsx info/read` 确认表和元数据，再使用 `write --data-file` 批量原地编辑。不要重跑初始化脚本覆盖已有行。`Tools/DataPipeline/author_rogue_cards.py`仅初次建表；已存在时会拒绝。`extend_rogue_upgrade_fields.py`是已有字段迁移器，使用前检查其范围。
2. 执行 `python Tools/DataPipeline/export_data_from_excel.py`。这是项目导出器，不臆造单表命令行开关；它校验跨表文本/单位引用、两项数组、效果数值契约、完整软引用和HDR格式。依赖校验包含未知ID、重复/自引用、依赖环、同时依赖与互斥、依赖闭包内的双向互斥冲突；错误应定位工作表、数据行及卡ID。迁移既有卡表使用幂等的 `author_wm01_missile_cards.py`，不以初始化脚本重建表。
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
- 导弹仓：队伍Source.Unlocks解锁初始锁定的 `MissileLauncher`；对本队现有和后续重防号生效，模型/HUD/权威Q入口读取同一状态。
- 雨点攻势：`ProjectileCount` 基值1，使用 `IntegerMagnitude` 和 AddFlat，每次+1；一次Q整轮预检并同时发射，左右仓交替，每轮成功只计一次冷却。
- 移速：权威阵营＋兵种倍率，固定步同步速度、加速度、Mass参数；不改全军公共基准。
- 每次成功取得独立SourceId。百分比效果重复取得逐次乘算：两次射速2→2.4→2.88，两次移速720→1080→1620，导弹30→36→43.2；整数弹量为1→2→3，不走浮点百分比。
- 本局己方现有和后续同兵种受益；换任保留、新局清空。会话确认幂等，客户端不能提交任意倍率或替服务器写属性。
- 配置错误、超现有数值上限、目标不支持时拒绝本次提交并保留旧值。不要为展示卡图顺手改变平衡、候选规则或联网权限。
