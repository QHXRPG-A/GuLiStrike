# 战争机器能力视差卡牌

**当前UE版本：**[ModelComic_v9制作与操作](ModelComic_v9/Production/README.md)。射速和机动采用v9补腿/速度光线修订图，导弹伤害按用户选择采用v7美漫原图；三张六层图集、新材质与评审地图已经保存并从磁盘重载核对。视差4、偏转±16°、面积/厚度2倍与可编辑文案保留。助手未读图，最终效果待用户核验。

当前UE版本是干净美漫风的局部机械特写、六层插画视差、独立空白卡框和数据表驱动的 UMG 文字。目录中的 Tarot 是早期方案沿用的路径名。

**原图来源：**[v7真实模型参考与完整prompt](ModelComic_v7/README.md)、[v9局部修订及完整输入](ModelComic_v9/README.md)。插画生成记录与后续分层记录分开保存。

**历史UE版本（已被替换，用户画面反馈未通过）：**[v6美漫牌面制作与操作](Comic_Closeups_v6/Production/README.md)。旧源图和资产保留作回退，不能作为v9效果证据。

历史版本：[v5制作记录](Cel_Closeups_v5_Redraw/Production/README.md)、[v6初始完整图](Comic_Closeups_v6/README.md)。初始完整图先用于确定风格，生产图集经过后续修订，最新构图以UE预览为准。

v6历史效果：[三牌并排](Comic_Closeups_v6/Production/EdgeCoverageFix/AllCardsAfter/three-cards-comic.png) · [右上极限偏转](Comic_Closeups_v6/Production/EdgeCoverageFix/After/top-right.png) · [左下极限偏转](Comic_Closeups_v6/Production/EdgeCoverageFix/After/bottom-left.png)。旧版边缘修订和闪光过程只作历史记录。

v6历史完整卡面：[增加射速](Comic_Closeups_v6/Production/Previews/FireRate-actual-card.png) · [增加导弹伤害](Comic_Closeups_v6/Production/Previews/MissileDamage-actual-card.png) · [极速机动](Comic_Closeups_v6/Production/Previews/HighSpeed-actual-card.png)。本轮v9静态预览中断后已恢复编辑器，保存数据读回通过，实际画面由用户进入地图核验。

## 玩家操作

1. 打开 `/Game/GuLiStrike/Cards/WarMachineTarot/Maps/LVL_WarMachineTarotReview`。
2. 使用单机模式游玩。三张正面牌依次入场，每张 0.5 秒，共 1.5 秒。
3. 鼠标移到牌面不同位置，所在一侧向内压，最大偏转为每轴 16°；移出后回正。
4. 第一次点选锁定卡牌，0.45 秒翻到卡背。此后其他卡牌不能改选。
5. 翻面完成后，再按一次所选卡牌，播放 0.25 秒局部闪光；三张牌随后在 0.5 秒内退场。
6. 点击 HUD 的“重新播放”从正面三牌重新开始。

原版对照地图：`/Game/GuLiStrike/Cards/RevealDemo/Maps/LVL_CardRevealDemo`。

## 修改卡名与说明

卡底文字为 `/Game/GuLiStrike/Cards/WarMachineTarot/UI/WBP_CardText` 中两个 TextBlock，随实体卡牌移动、倾斜和翻转。它们不参与插画层间视差，不接收硬件鼠标或键盘焦点。卡框贴图不含文字。

**直接编辑 UE 数据表：**打开 `/Game/GuLiStrike/Cards/WarMachineTarot/Data/DT_CardText`，选择一行，修改 `Title` 与 `Description`，保存。重新开始一次展示即可读取新文案。

| 稳定行 ID | Title | Description |
|---|---|---|
| WarMachine_FireRate | 增加射速 | 提升战争机器的射击频率。 |
| WarMachine_MissileDamage | 增加导弹伤害 | 提升战争机器的导弹伤害。 |
| WarMachine_HighSpeed | 极速机动 | 提升战争机器的移动速度。 |

**批量填表入口：**编辑 [Data/CardTextSource.csv](Data/CardTextSource.csv)，仅有 `CardId,Title,Description` 三列。UE 打开且结束本演示游玩后，在项目根目录运行：

```powershell
python Scripts/ue_exec.py Scripts/Cards/import_card_text.py
```

该脚本把易填写的源表转换为保留本地化身份的 `CardText.csv`，更新 UE 数据表，并生成 `CardText_UE_Export.csv` 作回读证据。它不会修改插画、材质或蓝图。UE 数据表已经记录生成 CSV 的导入路径。重新导入以 CSV 为准，会覆盖 UE 表内同一批行的手工修改；批量维护时请把改动保留在源表。

目前未新增 XLSX 数据管线；后续 Excel 可使用相同三列结构，将导出接到此入口。

## 多语言准备

