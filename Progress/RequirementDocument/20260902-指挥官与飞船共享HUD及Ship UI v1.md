# 指挥官与飞船共享 HUD 及 Ship UI v1

- 类型：UI / 技术架构
- 日期：2026-09-02
- 状态：已确认

## 背景

项目已经有一套可运行的 Commander HUD，但飞船驾驶尚缺少独立 HUD。用户提供 `D:\UE5.7\UI\Ship UI` 作为内部视觉分析参考，并确认 Ship UI v1 与 Commander UI 使用同一套视觉语言；同时，本轮要把现有 Commander UI 迁移到共享视觉基元，但不能改变既有功能行为。

本需求采用三阶段工作流：先在 Figma 完成结构与视觉，再在 UE 中完成不接逻辑的静态 UI，最后才接入完整运行时逻辑。Figma Gate 1 已完成；UE 纯视觉和完整逻辑阶段均未开始，代码与 UE 内容资产继续冻结。

参考目录共 60 张图：01–50 主要是第一人称驾驶 HUD，51–60 主要是第三人称、舰队与管理界面。索引标题仍写“50”，属于旧标题。参考素材含第三方内容或水印，只允许分析构图、信息层级和交互语汇，不允许截图切片、描摹具体素材或直接导入项目。

## 需求描述

### 1. 建立 Commander / Ship 共用视觉语言

- 以现有 Commander HUD 为权威风格基线：
  - 深黑蓝：`#050A12`、`#0B1622`
  - 中层面板蓝：`#102532`
  - 信息青：`#18D7FF`
  - 次级信息蓝：`#75A3B8`
  - 暖金属：`#D1AF86`
  - 主文字：`#EDF8FF`
  - 生命/危险红：`#E44758`
- 字体统一使用 Noto Sans SC，字号级别为 10/11/12/13/17/20。
- 间距级别为 4/8/12/16/24/32；描边级别为 1/2/3；轮廓只使用直角或 45° 斜切。
- 不使用大圆角、重阴影、烘焙辉光或与现有 Commander 不一致的高反差 3D 高光。
- 建立可供两套 HUD 复用的面板、边框、导轨、标题板、状态条、命令格、文字层级、图标状态和动效规则。
- Commander 与 Ship 共享视觉基元，但不强行共享业务数据模型或交互逻辑。

### 2. Ship UI v1 关键组成

Ship UI v1 只完整设计当前项目真实可落地的状态，不用参考图中的成熟系统制造假完成度。

| 状态/区域 | v1 完整设计内容 | 数据边界 |
|---|---|---|
| 载具同步 | 明确的 `Syncing` 状态、三段扫描反馈、暂不可用信息 | PlayerState 已是 Air，但 Pawn、装配或移动配置尚未 Ready |
| 合法裸舰 | Ready 状态、速度/姿态摘要、`未安装` 的部件区 | Revision 已发布且部件数组为空是合法状态，不是加载失败 |
| 已装配 | 当前/最大速度、总推力、总质量、已应用部件列表 | 只显示已应用 Revision，不显示半套装配 |
| 加力 | 真实 Boost 生效时的强调反馈 | 只在真实 `bBoostActive` 时循环，不以按键按下伪装确认 |
| Pawn 丢失 | 载具暂失和等待重绑状态 | 不重复创建 HUD，不沿用旧 Pawn 数据 |
| 重生/重新绑定 | 新 Pawn 到达后的恢复状态 | 清理旧源并绑定新 Pawn，不复用旧值 |
| 舰艏航向标记 | 88×88 屏内标记和离屏安全区方向箭头 | 表示舰体 `+X` 航向，不命名为锁定准星或命中点 |

1920×1080 主布局固定为：顶部状态岛 420×64、左侧飞行遥测 300×244、右侧装配状态 320×284、底部快捷提示 840×56、运行时投影的舰艏航向标记 88×88，屏幕安全边距 24px。另行验证 1280×720 和 2560×1080。

