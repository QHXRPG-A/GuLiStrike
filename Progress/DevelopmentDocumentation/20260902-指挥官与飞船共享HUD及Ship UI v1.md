---
schema: guli-progress/v1
id: DEV-20260902-002
work_id: WORK-20260902-002
kind: development
role: root
title: 指挥官与飞船共享 HUD 及 Ship UI v1 — 技术方案
areas:
- commander
- ship
- ui
- network
- assets
status: in_progress
verification: partial
created: '2026-09-02'
updated: '2026-09-04'
summary: UGuLiCommanderHUDWidget 会在外层 WidgetTree 中按 FName 查找文本、图片、按钮、进度填充和布局岛。若把这些节点移动到嵌套共享 WBP 内部，外层查找可能失效，且不一定产生明显编译错误。因此本轮只共享叶子资源和样式；共享复合
  WBP 仅供新 Ship UI 或无运行时引用的纯装饰区域，且必须在后续 UE 阶段单独验证
next_action: 保留 Commander 全部既有数据、输入、MiniMap、Tooltip 和生命周期行为
relations:
  requirement: REQ-20260902-002
status_note: 实施中
---

# 指挥官与飞船共享 HUD 及 Ship UI v1 — 技术方案

- 对应需求：[指挥官与飞船共享 HUD 及 Ship UI v1](../RequirementDocument/20260902-指挥官与飞船共享HUD及Ship UI v1.md)
- 当前阶段：Figma Gate 1、UE 静态 Gate 2 已完成；完整逻辑 Gate 3 未开始

## 技术选型

### 结论

- 设计端先建立一套 Commander/Ship 共用的 Figma token 与视觉组件，再分别组合为 Commander HUD 和 Ship HUD。
- UE 端继续使用项目已有 UMG/Slate，不引入 CommonUI、MVVM 或新插件。
- 共享以颜色、字体、材质、Brush、ButtonStyle、图标和纯装饰基元为主；Commander 本轮采用原位换肤，不把 Native 引用节点封装为共享子 `UserWidget`。
- Ship UI 后续使用独立 WBP 和只读表现快照；不与 Commander 强行共享业务 ViewModel。
- UE Gate 2 直接硬引用 `/Game/NEONCTRL_FuturisticClea_UIKit` 的贴图基元，不复制商城资源、不另造材质层；以可重复执行的 Editor Python 构建脚本维护两套 WBP。
- 本轮只做静态 UI：未修改 `Source/`、`Config/`、玩法逻辑、输入模式或生命周期。

### 为什么不在本轮共享完整 WBP

`UGuLiCommanderHUDWidget` 会在外层 `WidgetTree` 中按 `FName` 查找文本、图片、按钮、进度填充和布局岛。若把这些节点移动到嵌套共享 WBP 内部，外层查找可能失效，且不一定产生明显编译错误。因此本轮只共享叶子资源和样式；共享复合 WBP 仅供新 Ship UI 或无运行时引用的纯装饰区域，且必须在后续 UE 阶段单独验证。

### 为什么不采用 CommonUI/MVVM

当前 Commander 已经形成稳定的 UMG + Native Adapter 模式：WBP 保持零业务逻辑，Native 负责委托、计时器和销毁。为一次视觉统一引入新 UI 框架会扩大输入、生命周期、打包和测试范围，不符合“不改变功能行为”的要求。

## 已确认基线

### 参考与视觉基线