- 行结构 `FCardTextRow` 的两个字段均为 **FText**，经过蓝图直接传给 TextBlock，没有转成 FString 再显示。
- Namespace：`GuLiStrike.Cards`。
- Key：`<CardId>.Title` 和 `<CardId>.Description`，例如 `WarMachine_FireRate.Description`。修改中文正文不改变键；不要用中文标题充当 ID。
- 当前默认源语言为中文。后续通过 UE 本地化收集这些数据表 FText，导入翻译并生成目标语言目录。实际语言包和语言切换菜单尚未制作。
- 标题使用仅缩小的 ScaleBox；说明使用固定换行宽度和仅缩小的 ScaleBox，避免自动换行与缩放反复反馈。英文长文案只用作临时布局检查，不代表已经交付英文翻译。
- 后续需要带数值的文案应保留 FText 格式参数，例如 `{FireRateBonus}`，由本地化文本格式化流程替换。

## 牌面分层

当前v9每份图集是1254×1254 RGBA，3列×2行，单格418×627。顺序从左到右、从上到下。以下是分层制作要求；主体机械与连接放在第3层，实际合成结果仍待用户核验。

| 图集格 | 增加射速 | 增加导弹伤害 | 极速机动 |
|---|---|---|---|
| 1 近景 | 废弃机械残骸 | 最近的已离舱导弹与尾焰 | 近景地面速度带 |
| 2 能力动势 | 已飞离炮口的短弹迹 | 中距离已离舱导弹 | 分离的蓝紫推进尾流 |
| 3 完整机械 | 全部可见机械、腿盘及附着炮口火光 | 平行双舱、支座及出膛导弹 | 全部可见机械、腿盘与盘底光边 |
| 4 中景 | 被击溃部队、爆炸和短烟团 | 远处飞行导弹与爆炸 | 地面结构与速度环境 |
| 5 远景 | 远处烟柱、部队及剪影 | 远处环境 | 工业远景 |
| 6 最远背景 | 补全天空与地面 | 补全仰视天空 | 补全天空和高速地面 |

图集有真实Alpha通道，每格采样独立限界；主机械Alpha作平滑阈值映射，背景RGB作为不透明底层。固定牌面窗口与环境层缩放提供覆盖余量；边缘、遮挡和组件数量仍需在实际画面核验。

## 资产与可复用接口

- UE 资产根：`/Game/GuLiStrike/Cards/WarMachineTarot`。
- `Materials/M_CelCardParallax_ModelComic_v9`：复制项目已有视差父材质，保留动态纹样、六格UV、边界裁切和遮挡关系。商城原件只作引用来源。
- `MI_FireRate_ModelComic_v9`、`MI_MissileDamage_ModelComic_v9`、`MI_HighSpeed_ModelComic_v9`：可调`Layers global depth`及各层`Depth / Offset / Scale / Opacity`。两张纹理参数均使用对应新版图集。
- `M_CelCardUI_Comic_v6` 与三份 UI 实例：共用无字 `T_CelCardFrame_Comic_v6`；文本由 UMG 渲染。
- 共用 Director 的 `CardFrontMaterials`、`CardTextMaterials`、`CardTextDataTable`、`CardTextRowNames` 配置三牌。`SimpleCardFrame=true` 使用新卡框，原演示默认保持 Moon/Star/Tower。
- `StartPresentation()` 与 `OnPresentationFinished(SelectedIndex)` 保留。此样板不发放奖励、不修改战斗属性、不实现联网。

## 从参考扩展到下一张卡

1. 固定机械参考与已确认的卡牌风格参考，生成插画；参照 `ModelComic_v9/Production/Prompts` 保存分层提示词。
2. 按六格模板拆分，补全背景。附着火光、刚出膛弹体与机械采用相同深度或同一层；已飞离的物体独立。
3. 检查透明边缘、格子边界、遮挡顺序、重复背景和组件数量。通过 `Offset/Scale` 校准相对位置。
4. 导入图集，复制牌面材质实例并替换 `BaseColor Map` 与 `Ability Layer Map`；当前三张两参数均指向各自同一图集。复用空白卡框、卡背和文本控件。
5. 给文本源表增加稳定 `CardId` 与两段 FText，导入；在 Director 中填写材质与行 ID。
6. 在中心、四边、四角及 ±16° 下实际检查；原始生成图不能替代 UE 分层合成结果。

## 当前源文件与修订

- `ModelComic_v9/Production`：当前三张分层图集、实际图文prompt、来源哈希、UE保存与恢复后读回、回退绑定及操作说明。导弹原插画位于`ModelComic_v7`。
- 下列v6目录为历史源文件及当前复用的无字卡框，不代表当前牌面内容。

- `References`：已审机械参考、三渲二风格参考和原商城牌面导出。
- `Comic_Closeups_v6`：完整美漫风格预览及参考，原图中的文字仅用于审构图。
- `Comic_Closeups_v6/Production/Layers`：本版六层RGBA图集；`Corrected`保存炮口和完整悬浮盘修订，实际使用文件见Production/manifest.json。
- `Comic_Closeups_v6/Production/EmptyCardFrame.png`：无字通用卡框。
- `Comic_Closeups_v6/Production/Prompts`：分层及后续修订的完整imagegen提示词。
- `Comic_Closeups_v6/Production/Inspection`：UE配置、保存与编译读回；`Previews`：实际画面、偏转与尺寸检查。
- v3/v4/v5源图和旧UE材质实例均保留，不作为v6的验证证据。

面积和厚度在Director实例上分别调节，具体参数见[本版操作细则](ModelComic_v9/Production/README.md)。技术检查、助手视觉检查与用户美术验收分别记录。