雷达、目标锁定、方向护盾、船体生命、弹药、热量、航点和停靠目前没有完整权威数据来源，统一列入排除范围，不进入玩家成品或三张验证画幅；不得制作联系人、锁定进度、生命比例、弹药数、热量值、航点距离或停靠偏差等完整假状态，也不得出现“设计样例/示例/占位/未接入/评审”等提示。Ship UI v1 不制作五类 MFD 展开态。

舰队总览、舰群编队和管理画面（如 51、58）也不纳入驾驶 HUD v1，只保留为 `99 References Excluded` 的排除参考。

### 3. 三阶段交付

#### 阶段 A：Figma（Gate 1 已完成）

- 建立共享 token、栅格、文字层级、视觉基元、图标规范和动效规范。
- 完成 Commander HUD 的共享基元映射，不改变信息结构和交互含义。
- 完成 Ship UI v1 的载具同步、合法裸舰、已装配、加力、Pawn 丢失、重生/重新绑定和航向离屏七种真实状态；无权威数据的系统从玩家画面排除。
- 受 Figma Starter 方案最多 3 个物理页限制，实际物理页为 `00 · UI 系统`、`20 · 指挥官`、`30 · 飞船`；使用 10 个中文顶层 Section 一一落实原计划逻辑页：`00 · UI 系统总览`、`01 · 基础规范`、`10 · 共享组件`、`11 · HUD 指示器`、`20 · 指挥官 · 主 HUD`、`21 · 指挥官 · 状态`、`30 · 飞船 · 主 HUD`、`31 · 飞船 · 状态`、`90 · UE 交付`、`99 · 参考资料 · 排除`。
- 输出 1920×1080 主设计稿，并检查 1280×720 与 2560×1080 的安全区和响应式规则。
- 图标和动效必须包含静态降级状态，保证不依赖动画也能辨识当前状态。