- Gate 2 最终成品风格只参考 `/Game/NEONCTRL_FuturisticClea_UIKit/Preview/T_Gameplay`；Inventory、Pause、MainMenu 等成品预览不作为本轮布局参考。
- Figma 继续作为信息层级、五区尺寸、安全边距和三画幅规则来源；NEONCTRL `T_Gameplay` 决定黑蓝底、青色斜切框、导轨和局内 HUD 构图。
- `Preview/T_Gameplay` 仅用于视觉比对，不作为运行时 Brush；WBP 使用包内独立 Gameplay、Icons 及少量通用装饰 Texture2D。
- Commander 权威色板：`#050A12`、`#0B1622`、`#102532`、`#18D7FF`、`#75A3B8`、`#D1AF86`、`#EDF8FF`、`#E44758`。
- 字体使用 Noto Sans SC，字号 10/11/12/13/17/20；间距 4/8/12/16/24/32；描边 1/2/3；轮廓使用直角/45° 斜切。
- 形态规则：几何单线透明图标，无大圆角、重阴影和烘焙辉光。

### Commander 运行时基线

- HUD 资产：`/Game/Commander/UI/Widgets/WBP_CommanderHUD`
- Native Parent：`UGuLiCommanderHUDWidget`
- 当前 Widget 数：213
- HUD 宿主：`AGuLiCommanderHUD`
- 运行时模式：Native 绑定数据和输入，WBP 无业务 EventGraph、Binding、MVVM、Tick。
- 输入阻挡岛：`SB_TopStatus`、`SB_MapDesign`、`SB_DockDesign`、`SB_ShortcutsDesign`。
- 当前没有正式 UMG 动画规范；按钮状态主要依靠 Tint、亮度、边框和透明度，Disabled 约为 32% 视觉强度。

### Ship 逻辑数据基线

后续可从真实飞船状态构建的表现数据包括：当前/最大速度、总推力、总质量、偏航速度、压弯角、Boost 状态和已应用部件。v1 只完整设计载具同步、合法裸舰、已装配、加力、Pawn 丢失/重生和航向离屏。雷达、目标锁定、方向护盾、船体生命、弹药、热量、航点和停靠目前没有完整权威数据接口，只允许紧凑“未接入”样本，不制作完整假状态，也不制作五类 MFD 展开态。

## 规划资产与路径及 Gate 2 落地

Gate 1 的原规划如下。Gate 2 没有复制 NEONCTRL 资源到共享目录，也没有创建仅为换肤服务的材质实例；两套 WBP 直接形成可 Cook 的硬引用，避免重复资产与路径漂移。

| 用途 | 建议路径 | 说明 |
|---|---|---|
| 共享主题 | `/Game/GuLiStrike/UI/Shared/BattleHUD/Theme` | 色板、字体层级和公共样式数据 |
| 共享材质 | `/Game/GuLiStrike/UI/Shared/BattleHUD/Materials` | 斜切标题、边框、扫描和克制辉光 |
| 共享纹理 | `/Game/GuLiStrike/UI/Shared/BattleHUD/Textures` | 通用图标和可复用纹理 |
| 共享装饰 WBP | `/Game/GuLiStrike/UI/Shared/BattleHUD/Widgets` | 仅纯装饰或新 Ship 组件 |
| Ship WBP | `/Game/Ship/UI/Widgets` | Ship UI 角色专用组合层 |
| Ship 图标 | `/Game/Ship/UI/Textures/Icons` | 飞船专用原创图标 |
| Figma token 镜像 | `data/UI/Shared/BattleHUD/tokens.json` | 后续导出/审计用，不是本轮产物 |
| 共享构建 helper | `Scripts/UI/Shared/battle_hud_primitives.py` | 后续统一写入样式，不是本轮产物 |

共享资源必须由 WBP 或材质实例形成可 Cook 的硬引用。现有 Commander 图标和 C++ 路径本轮不搬迁、不删除；旧材质也先保留为兼容资源，清理由独立任务处理。

实际落地产物：

- `/Game/Ship/UI/Widgets/WBP_ShipHUD`：新建的纯静态 Ship HUD。
- `/Game/Commander/UI/Widgets/WBP_CommanderHUD`：保留原路径与 Native 合同，原位替换 Brush/Tint。
- `Scripts/build_neonctrl_battle_hud.py`：唯一构建入口，可重复执行并在保存前校验结构、焦点、图表、动画与 MVVM 合同。
- `outputs/review/neonctrl-ui/`：三画幅、指挥官运行时截图、构建报告和最终资产审计。

