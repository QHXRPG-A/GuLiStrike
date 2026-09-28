# v6美漫能力卡：制作与操作

当前评审地图已替换为本目录的六层美漫资源。内部视差深度为4，整牌最大偏转为每轴16°，卡面面积与厚度均为初版的2倍。卡名与说明仍是数据表驱动的UMG/FText。

[最新UE三牌画面](EdgeCoverageFix/AllCardsAfter/three-cards-comic.png) · [边缘修复右上近看](EdgeCoverageFix/After/top-right.png) · [边缘修复左下近看](EdgeCoverageFix/After/bottom-left.png)

用户后续发现两侧露出图层边界，已扩大机动卡环境层覆盖并加入固定卡框裁切。主盘尺寸及4/16/2/2参数不变，后方盘随第4层等比放大。最新修订及同角度前后证据见[边缘覆盖修复](EdgeCoverageFix/README.md)；旧Previews目录保留为修复前历史。

## 玩家操作

1. 打开 `/Game/GuLiStrike/Cards/WarMachineTarot/Maps/LVL_WarMachineTarotReview`，单机游玩。
2. 等待左、中、右依次飞入，每张0.5秒，共1.5秒。
3. 在牌面移动鼠标查看压动与内部视差；卡牌中心保持原位。
4. 单击一张锁定，等待0.45秒翻到卡背。
5. 翻完后再单击同一张，播放0.25秒局部闪光，然后三牌在0.5秒内退场。
6. 点击“重新播放”。

原版对照地图为 `/Game/GuLiStrike/Cards/RevealDemo/Maps/LVL_CardRevealDemo`，仍默认Moon/Star/Tower。

## 本次修订

| 用户反馈 | 当前实现 |
|---|---|
| 画面脏，换美漫 | 清晰墨线、整块明暗、安静背景；所有生成提示词保留干净视觉及五项禁止词。 |
| 炮口一大一小、火光错位 | 重新统一炮口结构；两团附着火光直接画入完整机械层，随炮管使用同一UV/深度，避免悬停错位。保留自然透视和不完全相同的火焰外轮廓。 |
| 三发导弹移到上方天空 | 第1/2/4格独立已离舱导弹通过位置参数上移并错开；第3格仍保留一枚刚出舱导弹及双舱。 |
| 轮盘完整 | 重绘主盘完整外周、补齐右侧盘缘和上方支架，重新调整完整机械层构图；窄尾流继续单独分层。 |

## 图层与源文件

每牌6层，图集1254×1254 RGBA，3列×2行，单格418×627。卡框及两段文字另外独立显示。

| 顺序 | 射速 | 导弹伤害 | 极速机动 |
|---|---|---|---|
| 1 | 近景机械残骸 | 最近的自由飞行导弹 | 近景速度带 |
| 2 | 两条短弹迹 | 中距离自由导弹 | 蓝紫窄尾流 |
| 3 | 双联炮、装甲、附着枪口火光 | 双舱、支座、出膛导弹 | 完整主盘、支架、盘底光边 |
| 4 | 中景爆炸与残骸 | 远处自由导弹 | 后侧盘及尾流 |
| 5 | 远景工业剪影与烟尘 | 工业剪影和爆炸 | 远景建筑 |
| 6 | 补全天空与地面 | 补全仰视天空 | 补全天空与地面 |

最终主图集：`Layers/Corrected/FireRate_Atlas.png`、`Layers/MissileDamage_Atlas.png`、`Layers/Corrected/HighSpeed_Atlas.png`。

机动卡第2层使用 `Layers/HighSpeed_Atlas.png` 的对应单格，以保留修订主盘前的完整尾流；其余5层仍取修订后的主图集。材质只增加一份能力采样来源，仍然是6层。共用无字框为 `EmptyCardFrame.png`。来源、SHA-256、8份生产提示词及修订稿见[manifest.json](manifest.json)。图片修改均使用imagegen。

## 编辑数值与文字

评审地图内选择 `WarMachine_CardDirector`：

| 参数 | 当前值 | 含义 |
|---|---:|---|
| MaximumTilt | 16 | 每轴最大整牌偏转角，单位度 |
| CardAreaMultiplier | 2 | 面积倍数，长宽按平方根缩放 |
| CardThicknessMultiplier | 2 | 厚度倍数 |
| EntryDuration / FlipDuration | 0.5 / 0.45 | 每张入场与翻面秒数 |
| FlashDuration / ExitDuration | 0.25 / 0.5 | 局部闪光与共同退场秒数 |

材质均在 `/Game/GuLiStrike/Cards/WarMachineTarot/Materials`：三份 `MI_*_Comic_v6` 牌面实例的 `Layers global depth=4`（初版基准1、前版2）。各层 `Depth / Offset / Scale / Opacity` 可分别调整；整牌偏转与内部深度独立。

父材质 `M_CelCardParallax_Comic_v6` 保留动态纹样，增加 `Ability Layer Map`；通常它与 `BaseColor Map` 指向同一图集，只有机动卡第2层引用单独能力图集。主机械与附着效果不要拆成不同深度。卡框使用 `M_CelCardUI_Comic_v6` 和三份 `MI_*_UI_Comic_v6`。

文字编辑：打开 `/Game/GuLiStrike/Cards/WarMachineTarot/Data/DT_CardText`，修改 `WarMachine_FireRate`、`WarMachine_MissileDamage`、`WarMachine_HighSpeed` 三行的 `Title` / `Description`，保存并重播。批量CSV、稳定本地化键及未来翻译入口见[总操作说明](../../README.md)。本轮未改变行结构，也未新增翻译目录。

## 复用与验证

复用顺序：固定机械/风格参考 → 生成局部插画 → 六层透明图集及背景补全 → 导入贴图 → 复制牌面实例并校准层位置 → 配置Director三项材质与文字行 → 实际UE极限偏转检查。完整提示词在 `Prompts`，本次用户标注在 `../References/UE-layout-user-markup.png`。

可复现脚本为项目 `Scripts/Cards/apply_warmachine_v6.py`、`preview_warmachine_v6.py`、`finalize_warmachine_v6.py`。导入和保存脚本要求编辑器处于专用地图且已结束游玩；预览脚本则要求该地图PIE运行，会临时冻结流程并调用蓝图姿态入口，完成后需结束预览。

6份蓝图编译0错误0警告，22项包依赖无缺失，地图已保存并读回；见[资产检查](Inspection/final-assets.json)、[接入配置](Inspection/v6-integration.json)。本轮实际UE渲染采集中心、四边、四角，核对每轴±16°和中心不位移；见[姿态记录](Previews/v6-preview.json)。不同视口的真实尺寸以 `Previews/layout-*.json` 为准。

助手已检查火光连接、导弹分布、主盘完整性和极限偏转边缘。SceneCapture完整卡面仅供构图，颜色以PIE截图为准。没有把脚本驱动的姿态检查当作OS鼠标测试；本轮未重跑全套翻面/闪光/完成事件或语言切换测试，旧版记录只作历史依据。用户美术与交互手感终验待反馈。
