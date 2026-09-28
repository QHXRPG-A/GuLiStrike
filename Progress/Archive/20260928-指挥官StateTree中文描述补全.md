---
schema: guli-progress/v1
id: ARC-20260928-002
work_id: ''
kind: archive
role: root
title: 三棵指挥官StateTree状态中文描述补全
areas: [commander, resources, building]
categories: [gameplay]
status: recorded
verification: partial
created: "2026-09-28"
updated: "2026-09-28"
summary: 为Miner、Builder、Mass共74个状态写入中文描述，三包编译保存回读通过，结构与兵种绑定保持。
next_action: 玩家在 /Game/Maps/LVL_CommanderMassPrototype 验收原有状态树运行效果。
relations:
  work_items: [WORK-20260926-001]
status_note: 资产描述和结构静态核对通过；本次未运行游戏或自动化测试，原玩法效果仍待玩家验证。
---

# 2026-09-28：指挥官 StateTree 中文描述

## 变更清单

| 资产／文件 | 结果 |
|---|---|
| `/Game/GuLiStrike/Commander/Behavior/ST_CommanderMiner` | 30个状态含中文描述 |
| `/Game/GuLiStrike/Commander/Behavior/ST_CommanderBuilder` | 23个状态含中文描述 |
| `/Game/GuLiStrike/Commander/Behavior/ST_CommanderMass` | 21个状态含中文描述 |
| `Scripts/annotate_commander_state_trees_zh.py` | 根据现有资产结构定位状态，只写 Description，调用既有资产编译保存命令 |
| `Artifacts/CommanderStateTree/ChineseDescriptions/descriptions.zh-CN.json` | 45个不同状态名称的中文文案，覆盖三树共74个状态 |
| `Artifacts/CommanderStateTree/ChineseDescriptions/GuLiStrike_StateTree_HierarchyV2_ZH.xmind` | 从更新后的实际资产导出独立七页审核副本 |

## 决策与实施

用户所指的是 UE StateTree 状态详情面板里的“描述”字段，因此逐个填写状态的业务职责和离开条件，保留英文状态名称、原路径、任务参数和转换结构。当前编辑器起初运行卡牌地图 PIE；只读导出时未修改资产。待该会话自行结束后操作三棵无未保存修改的目标资产，没有中断其他任务。

编辑前将三份已保存资产复制到 `Artifacts/CommanderStateTree/ChineseDescriptions/Backup/`。脚本按实际编辑器对象和递归读回路径核对全部74个状态后写入中文，再执行既有 `gs.Commander.RefreshStateTrees`。未改 C++，不需要新的原生构建；StateTree 资产本身已重新编译并保存。

## 验证与边界

- `delivery.json`：74项写入，三树分别30／23／21；无目标资产脏标记，所有状态 Description 含中文。
- `refresh.json`：三棵资产编译成功、Ready、保存成功；编译期间编辑图保持不变。
- `before.json`、`after.json`：排除预期的描述、编译哈希与保存状态后，三树完整递归结构、条件、任务、转换、Schema和策略一致。`asset-readback.json` 再次核对兵种1／2→Mass、3→Miner、4→Builder。
- `xmind-delivery.json`：独立审核图七页，74个中文描述和142条实际转换均可回读；0错误、0警告，实际 Xmind 积分消耗0。来源快照 SHA256 为 `6a7d1272e03013db9988491a6fa7d9ace31ad2960a916686f9dfb02453c4f464`。
- 未启动新的 PIE、Standalone、无头 Editor 验收或自动化测试。此次是状态说明完善，不涉及玩法规则；原有运行效果仍由玩家验收。

完整玩家操作见[技术记录](../DevelopmentDocumentation/20260926-三棵指挥官StateTree分层重构.md)，原资产迁移事实见[上一阶段归档](20260926-指挥官StateTree分层资产与审核交付.md)。
