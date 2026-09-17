---
schema: guli-progress/v1
id: REQ-20260905-001
work_id: WORK-20260905-001
kind: requirement
role: root
title: 指挥官兵种技能、Ship 武器通道与 Roguelike 升级归属
areas:
- wingman
- commander
- ship
- ui
- network
categories:
- gameplay
status: draft
verification: not_applicable
created: '2026-09-05'
updated: '2026-09-07'
summary: 定义兵种技能身份、GAS 归属、多武器执行、冷却、Roguelike 升级及换装生命周期；当前仍为待确认设计基线。
next_action: 确认技能计数口径、冷却归属和奖励叠加规则后，再批准需求并推进实施任务。
relations:
  development: DEV-20260905-001
  children:
  - REQ-20260905-001-D01
  - REQ-20260905-001-D02
  - REQ-20260905-001-D03
  - REQ-20260905-001-D04
  - REQ-20260905-001-D05
status_note: 草案，待确认；不是已批准的开发任务
split_payload_sha256: 5c2e5c0f9faad8359d9df5f9fc9484a7e7ecd96e0f6b98e1495ffd1c2011e2d4
split_children: 5
---

# 指挥官兵种技能、Ship 武器通道与 Roguelike 升级归属

> 本页保留稳定入口与整体状态；原始正文已按连续章节无损迁入下列子页。外部链接继续指向本页。

## 文档导航

1. [结论、边界与实现基线](20260905-指挥官兵种技能与Roguelike升级归属/01-结论边界与实现基线.md)
2. [技能身份与 GAS 归属](20260905-指挥官兵种技能与Roguelike升级归属/02-技能身份与GAS归属.md)
3. [多武器执行与冷却](20260905-指挥官兵种技能与Roguelike升级归属/03-多武器执行与冷却.md)
4. [Roguelike 升级与生命周期](20260905-指挥官兵种技能与Roguelike升级归属/04-Roguelike升级与生命周期.md)
5. [同步、验收与关联](20260905-指挥官兵种技能与Roguelike升级归属/05-同步验收与关联.md)

## 当前摘要

- 状态：`draft`
- 验证：`not_applicable`
- 摘要：定义兵种技能身份、GAS 归属、多武器执行、冷却、Roguelike 升级及换装生命周期；当前仍为待确认设计基线。
- 下一步：确认技能计数口径、冷却归属和奖励叠加规则后，再批准需求并推进实施任务。

## 内容完整性

- 拆分正文规范化 SHA-256：`5c2e5c0f9faad8359d9df5f9fc9484a7e7ecd96e0f6b98e1495ffd1c2011e2d4`
- 使用 `progress_docs.py split --check` 可重新拼接子页并核对哈希。
