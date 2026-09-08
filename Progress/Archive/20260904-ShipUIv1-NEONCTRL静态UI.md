---
schema: guli-progress/v1
id: ARC-20260904-001
work_id: ''
kind: archive
role: root
title: Ship UI v1：NEONCTRL 局内静态 UI
areas:
- commander
- ship
- ui
- data-pipeline
- assets
status: recorded
verification: partial
created: '2026-09-04'
updated: '2026-09-04'
summary: 完成了局内 HUD 的 NEONCTRL 风格落地：新建纯静态 /Game/Ship/UI/Widgets/WBPShipHUD，并在不改变原控件树、输入岛或 Native 查找合同的前提下原位换肤 /Game/Commander/UI/Widgets/WBPCommanderHUD
next_action: ''
relations:
  work_items:
  - WORK-20260902-002
status_note: ''
---

# Ship UI v1：NEONCTRL 局内静态 UI

- 对应需求：[指挥官与飞船共享 HUD 及 Ship UI v1](../RequirementDocument/20260902-指挥官与飞船共享HUD及Ship%20UI%20v1.md)
- 技术方案：[指挥官与飞船共享 HUD 及 Ship UI v1](../DevelopmentDocumentation/20260902-指挥官与飞船共享HUD及Ship%20UI%20v1.md)
- 完成范围：Figma Gate 1 之后的 UE 静态 Gate 2
- 未完成范围：Ship 真实数据、HUD 生命周期、重生/切角、航向投影和动效等 Gate 3 逻辑

## 结论

完成了局内 HUD 的 NEONCTRL 风格落地：新建纯静态 `/Game/Ship/UI/Widgets/WBP_ShipHUD`，并在不改变原控件树、输入岛或 Native 查找合同的前提下原位换肤 `/Game/Commander/UI/Widgets/WBP_CommanderHUD`。

最终成品风格只采用 `/Game/NEONCTRL_FuturisticClea_UIKit/Preview/T_Gameplay` 作为参考。Figma 负责信息层级、五区尺寸与响应式规则；NEONCTRL Gameplay/Icons/通用装饰 Texture2D 负责实际 Brush。没有把 Preview 成品大图直接贴到 HUD，也没有参考 Inventory、Pause 或 MainMenu 的成品构图。

## Figma MCP 复核

- 从 `C:\Users\a\.figma-console-mcp\plugin\manifest.json` 重新导入并启动 Figma Desktop Bridge 后，MCP WebSocket 连接恢复。
- 当前文件：`MCP Verification`，Key `2NS9XNysjlO8KLJdTilsua`。
- 当前可读节点：Ship 1920 主稿 `14:1381`、1280 画幅 `15:1698`、2560 画幅 `15:1808`、状态板 `15:1918`、Commander 主稿 `14:1116`。
- 旧文档记录的玩家节点 `23:2660` 在当前文件中返回不存在；实现没有猜测该节点内容，使用当前可读节点完成。
- Ship 采用五区合同：420×64 顶部状态、300×244 左侧遥测、320×284 右侧装配、840×56 底部快捷栏、88×88 中央航向标，安全边距 24px。

## UE 资产变更

### Ship HUD

- 新建：`/Game/Ship/UI/Widgets/WBP_ShipHUD`
- 控件数：83
- Root：`RootCanvas`
- Root 五个直接子区域：
  - `C_ShipTopStatus`
  - `C_ShipFlightTelemetry`
  - `C_ShipLoadoutStatus`
  - `C_ShipShortcutStrip`
  - `C_ShipBoresightHeading`
- 布局：顶部居中；遥测左下、装配右下；快捷栏底部居中；航向标位于视口中心。左右面板底边向上留出 108px，稳定避开 56px 快捷栏与间隔。
- 内容仅为静态占位：飞行速度、最大速度、姿态、Boost 就绪、装备状态及快捷提示；不写玩法状态，不拦截飞行输入。

### Commander HUD

- 修改：`/Game/Commander/UI/Widgets/WBP_CommanderHUD`
- 原位替换 91 个 Image Brush，并统一青蓝、冷白、警告色 Tint。
- 保持 213 Widgets，控件名称、类型、父子层级不变。
- `SB_TopStatus`、`SB_MapDesign`、`SB_DockDesign`、`SB_ShortcutsDesign` 的 CanvasSlot 几何不变。
- 5 个 Button 全部保持 `IsFocusable=false`。
- MiniMap、Tooltip、UnitType、动态命名与 Native 绑定入口不移动、不嵌套。

