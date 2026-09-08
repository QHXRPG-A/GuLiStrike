# Progress 文档模板

仅在新建正式文档或月度 Backlog 时读取。先按 `metadata-spec.md` 分配稳定 ID；模板中的字段不可省略。

## 需求根文档

```markdown
---
schema: guli-progress/v1
id: REQ-YYYYMMDD-NNN
work_id: WORK-YYYYMMDD-NNN
kind: requirement
role: root
title: <需求名称>
areas: [<area>]
status: draft
verification: not_applicable
created: YYYY-MM-DD
updated: YYYY-MM-DD
summary: <一句话需求价值与边界>
next_action: <需要确认或推进的具体动作>
relations:
  development: null
status_note: <来源、假设或旧状态；无则空字符串>
---

# <需求名称>

## 背景与目标

## 需求描述

## 边界

## 验收标准

- [ ] <可观察、可验证的条件>

## 关联
```

## 开发根文档

```markdown
---
schema: guli-progress/v1
id: DEV-YYYYMMDD-NNN
work_id: WORK-YYYYMMDD-NNN
kind: development
role: root
title: <需求名称> — 技术方案
areas: [<area>]
status: planned
verification: not_run
created: YYYY-MM-DD
updated: YYYY-MM-DD
summary: <方案结论与实施范围>
next_action: <下一项可执行任务>
relations:
  requirement: REQ-YYYYMMDD-NNN
status_note: <阻塞、假设或旧状态；无则空字符串>
---

# <需求名称> — 技术方案

## 技术选型与边界

## 涉及模块

| 变更点 | 路径/类 | 类型 |
|---|---|---|

## 任务清单

- [ ] <产出物 + 完成判据>

## 风险与备忘

## 结果链接
```

## 增量归档

```markdown
---
schema: guli-progress/v1
id: ARC-YYYYMMDD-NNN
work_id: ''
kind: archive
role: root
title: <本次开发事实标题>
areas: [<area>]
status: recorded
verification: <not_run|partial|passed|failed|not_applicable>
created: YYYY-MM-DD
updated: YYYY-MM-DD
summary: <结果、证据和边界的一句话摘要>
next_action: <遗留动作；无则空字符串>
relations:
  work_items: [WORK-YYYYMMDD-NNN]
status_note: <验证边界或历史说明>
---

# YYYY-MM-DD：<解决了什么>

## 变更清单

| 文件/资产 | 变更 |
|---|---|

## 决策与实现

## 验证

## 遗留问题
```

里程碑摘要沿用归档模板，可增加 `archive_mode: milestone`，正文只汇总结论并链接增量归档。勘误可增加 `relations.supersedes`。

## 玩法根文档

```markdown
---
schema: guli-progress/v1
id: GAMEPLAY-<MODULE>
work_id: ''
kind: gameplay
role: root
title: <模块名>
areas: [<area>]
status: current
verification: <not_run|partial|passed|failed|not_applicable>
created: YYYY-MM-DD
updated: YYYY-MM-DD
summary: <模块在游戏循环中的作用>
next_action: <下一项演进；无则空字符串>
relations: {}
status_note: <当前验证边界>
---

# <模块名>

## 模块概述

## 机制细节

## 数值速查

| 数值 | 值 | 备注 |
|---|---|---|

## 演进记录
```

## 月度 Backlog 条目

条目追加到 `Progress/Backlog/YYYY-MM.md`，不要为每个点子创建独立文件：

```markdown
## IDEA-YYYYMM-NNN <点子名称>

- 状态：inbox
- 模块：<area>
- 价值：<为什么值得讨论>
- 待确认：<进入正式需求前必须回答的问题>
- 正式需求：
```

提升后把状态改为 `promoted`，并在“正式需求”填写需求文档相对链接；不得删除原条目。