Gate 1 最终交付位于 [Figma 文件](https://www.figma.com/design/2NS9XNysjlO8KLJdTilsua)。设计系统包含 4 个 Variable Collections、77 个 Variables、6 个 Noto Sans SC Text Styles、83 个 Components、14 个 Component Sets 和 483 个 Instances。质量检查结果为 Broken Aliases 0、Broken Instances 0、Image Paints 0、Prototype Reactions 0、Reserved Icon Instances 0、Design Sample Hits 0；系统总览状态为“已完成”。

三张 Ship 实屏 1280×720、1920×1080、2560×1080 均使用完整五区组件实例，并已移除设计样例；Ship 7 张真实状态卡和 Commander 主画面均已完成中文化。全文件画布可见普通英文均已中文化，保留的拉丁文只包括项目/技术专名、快捷键、编号、令牌/属性合同和参考文件名。主要节点为 `14:1381`、`15:1698`、`15:1808`、`15:1918`、`14:1116`。

新增独立玩家成品节点 [`23:2660` · 飞船 HUD · 玩家实机视图 · 1920×1080](https://www.figma.com/design/2NS9XNysjlO8KLJdTilsua?node-id=23-2660)，位于 Section `8:14`。该节点为 1920×1080，包含五个直接 Attached 的组件实例：420×64、300×244、320×284、840×56、88×88；安全边距 24px，实例之间无重叠。画面不含设计样例、示例、占位、未接入、评审或普通英文；仅保留 `LMB`、`SHIFT`、`R`、`T` 技术快捷键。

Desktop Bridge 不具备 Metronome 能力，因此 Motion API 保持 Closed：本 Gate 没有 Live Timeline，也没有 Prototype Reactions；交付内容是 0.42s、0.8s、1.2s、0.18s 和 `Motion Scale = 0` 的静态动效合同。这是工具能力边界，不得写成已制作可播放的 Figma 动画。

#### 阶段 B：UE 静态 UI，不接逻辑（Gate 2，未开始/冻结）

- 按 Figma 建立 UMG 资产，只接占位数据，不读取玩法对象。
- WBP 不增加 EventGraph 业务逻辑、属性 Binding、MVVM 或 Tick。
- Ship UI 根节点不拦截飞船输入，不设置 UIOnly/GameAndUI，不抢键盘焦点，不显示鼠标。
- Commander 只做原位换肤；保持当前 Native Parent、资产路径、节点命名、层级、输入阻挡和运行时行为。

#### 阶段 C：完整逻辑（Gate 3，未开始/冻结）

- 只读接入真实的本地/复制数据；UI 不写玩法状态，不新增 HUD 专用 RPC。
- “完整逻辑”定义为：载具同步、合法裸舰、已装配、加力、Pawn 丢失/重生和航向离屏都有真实数据、明确更新路径和对称销毁路径。
- 可规划接入的数据包括速度、最大速度、Boost、偏航/压弯、总推力、总质量和已应用部件列表。
- 雷达、锁定、护盾、船体生命、弹药、热量、航点和停靠在 v1 不进入玩家 HUD，也不接玩法逻辑。

### 4. Commander 同步迁移合同

- 保留 `/Game/Commander/UI/Widgets/WBP_CommanderHUD` 和 Native Parent `UGuLiCommanderHUDWidget`。
- 当前 213 个 Widget 作为迁移基线；本轮不增删、不重命名、不重排 Native 或构建脚本依赖的节点。
- 保留四个世界输入阻挡岛：`SB_TopStatus`、`SB_MapDesign`、`SB_DockDesign`、`SB_ShortcutsDesign`。
- 保留 `C_Map`、`C_SelectionTooltip`、`C_UnitTypeCard`、全部动态文本/Fill、按钮和 `I_Cmd* / TXT_Cmd*` 动态命名合同。
- 所有 Commander Button 保持 `IsFocusable=false`；不改变 Visibility、Hit Test、Clipping、Pivot、ZOrder 或 CanvasSlot 几何。
- 不把 Native 通过外层 `WidgetTree` 按名称查找的节点封装进共享子 WBP。
- 现有 `T_UI_Cmd_*` 名称和 C++ 软/硬引用路径不迁移；共享改造采用原位 Brush、材质、颜色、字体和 ButtonStyle 替换。

### 5. 原创图标规范

- 允许参考 Ship UI 的信息语汇制作简单原创图标，但不得复制参考图中的具体图形、商标、水印或截图内容。
- 图标使用 24×24 设计网格和 1.5–2px 线宽，采用几何单线、无文字、无背景、无边框、无阴影、无烘焙辉光。
- UE 交付为 128×128 白色透明 RGBA PNG；语义颜色和状态颜色由 UMG Brush Tint 提供。
- 命名规则：共享图标 `T_UI_Shared_*`，飞船专用图标 `T_UI_Ship_*`；现有 `T_UI_Cmd_*` 保持原名。
- 计划导入设置：Texture Group 为 UI、UI Compression、No Mip、`MaxTextureSize=128`、保留透明 Alpha，并确认 Brush Tint 可正确着色。
- 每枚图标必须检查透明边缘、无黑/白底杂边、中心和留白一致、24px 显示可读性以及 Cook 后引用完整性。

### 6. 动效范围

- Ship HUD 入场总时长为 0.42s；各信息岛错峰淡入并平移不超过 8px，缓动为 `EASE_OUT`。
- 加力反馈为 0.8s 明度往返，只在真实加力状态生效时循环。
- 载具同步反馈为 1.2s 三段扫描，只在 `Syncing` 状态显示。
- 舰艏航向标记以 0.18s 淡入并缩放到终态；离屏后切换为安全区方向箭头。
- `Motion Scale = 0` 时所有动效直接呈现静止终态，不丢失状态含义。
- 不对快速刷新数字做闪烁或位移动画，不制作锁定扫描、虚假告警或 MFD 切换动效。
- Commander UE 资产本轮保持 **0 Animation**；共享视觉迁移只改静态样式，不添加 UMG Animation、材质循环或播放逻辑。

## 边界

- Figma Gate 1 已完成；当前只回写本次需求与开发文档，`Source/`、`Scripts/`、`Config/`、`Content/` 全部冻结。
- 当前不创建或修改 UE 资产，不运行资产构建脚本，不接玩法逻辑，不更改输入、网络、地图或 GameMode。
- 不引入 CommonUI、MVVM 或新的 UI 插件；优先沿用项目现有 UMG/Slate 栈。
- 不重命名 `AGuLiCommanderHUD`，不创建第二套 HUDClass，不改 Commander 现有运行时入口。
- 不把参考目录中的第三方图片作为项目资产、Figma 素材或图标源图。
- 不在本需求内实现舰队管理 UI。
- 不制作五类 MFD 展开态；无权威来源系统从玩家画面排除，不显示占位或“未接入”提示。

## 验收标准

### Figma 阶段（Gate 1）

- [x] 在 `00 · UI 系统`、`20 · 指挥官`、`30 · 飞船` 三个物理页内，以 10 个中文顶层 Section 一一落实原计划逻辑页。
- [x] 共享 token 与组件库覆盖 Commander/Ship 的面板、标题、信息条、状态、图标和静态动效合同。
- [x] Commander 共享基元版与当前布局、文案层级和交互含义一一对应，主画面完成中文化。
- [x] Ship UI v1 以 7 张中文状态卡覆盖七种真实状态，不包含设计样例或缺失系统占位。
- [x] 三张 Ship 实屏均为完整五区组件实例，覆盖 1280×720、1920×1080、2560×1080，并已移除设计样例。
- [x] 独立成品节点 `23:2660` 位于 Section `8:14`，为 1920×1080 五个直接 Attached 实例、24px 安全边距且无重叠。
- [x] 玩家成品无设计样例/示例/占位/未接入/评审/普通英文，仅保留 `LMB/SHIFT/R/T` 技术快捷键。
- [x] 1920×1080 布局精确采用 420×64、300×244、320×284、840×56、88×88 和 24px 安全边距。
- [x] Motion API 状态如实记录为 Closed；无 Live Timeline，仅交付 0.42s/0.8s/1.2s/0.18s 与 `Motion Scale = 0` 静态合同。
- [x] 原创图标遵守 24×24 网格、1.5–2px 线宽和 128×128 白色透明 PNG 交付，不复用参考素材。
- [x] 全文件可见普通英文完成中文化，仅保留批准的拉丁文类别。
- [x] 质量门通过：Broken Aliases/Instances、Image Paints、Prototype Reactions、Reserved Icon Instances、Design Sample Hits 均为 0。

### UE 静态阶段

- [ ] Commander 原位迁移后仍为相同 WBP 路径、Native Parent、213 Widgets 和关键层级，原有输入阻挡几何不变且保持 0 Animation。
- [ ] Commander/Ship WBP 无业务 EventGraph、Binding、MVVM 或 Tick；Ship HUD 不抢输入和焦点。
- [ ] 图标导入参数、Alpha、Brush 引用、三画幅/DPI 和 Cook 验证通过。
- [ ] Ship 动画不改变布局、命中、可见性或输入；Commander 不新增任何动画。

### 完整逻辑阶段

- [ ] Commander 数据、按钮、快捷键、MiniMap、Tooltip、输入防穿透和角色销毁行为与迁移前一致。
- [ ] Ship HUD 在 Air 角色出现，在 Ground/Observer/Unassigned 移除；重生和 Pawn 替换后能安全重绑。
- [ ] 七种真实状态均来自真实来源；雷达、锁定、护盾、船体生命、弹药、热量、航点和停靠不进入玩家 HUD。
- [ ] 输入焦点、GameOnly、鼠标状态、Enhanced Input、网络与性能预算通过回归。

## 关联

- 上游：[指挥官 UI 与小兵血条视觉设计](./20260829-指挥官UI与小兵血条视觉设计.md)
- 上游：[指挥官 HUD 逻辑接入与批量小兵血条](./20260829-指挥官HUD逻辑接入与批量小兵血条.md)
- 上游：[DIY 飞船](./20260820-DIY飞船.md)
- 下游：[开发文档](../DevelopmentDocumentation/20260902-指挥官与飞船共享HUD及Ship UI v1.md)
