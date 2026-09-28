---
schema: guli-progress/v1
id: ARC-20260926-002
work_id: ''
kind: archive
role: root
title: 指挥官StateTree分层资产、原生编译与Xmind审核交付
areas: [commander, resources, building]
categories: [gameplay]
status: recorded
verification: partial
created: "2026-09-26"
updated: "2026-09-26"
summary: 两次已授权构建成功，三棵原路径V2树已编译保存回读，七页Xmind与原地图审核入口交付，运行效果待玩家确认。
next_action: 玩家在 /Game/Maps/LVL_CommanderMassPrototype 确认持续阶段、局部恢复、停止替换与阶段交接效果。
relations:
  work_items: [WORK-20260926-001]
status_note: 静态检查、构建、资产回读和场景实体数据核对通过；未运行PIE、Standalone、自动化测试或性能验收。
---

# 2026-09-26：分层 StateTree 资产与审核件交付

## 变更清单

| 文件／资产 | 结果 |
|---|---|
| `/Game/GuLiStrike/Commander/Behavior/ST_CommanderMiner` | 30 状态、最大深度4、10个持续叶、71条转换，共用返货和局部矿位恢复 |
| `/Game/GuLiStrike/Commander/Behavior/ST_CommanderBuilder` | 23 状态、最大深度4、8个持续叶、39条转换，施工准备与退单分组 |
| `/Game/GuLiStrike/Commander/Behavior/ST_CommanderMass` | 21 状态、最大深度3、7个持续叶、32条转换，持续推进／占领和目标恢复 |
| `Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeHierarchy.cpp` | 完整导出默认属性，避免零值、False、Any、InitialOnce 被省略 |
| `Scripts/export_commander_xmind.py` | 仅从真实资产递归回读生成层级与转换页，按页设置内容密度，使用免费分叉层级样式 |
| `/Game/Maps/LVL_CommanderMassPrototype` | 更新4个既有审核Note并增加Control，保存并读取相关实体数据 |
| `Artifacts/CommanderStateTree/HierarchyV2/GuLiStrike_StateTree_HierarchyV2.xmind` | 7页、291主题，74个状态和142条转换完整回读，0积分 |

深度以真实 UE 根状态为0。持续适配任务同时支持一次动作与持续阶段，因此任务总数分别为23、17、16，不等同于持续叶状态数。

## 授权与原生构建

- 用户先明确回复“编译”，再明确选择“关闭该无窗口进程，立即编译”。重新确认 PID 75028 无 PIE、无未保存包后正常关闭编辑器。
- 源码版引擎：`D:\UnrealEngine-5.7`；目标：`GuLiStrikeEditor Win64 Development`，参数 `-WaitMutex -NoHotReloadFromIDE`。
- 首次构建40个动作，退出码0，耗时91.83秒。随后发现只读导出省略默认参数，修正后同一范围增量构建4个动作，退出码0，耗时13.10秒。
- 引擎、项目、GuLiFlightNavigation、GuLiMapAuthoring、Tripo3DUEBridge、UnrealMCP、UnrealMCPython、VibeUE 的清单 BuildId 全部一致：`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。构建记录中有既有插件依赖／StructUtils弃用警告，构建成功。
- 启动普通源码版编辑器加载新节点；只读导出修正后再次启动并从磁盘回读三树。未运行无头 Editor 验收。

## 资产与审核证据

证据目录为 `Artifacts/CommanderStateTree/HierarchyV2/`。

- `Backup/manifest.json` 和三份原包：Miner 649537字节、Builder 215998字节、Mass 214631字节。迁移顺序 Miner → Builder → Mass，已标记V2的资产不再生成。
- `migration.json`：三资产编译保存成功，Soldiers表未变。`assets-readback.json`：三包无脏标记、编译哈希一致；Actor／Mass Schema和Persistent／InitialOnce策略保持；兵种1／2→Mass、3→Miner、4→Builder。
- `readonly-evidence.json`：检查前后三包大小和修改时间一致。常规入口读取现有EditorData，供后续UE手工编辑后重新导出。
- `build-attempt-1.log/json`、`build-attempt-2.log/json`、`build-ids.json`、`disk-reload-response.json`：构建和新代码实际加载证据。
- `xmind-delivery.json`、`xmind-content-readback.json`、`xmind-description.json`：实际层级标题与导出逐层一致，全部状态／转换GUID存在，文件校验0错误、0警告，实际Xmind积分消耗0。
- 审核件固定来源 `assets-readback.json`，SHA256：`5b0d9bf1ddd3145cae7056db7343d6455579930733ca5c20002f17e0e6c63b71`。最终布局为 `OrgChart-1 / org-chart-down`，总览、三张层级、三张转换分别成页；旧20260924审核件保留。

## 原地图审核入口

Map：`/Game/Maps/LVL_CommanderMassPrototype`。Outliner文件夹 `GuLiStrike/Review/CommanderStateTree` 中保留5个编辑器专用Note，入口 `StateTreeReview_Entry` 坐标约 `(0, 72500, -1266)`，当前已选中。玩家自行启动正常指挥官流程，建造沿用B入口。

`scene-delivery.json` 记录保存成功及保存后回读：GuLiCommanderGameMode、PlayerStart、导航、相关据点和四兵种绑定正确；128个其他Actor变换保持；资源布局仍为81领地、240矿簇、6240矿节点，哈希未变；四个卸货组件位置各异。配置对应500个Mass单位和32个工程车Actor，这是静态配置数量。`editor-delivery-state.json` 确认地图正确、无未保存包、未启动游戏。

## 验证边界与后续

- 静态差异检查和六个Python脚本语法检查通过；未新增自动化测试。
- 资产结构、编译保存、Xmind和场景数据交付完成。停止、替换移动、运输退出、暂停恢复、过期回执、矿车人工／自动失败差异、建造抢位排除、Mass接续推进的运行效果仍由玩家确认。
- 开发状态为 `verification / partial`。玩家操作及预期见[技术记录](../DevelopmentDocumentation/20260926-三棵指挥官StateTree分层重构.md)。前一阶段事实保留于[源码阶段归档](20260926-指挥官StateTree分层重构源码阶段.md)，未改写旧结论。