## Figma 实施规格

### Figma 结构（3 个物理页 + 10 个顶层 Section）

Figma Starter 方案最多允许 3 个物理页，因此没有创建 10 个物理页；以下 10 个顶层 Section 与批准的 10 个逻辑页一一对应：

1. `00 · UI 系统总览`：范围、状态、页面导航与交付入口。
2. `01 · 基础规范`：色板、Noto Sans SC 字号、间距、描边、透明度和 Motion Scale。
3. `10 · 共享组件`：面板、边框、导轨、标题板、数值条、状态徽标、命令格和图标状态。
4. `11 · HUD 指示器`：航向标记、同步、加力、状态与紧凑“未接入”样本。
5. `20 · 指挥官 · 主 HUD`：现有 213 节点 HUD 的共享基元映射与静态主稿。
6. `21 · 指挥官 · 状态`：现有默认、悬停、选中、禁用和数据状态；UE 资产保持 0 Animation。
7. `30 · 飞船 · 主 HUD`：1920×1080 玩家实机视图与三画幅响应式规则。
8. `31 · 飞船 · 状态`：载具同步、合法裸舰、已装配、加力、Pawn 丢失/重生和航向离屏。
9. `90 · UE 交付`：尺寸、命名、图标导出、动效、状态和资产交接合同。
10. `99 · 参考资料 · 排除`：第三方/水印参考、舰队管理画面和明确排除项。

### Gate 1 最终结果

