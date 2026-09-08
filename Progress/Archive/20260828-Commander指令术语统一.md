---
schema: guli-progress/v1
id: ARC-20260828-001
work_id: ''
kind: archive
role: root
title: 2026-08-28 解决了：Commander 的 Order 中文术语统一
areas:
- commander
- network
- assets
- learning
status: recorded
verification: not_run
created: '2026-08-28'
updated: '2026-08-28'
summary: 2026-08-28 解决了：Commander 的 Order 中文术语统一
next_action: ''
relations:
  work_items: []
status_note: ''
---

# 2026-08-28 解决了：Commander 的 Order 中文术语统一

- 对应开发文档：`20260827-Mass双端同步架构草案.md`、`Mass精读笔记/` 与 `Gameplay/指挥官.md`
- 变更类型：文档

## 变更清单

| 文件/资产 | 变更 |
|---|---|
| `Progress/DevelopmentDocumentation/20260827-Mass双端同步架构草案.md` | 增加术语约定，并将游戏内 Order 的中文旧称统一为“指令” |
| `Progress/DevelopmentDocumentation/Mass精读笔记/MassEntityElementTypes.md` | 明确 `Order` 为“单位指令”、`IssueMove` 场景为“移动指令”，同步正文与表格 |
| `Progress/DevelopmentDocumentation/Mass精读笔记/MassArchetypeTypes.md` | 将 Archetype、OrderFormation 与玩法事实中的中文旧称统一为“指令” |
| `Progress/DevelopmentDocumentation/Mass精读笔记/MassEntityHandle.md` | 将 SoldierId 业务身份与示例注释中的中文旧称统一为“指令” |
| `Progress/DevelopmentDocumentation/Mass精读笔记/MassEntityQuery与ExecutionContext.md` | 将非 Query 业务数据及依赖说明统一为“指令” |
| `Progress/Gameplay/指挥官.md` | 统一玩家可感知的“移动指令”“单位指令”和“指令状态”，更新最近更新摘要 |
| `Source/GuLiStrike/` | 扫描中文注释；未发现 Commander Order 的中文旧称，因此没有代码变更 |

## 做了什么

1. 固化分层术语：`Request` 为“请求”，`Command` 为“网络命令”，服务端接受并持续执行的 `Order` 为“单位指令”，当前 `IssueMove` 具体为“移动指令”，`MoveTarget` 为“移动目标”。
2. 在可演进的开发文档和玩法总册中逐处按语境更新，不修改 `OrderId`、`OrderFormation`、`ActiveOrderId`、`FGuLiMassOrderFragment` 等英文协议及源码符号。
3. 保留 `Command` 对客户端请求、RPC、ACK 和命令反馈线的网络语义，避免把请求阶段与服务端已接受指令混为一层。
4. 按归档只增不改、已确认需求只读的契约，未回写旧 `Archive/` 与已确认 `RequirementDocument/`；当前术语约定作为后续活文档和新文档的统一标准。

## 验证

- `rg` 复查 `Progress/DevelopmentDocumentation` 与 `Progress/Gameplay`：中文旧称只剩两处术语约定中的反例说明，没有业务语句继续使用。
- `rg` 扫描 `Source/**/*.h`、`Source/**/*.cpp` 中文注释：未发现 Commander Order 的中文旧称。
- `git diff --check`：所修改的已跟踪 Markdown 文件无空白错误；Git 仅提示工作区既有 LF/CRLF 转换策略。
- 本次未修改 C++、配置或资产，不需要编译或 PIE 验证。

## 遗留问题

- 历史归档和已确认需求基线仍保留旧称，这是只读契约下的有意保留；阅读时以本次术语约定为准。
