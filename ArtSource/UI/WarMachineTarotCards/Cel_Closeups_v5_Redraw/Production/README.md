# v5 干净牌面 UE 制作与操作

用户在查看 v5 三张完整图后要求“开始制作UE资源，替换之前的”，同时要求增强层间位移、卡牌面积和厚度各变为原来的两倍。本目录保存由该三图制作的正式分层素材、提示词、材质配置记录与 UE 预览；完整原图保持在上一级。

## 打开与操作

地图：`/Game/GuLiStrike/Cards/WarMachineTarot/Maps/LVL_WarMachineTarotReview`。使用专用单机 GameMode，点击编辑器“游玩”。

1. 三张牌从远处依次入场，每张 0.5 秒，总计 1.5 秒。
2. 移动鼠标到牌面边缘，所在一侧向内压；中心固定，每轴仍为 ±12°。内部六层的深度参数由 1 提高为 2。
3. 单击一张牌，锁定选择并在 0.45 秒翻到背面；其他牌不能改选。
4. 翻完后重新按一次所选牌，中心闪光 0.25 秒，随后三张牌在 0.5 秒内一起退场。
5. 点击“重新播放”恢复三张正面牌。

原版对照地图仍是 `/Game/GuLiStrike/Cards/RevealDemo/Maps/LVL_CardRevealDemo`。

## 编辑数值与文字

选中地图里的 `WarMachine_CardDirector`，在 Presentation Tuning 中修改：

| 参数 | 当前值 | 作用 |
|---|---:|---|
| CardAreaMultiplier | 2 | 面积倍数，宽高各乘 √2；不是宽高各乘 2。 |
| CardThicknessMultiplier | 2 | 正反面间距倍数；独立于面积，避免厚度被额外乘 √2。 |
| MaximumTilt | 12 | 整牌悬停偏转上限，本轮未加大。 |
| EntryDuration / FlipDuration / FlashDuration / ExitDuration | 0.5 / 0.45 / 0.25 / 0.5 | 原有流程时长。 |

放大后的命中区域仍使用稳定牌面坐标。操作提示移至屏幕高度 94%，避开加大的卡牌。实体深色侧边位于正反面之间，翻面时可见；卡牌根节点始终均匀缩放，文字不会因增加厚度而拉宽。

牌面材质在 `/Game/GuLiStrike/Cards/WarMachineTarot/Materials`：`MI_FireRate_Redraw_v5`、`MI_MissileDamage_Redraw_v5`、`MI_HighSpeed_Redraw_v5`。共同参数 `Layers global depth=2` 控制内部视差；每层还有 Depth / Offset / Scale / Opacity。不要靠改变 MaximumTilt 来代替图层深度。

卡底仍是 `WBP_CardText` 的两个可编辑 TextBlock；打开 `/Game/GuLiStrike/Cards/WarMachineTarot/Data/DT_CardText` 编辑 `Title`、`Description`，保存后重新展示。源表、FText 键与未来多语言步骤见[总操作细则](../../README.md#修改卡名与说明)。新框图不含文字，未把预览图中的字导入牌面。

## 六层对应

每张为 1254×1254 RGBA，3 列×2 行，每格 418×627；从左到右、从上到下编号。六层共用牌面坐标，在材质中映射到上方插画窗口，底栏单独覆盖。

| 层 | 射速 | 导弹伤害 | 极速机动 |
|---|---|---|---|
| 1 | 近景废弃机械 | 最近的已离舱导弹与尾焰 | 近景地面速度带 |
| 2 | 两个附着炮口火光 | 中距离已离舱导弹 | 主悬浮盘后的窄尾流 |
| 3 | 完整双联炮、装甲和连接 | 双导弹舱、安装座、出膛导弹 | 主盘、支架、装甲、盘底窄光带 |
| 4 | 较远残骸及火烟 | 远处小导弹 | 后方小悬浮盘及尾流 |
| 5 | 工业建筑及远烟 | 命中爆炸与工业建筑 | 高架和建筑 |
| 6 | 补全的天空和地面 | 补全的天空 | 补全的天空和地面 |

射速火光与炮管采用相同深度；出膛导弹和导弹舱、主悬浮盘和盘底发光带各在同一层。已飞离物体与背景使用不同深度。文字与框不参与插画内部视差。

## 复用步骤

1. 保存已批准完整图和机械参考；本轮使用内置 imagegen，没有调用外部 API CLI。
2. 参照 `Prompts/*_Layers.txt` 生成六格真实透明图集，补齐背景和被遮挡部位；禁止把卡框、标题、说明画进图层。
3. 对照完整图，在 UE 中检查层的位置和遮挡。射速炮口重新对齐；导弹前景横向错开，避免覆盖出膛口；插画窗口整体映射避免底栏裁掉主体。
4. 导入图集，复制 `MI_*_Redraw_v5`，替换 `BaseColor Map`；复用 `M_CelCardParallax`、空白卡框和文字控件。
5. 填写数据表行与 Director 的三项材质/行名数组，保存专用地图。`Scripts/Cards/apply_warmachine_v5.py` 记录本轮导入和参数，旧 v3/v4 资产保留。
6. 在实际游戏透视相机下检查中心和极限偏转，再看翻面、闪光与退出。完整卡面导出用于检查构图，颜色以 PIE 截图为准。

## 证据与审核边界

- [UE 三牌](Previews/three-cards-clean.png)、[左上偏转](Previews/hover-top-left.png)、[右下偏转](Previews/hover-bottom-right.png)。
- [尺寸和流程记录](Previews/runtime-live.json)：113 项直接蓝图/几何检查通过，含入场节奏、三种选择、锁定、翻面、确认、清理、重播与固定中心。
- 本轮按既有用户验证计划检查 16:9、16:10、21:9；该检查不等于真实鼠标或窗口失焦的自动化验证。
- 原图放行依据是用户明确要求制作 UE 资源；当前分层合成画面仍待用户最终美术与手感验收。
- 未改 C++、未运行原生编译；没有接入属性加成、奖励、翻译目录或语言切换。