- Figma：[Ship UI 设计系统](https://www.figma.com/design/2NS9XNysjlO8KLJdTilsua)
- 2026-09-04 通过 Figma Desktop Bridge MCP 复核时，当前文件中的玩家主稿为 [`14:1381`](https://www.figma.com/design/2NS9XNysjlO8KLJdTilsua?node-id=14-1381)，三画幅为 `15:1698`、`14:1381`、`15:1808`，状态板为 `15:1918`，Commander 主稿为 `14:1116`。
- 旧交付记录中的 `23:2660` 在当前文件查询结果为不存在；Gate 2 没有猜测或依赖该失效节点，改用当前可读的 `14:1381` 与三画幅节点。
- 设计系统规模：4 个 Variable Collections、77 个 Variables、6 个 Noto Sans SC Text Styles、83 个 Components、14 个 Component Sets、483 个 Instances。
- 质量门：Broken Aliases = 0、Broken Instances = 0、Image Paints = 0、Prototype Reactions = 0、Reserved Icon Instances = 0、Design Sample Hits = 0。
- Ship 实屏：1280×720、1920×1080、2560×1080 三张验证画幅均为完整五区组件实例，并已移除“设计样例”。
- 当前玩家主稿节点为五个直接区域，尺寸分别为 420×64、300×244、320×284、840×56、88×88；安全边距 24px，无重叠。
- 状态与本地化：Ship 7 张状态卡、Commander 主画面和全文件画布可见普通文案均已中文化；仅保留项目/技术专名、快捷键、编号、令牌/属性合同和参考文件名。
- 当前可用主要节点：`14:1381`、`15:1698`、`15:1808`、`15:1918`、`14:1116`。

Desktop Bridge 没有 Metronome 能力，Motion API 因此保持 **Closed**。本次没有 Live Timeline，也没有 Prototype Reactions；只交付 0.42s、0.8s、1.2s、0.18s 与 `Motion Scale = 0` 的静态动效合同。Gate 1 完成不代表可播放的 Figma 动画已经制作。

### UE Gate 2 最终结果

- Ship HUD 新建为 83 Widgets、`RootCanvas` 下五个直接布局岛；三画幅固定设计尺寸，顶/左/右/底均保留 24px 安全边距，左右面板上移避开底部快捷栏。
- Commander HUD 完成 91 处原位 Texture2D Brush 替换；仍为 213 Widgets，名称/类型/父子关系及 `SB_TopStatus`、`SB_MapDesign`、`SB_DockDesign`、`SB_ShortcutsDesign` 几何保持不变。
- 两套 WBP 均通过 WidgetService 层级校验与 Blueprint 编译，均为 0 EventGraph 节点、0 Animation、0 ViewModel、0 MVVM Binding；Commander 5 个 Button 全部保持 `IsFocusable=false`。
- Asset Registry 审计确认 Ship 有 23 个、Commander 有 13 个 `/Game/NEONCTRL_FuturisticClea_UIKit` 硬依赖；未以 `Preview/T_Gameplay` 大图替代可复用控件贴图。
- 构建脚本在最终布局上连续执行两轮结果一致；第二轮报告 Ship `created=false`、两套 Blueprint 均为 `UpToDate`。
- 1280×720、1920×1080、2560×1080 Ship 离屏截图与 Commander PIE 运行时截图通过人工视觉检查；中心飞行区无遮挡，底部区域无重叠，小屏和超宽屏均保持布局岛边界。
- 现有自动化 `GuLiStrike.Commander.UI` 9/9 Success、0 Warning、0 Error，其中包含 HUD AssetContract、UnitType、CommandFeedback、Shortcuts ViewportPlacement 与 HealthBar 策略测试；本轮未新增或扩充测试。
- 本阶段只交付静态占位信息。速度、Boost、姿态、装配、Pawn 重绑和航向投影等真实数据接入仍属于 Gate 3。

### Ship 主布局与状态

1920×1080 使用以下固定设计尺寸，外围安全边距为 24px：

| 区域 | 设计尺寸 | 内容 |
|---|---:|---|
| 顶部状态岛 | 420×64 | Air/Ship 可用性、同步或 Ready 摘要 |
| 左侧飞行遥测 | 300×244 | 当前/最大速度、偏航速度、压弯角、Boost |
| 右侧装配状态 | 320×284 | 总推力、总质量、合法裸舰或已应用部件 |
| 底部快捷提示 | 840×56 | 只展示现有飞行输入提示，不创建可点击 UI |
| 舰艏航向标记 | 88×88 | 运行时投影；离屏时显示安全区方向箭头 |

同一布局需验证 1280×720、1920×1080 和 2560×1080。完整状态只包括：

- `Syncing`：PlayerState 已为 Air，但 Pawn、装配或移动配置尚未 Ready。
- 合法裸舰：Revision 已发布且部件为空，显示 `未安装`，不能标为失败。
- 已装配：只显示已应用 Revision 的稳定部件列表、总推力和总质量。
- 加力：只在真实 `bBoostActive` 时显示动态强调。
- Pawn 丢失/重生：保留 HUD 同步语义，清空旧 Pawn 值并等待重绑。
- 舰艏航向：屏内显示舰体 `+X` 航向，离屏显示安全区箭头，不命名为准星或锁定。

雷达、锁定、方向护盾、船体生命、弹药、热量、航点和停靠集中为一个紧凑“未接入”样本区。不得设计联系人、锁定进度、生命/护盾比例、弹药计数、热量数值、航点距离或停靠误差等完整状态；不制作 Self/Target/Nav/Comms/Engineering MFD 展开态。

### 动效规范

| Ship 动效 | 精确时长 | 触发与表现 |
|---|---:|---|
| HUD 入场 | 0.42s | 各岛错峰淡入并平移不超过 8px，`EASE_OUT` |
| 加力强调 | 0.8s 往返 | 仅真实加力生效时循环明度，不以本地按键冒充确认 |
| 载具同步 | 1.2s 循环 | 仅 `Syncing` 时显示三段扫描 |
| 舰艏航向标记 | 0.18s | 淡入并缩放到终态；离屏后使用方向箭头 |

- `Motion Scale = 0` 时，所有 Ship 动效直接显示静止终态且保留状态含义。
- 不对快速刷新数字做逐字符、闪烁或位移动画；不设计锁定、虚假告警或 MFD 动效。
- Commander UE 资产本轮保持 **0 Animation**，不添加 UMG Animation、循环材质或播放逻辑。
- 动效不得改变布局、命中、Visibility、Clipping、焦点或飞行输入。

## 图标制作与导入合同

### 源图

- 简单原创几何单线图标，使用 24×24 设计网格和 1.5–2px 线宽。
- UE 交付为 128×128 白色透明 RGBA PNG，无文字、背景、外框、阴影、水印、棋盘格和烘焙辉光。
- 保持统一视觉重心和安全留白；24px 显示仍能区分功能。
- 功能色、Hover、Selected、Disabled 由 UMG Brush Tint 表达，源图不烘焙状态色。
- 禁止从参考截图裁切、描摹或修改水印后使用。

### 命名与位置

- 共享：`T_UI_Shared_<Meaning>` → `/Game/GuLiStrike/UI/Shared/BattleHUD/Textures/Icons`
- Ship：`T_UI_Ship_<Meaning>` → `/Game/Ship/UI/Textures/Icons`
- Commander：保留现有 `T_UI_Cmd_*` 名称和路径。

### UE 导入设置

| 设置 | 目标值 |
|---|---|
| Texture Group | UI |
| Mip Gen Settings | No Mipmaps |
| Compression | UserInterface2D (RGBA) / UI |
| Max Texture Size | `128` |
| Never Stream | `true` |
| Alpha/Tint | 保留透明 Alpha；白色源图可由 Brush Tint 正确着色 |

导入后检查 24px 显示、默认/Hover/Selected/Disabled、深色/亮色底板、三画幅 DPI 和 Cook 引用。若后续需要更换 Compression，必须整套图标统一 A/B，不在同一图标组内混用。

## Commander 原位迁移方案

### 不变合同

- WBP 路径、Native Parent、HUD 宿主和创建/销毁流程不变。
- 保留 213 Widgets、四个输入阻挡岛、关键布局容器、动态文本/Fill、Button 和命令格动态命名。
- `C_Map` 保持可容纳 Native MiniMap 的 Panel；`C_SelectionTooltip` 仍为 `RootCanvas` 直接子节点；`C_UnitTypeCard` 继续拥有其运行时子节点。
- Button 继续 `IsFocusable=false`；Visibility、Hit Test、Clipping、Pivot、ZOrder 和 CanvasSlot 几何不变。
- 现有 `T_UI_Cmd_*`、Radius/Cursor cook 锚点及 C++ 资源路径不变。

### 允许修改

- 现有节点的 Brush、Tint、Font、Padding、描边、材质引用和 ButtonStyle。
- 共享主材质和 Commander 参数实例的引用；旧资产保留兼容，不在同轮删除。

### 禁止修改

- 不删除、重命名、重排或嵌套 Native/脚本依赖节点。
- 不修改数据源、委托、Timer、MiniMap 绘制、世界血条、角色切换或输入模式。
- 不用 UMG 动画改变布局岛、命中区域、Visibility 或 Clipping。
- Commander 本轮保持 0 Animation；不增加 EventGraph、属性 Binding、MVVM、Tick、循环材质或 Blueprint 循环。

## 任务清单

### A. 参考与方案

- [x] 盘点 `D:\UE5.7\UI\Ship UI` 的 60 张参考图并区分驾驶 HUD 与舰队管理范围。
- [x] 提取 Ship UI v1 关键组成、Commander 权威视觉基线、图标规则和第三方素材边界。
- [x] 只读审计 Commander HUD 的 Native/WBP/构建脚本/资产引用，冻结最低风险迁移合同。
- [x] 创建配对需求文档与开发文档。

### B. Figma 阶段 — Gate 1 已完成

- [x] 在 Starter 限制的 3 个物理页内，以 10 个顶层 Section 一一落实批准的 10 个逻辑页。
- [x] 建立 4 Collections / 77 Variables / 6 Noto Sans SC Text Styles / 83 Components / 14 Component Sets / 483 Instances 的设计系统。
- [x] 用共享基元完成 Commander HUD 主画面，并完成中文化与现状映射。
- [x] 按 420×64、300×244、320×284、840×56、88×88 和 24px 安全边距完成 Ship 主布局。
- [x] 完成 7 张中文 Ship 状态卡：真实可落地状态加一个紧凑“未接入”样本，不制作完整假状态。
- [x] 完成 1280×720、1920×1080、2560×1080 三张完整五区组件实例实屏。
- [x] 历史 Gate 1 记录曾交付 `23:2660` 玩家实机节点；2026-09-04 MCP 复核时该节点已不存在，当前以 `14:1381` 和三画幅节点为准。
- [x] 玩家成品和三张验证画幅移除“设计样例”；全文件可见普通英文完成中文化。
- [x] 设计 24×24、1.5–2px 原创共享/Ship 图标，并定义 128×128 白色透明 PNG 导出合同。
- [x] 交付 0.42s、0.8s、1.2s、0.18s 与 `Motion Scale = 0` 静态动效合同；因 Desktop Bridge 无 Metronome，Motion API 保持 Closed，无 Live Timeline。
- [x] 质量检查通过：Broken Aliases/Instances、Image Paints、Prototype Reactions、Reserved Icon Instances、Design Sample Hits 均为 0。

### C. UE 静态阶段 — Gate 2 已完成

- [x] 直接复用 NEONCTRL Gameplay/Icon/通用装饰纹理并形成硬引用；不复制 Theme/Materials/Textures，不额外导入同义图标。
- [x] 以 91 处原位 Brush/Tint 替换迁移 Commander HUD，不改节点合同和功能行为，并保持 0 Animation。
- [x] 建立 `/Game/Ship/UI/Widgets/WBP_ShipHUD` 静态 WBP，只使用占位数据，不接玩法逻辑。
- [x] 新增 `Scripts/build_neonctrl_battle_hud.py` 作为统一幂等构建入口；最终布局连续执行两轮结果稳定。
- [x] 完成三画幅、透明边缘、硬引用、WidgetService、Compile、Save 与 Cook 依赖资格审计；现有 Commander UI 自动化 9/9 通过。

### D. 完整逻辑阶段 — 未开始，代码冻结

- [ ] 保留 Commander 全部既有数据、输入、MiniMap、Tooltip 和生命周期行为。
- [ ] 为 Ship 建立只读表现快照和 Air 角色 HUD 生命周期，不向 UI 暴露可写玩法接口。
- [ ] 接入已有速度、Boost、姿态、推力/质量和部件数据，按事件或低频采样刷新。
- [ ] 雷达、锁定、方向护盾、船体生命、弹药、热量、航点和停靠不接逻辑，只保留紧凑“未接入”样本。
- [ ] 覆盖角色切换、重生、Pawn 替换、晚加入、无缝切图、输入焦点、网络与性能测试。

## 回归与验收计划

### Commander 资产契约

- WBP 路径、Native Parent、213 Widgets、关键名称/类型/层级/尺寸完全一致。
- 无 `TRASH_*`；无新增业务 EventGraph、Binding、MVVM、Tick。
- 四个阻挡岛、Tooltip、MiniMap Panel、UnitTypeCard 层级和所有 Button `IsFocusable=false`。
- Commander 动画数量保持 0；不得新增 UMG Animation、循环材质或播放逻辑。
- 共享资源硬引用、原有 Commander 图标和隐藏 cook 锚点均能回读。

### Commander 行为回归

- 运行 `GuLiStrike.Commander.UI.HUD.AssetContract` 及现有 UnitType、CommandFeedback、Shortcuts ViewportPlacement 测试。
- 运行 Commander HUD QA 与 Slate Keys QA。
- 验证 Team、Score、Time、Energy、Roster、Selection、UnitType、ACK、按钮/快捷键、Tooltip、MiniMap 点击和世界点击防穿透。
- 验证 HUD 创建、销毁和角色重进后无重复实例、重复委托或遗留 Timer。

### 视觉、动效与图标

- 使用 1280×720、1920×1080、2560×1080 和 Bottom Dock 裁切做迁移前后 A/B。
- 检查安全区、最小文字、透明边缘、24px 图标、状态色和暗背景对比度。
- Ship 动效检查 0.42s 入场、0.8s 加力、1.2s 同步、0.18s 航向标记及 `Motion Scale = 0`；静止终态必须保留状态含义。
- 检查动画不改变 Slate 命中几何，Ship UI 不拦截 LMB、滚轮、Shift、R/T 或飞行输入。

### 性能与 Cook

- 对比 Draw Element、材质实例和 500 Soldier 场景性能，避免每图标独立动态材质和每帧 UMG Binding。
- Cook/打包验证 Commander WBP、Ship WBP、共享主材质/实例、全部图标无 Missing Resource。
- 连续执行计划中的迁移/构建脚本两轮，要求 Widget 数、命名、层级、编译状态和保存包集合稳定。

## 风险与备忘

- 最大回归风险是将 Native 按名称查找的 Commander 节点移动到嵌套 WBP；本轮明确禁止。
- 现有 Commander 构建脚本会重建部分标题、单位卡和快捷栏；未来必须同步接入共享 helper，否则脚本重跑会恢复旧样式。
- UI 目录不在当前 AlwaysCook 白名单中；共享资产不能只靠字符串加载，必须形成硬引用或另立 Cook 方案。
- Ship 参考包含大量项目尚无数据源的高完成度 HUD 状态。Figma 视觉完成不等于逻辑可用，逻辑阶段必须逐项核实权威来源。
- 缺失系统只能使用紧凑“未接入”样本；不得因参考图完整而扩展出锁定、雷达、护盾、生命、弹药、热量、航点、停靠或 MFD 假状态。
- Figma Gate 1 与 UE 静态 Gate 2 已完成；完整逻辑 Gate 3 尚未开始，不能把静态占位数值解释为运行时数据接入。

## 结果链接

- Figma Gate 1：[Ship UI 设计系统](https://www.figma.com/design/2NS9XNysjlO8KLJdTilsua)（已完成）
- Figma 当前玩家主稿：[`14:1381` · 飞船 HUD · 1920×1080](https://www.figma.com/design/2NS9XNysjlO8KLJdTilsua?node-id=14-1381)；旧记录 `23:2660` 当前缺失
- UE 静态实现：`/Game/Ship/UI/Widgets/WBP_ShipHUD`、`/Game/Commander/UI/Widgets/WBP_CommanderHUD`（Gate 2 已完成）
- 构建入口：`Scripts/build_neonctrl_battle_hud.py`
- 构建/审计：`outputs/review/neonctrl-ui/build-report.json`、`outputs/review/neonctrl-ui/final-audit.json`
- 自动化：`outputs/review/neonctrl-ui/automation/CommanderUI/index.json`（9/9 Success）
- 视觉证据：`outputs/review/neonctrl-ui/WBP_ShipHUD_1920x1080.png`、`outputs/review/neonctrl-ui/runtime/WBP_CommanderHUD_1920x1080.png`
- 完整逻辑接入：未开始（代码冻结）
- 归档记录：[Ship UI v1 Figma 玩家实机成品稿](../Archive/20260903-ShipUIv1-Figma玩家实机稿.md)（Figma Gate 1 阶段归档；三阶段整体尚未完成）
- Gate 2 归档：[Ship UI v1 NEONCTRL 静态 UI](../Archive/20260904-ShipUIv1-NEONCTRL静态UI.md)