### 构建方式

- 新增：`Scripts/build_neonctrl_battle_hud.py`
- 通过 `Scripts/commander_editor_python.py --file Scripts/build_neonctrl_battle_hud.py` 在已打开 Editor 中执行。
- 脚本只保存 Ship/Commander 两个 WBP；不修改关卡、Source、Config 或输入设置。
- 最终布局连续执行两轮结果一致：Ship 第二轮 `created=false`，两套 Blueprint 编译状态均为 `UpToDate`。
- Commander 写入前备份：`outputs/review/neonctrl-ui/backups/WBP_CommanderHUD.before.uasset`。

## 静态合同与引用

两套 WBP 均满足：

- WidgetService hierarchy valid；
- Blueprint Compile Success；
- 0 EventGraph 节点；
- 0 Animation；
- 0 ViewModel；
- 0 MVVM Binding；
- 全部装饰控件为 Hit Test Invisible，容器为 Self Hit Test Invisible。

Asset Registry 硬引用结果：

- Ship：23 个 `/Game/NEONCTRL_FuturisticClea_UIKit` Texture2D 依赖；
- Commander：13 个 `/Game/NEONCTRL_FuturisticClea_UIKit` Texture2D 依赖。

没有复制商城资源到 `/Game/GuLiStrike/UI/Shared/BattleHUD`，也没有为静态换肤新增同义材质。硬引用可被 Cook 依赖分析发现；本轮没有把这项静态依赖审计描述为完整项目 Cook。

## 验证

### 自动化

运行现有测试过滤器 `GuLiStrike.Commander.UI`：

- 9/9 Success；
- 0 Warning；
- 0 Error；
- 包含 HUD AssetContract、UnitType、CommandFeedback、Shortcuts ViewportPlacement 与 HealthBar 策略；
- 未新增或扩充测试。

报告：`outputs/review/neonctrl-ui/automation/CommanderUI/index.json`。

### 视觉

Ship 离屏截图：

- `outputs/review/neonctrl-ui/WBP_ShipHUD_1280x720.png`
- `outputs/review/neonctrl-ui/WBP_ShipHUD_1920x1080.png`
- `outputs/review/neonctrl-ui/WBP_ShipHUD_2560x1080.png`

Commander PIE 运行时截图：

- `outputs/review/neonctrl-ui/runtime/WBP_CommanderHUD_1280x720.png`
- `outputs/review/neonctrl-ui/runtime/WBP_CommanderHUD_1920x1080.png`
- `outputs/review/neonctrl-ui/runtime/WBP_CommanderHUD_2560x1080.png`
- `outputs/review/neonctrl-ui/runtime/WBP_CommanderHUD_dock.png`

三画幅检查确认：Ship 五区不重叠、中心飞行视野保持开放、24px 边距稳定；Commander 原有顶部战况、左下地图和底部指挥坞均在运行时正确显示。

### 报告

- 构建报告：`outputs/review/neonctrl-ui/build-report.json`
- 最终资产审计：`outputs/review/neonctrl-ui/final-audit.json`
- Commander UI 自动化：`outputs/review/neonctrl-ui/automation/CommanderUI/index.json`

## 后续 Gate 3

静态 UI 完成不代表 Ship 已接入游戏。后续需要独立实现并验证：

- Air 角色 HUD 创建、销毁、重生、Pawn 替换和角色切换；
- 当前/最大速度、Boost、姿态、推力/质量与已应用部件的只读表现快照；
- Syncing、合法裸舰、已装配、加力、Pawn 丢失/重绑等状态；
- 舰体 `+X` 航向的屏内投影与离屏安全区箭头；
- Figma 规定的入场/同步/加力/航向动效及 `Motion Scale = 0`；
- 输入焦点、多人网络、生命周期、性能和完整 Cook/打包回归。

雷达、锁定、方向护盾、生命、弹药、热量、航点、停靠和完整 MFD 仍没有获准的数据源，不在本次静态交付中伪造。
