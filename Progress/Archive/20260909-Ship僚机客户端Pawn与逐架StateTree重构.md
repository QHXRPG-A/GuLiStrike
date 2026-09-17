---
schema: guli-progress/v1
id: ARC-20260909-003
work_id: ''
kind: archive
role: root
title: Ship 僚机客户端 Pawn 与逐架 StateTree 重构实施
areas:
- wingman
- ship
- combat
- network
- ai
categories:
- gameplay
status: recorded
verification: partial
created: '2026-09-09'
updated: '2026-09-09'
summary: 完成僚机客户端Pawn、逐架UE StateTree、Actor表现池和客户端姿态转发边界；持续飞行、对地、性能、协议与构建通过，空战每成员两轮开火留作后续问题。
next_action: 后续另立空战射界循环工作项，复现并修复持续目标下部分成员长期无法完成第二轮有效开火。
relations:
  work_items:
  - WORK-20260909-002
status_note: 用户于2026-09-09决定空战问题暂不扩修，本归档如实采用partial；主机17/25、远端7/25达到两轮开火，其余本工作项门禁按下文证据通过。
---

# 2026-09-09：完成僚机客户端 Pawn 与逐架 StateTree 重构

## 变更清单

| 文件/资产 | 变更 |
|---|---|
| `Gameplay/Wingman/GuLiWingmanPawn.*` | 新增非复制、不可Possess的Owner模拟/Remote表现Pawn，挂载UE原生StateTree和专用飞行组件 |
| `Gameplay/Wingman/Movement/*` | 迁移30Hz三维飞行、FlightNav、世界障碍扫掠、滑移、稳定采样和0.5秒本地脱困；移除同类碰撞/避让 |
| `Gameplay/Wingman/Behavior/GuLiWingmanMemberBehavior.*`、`ST_WingmanMemberBehavior` | 每架独立运行六状态UE StateTree，只选择行为和移动请求 |
| `UGuLiWingmanSimulationSubsystem` | Mass Entity注册表改为Pawn注册表，Candidate、FireIntent、攻击和诊断全部读取逐架运行时 |
| `AGuLiWingmanPresentationActor` | Owner直接显示模拟Pawn，Remote改为最多175架轻量Pawn插值池，删除僚机ISM与Mass Mirror |
| `Battle/Relay`、`Battle/Contracts`、`Battle/Combat` | 协议升至v13；服务器正常姿态只校验传输身份、递增帧和存活成员后转发，继续保留选敌、开火、伤害、Roster与租约权威 |
| `Scripts/run_listen_multiprocess_smoke.ps1`、Listen诊断 | 增加25 Pawn/StateTree连续性、静止、拒绝、攻击轮、服务器零位移写入与移动Ship门禁；长测Ship走真实MovementComponent闭合航线 |

## 决策与实现

拥有客户端是本机25架僚机运动和行为的唯一模拟端。每架Pawn各自运行UE `UStateTreeComponent`，MovementComponent是Transform和Velocity的唯一写者；FlightNav、避障、环绕、归队与攻击运动都在客户端执行。服务器不复演轨迹，也不以World、FlightNav、Carrier历史、能力/编队版本、上传Grant或ACK基线中断正常姿态流。

服务器仍保留当前租约、连接代次、完整存活身份和递增Flight帧校验，并继续负责每Ship匈牙利目标分配、FireIntent授权、伤害、Roster和租约。远端客户端只插值Accepted Pose；真正断线后只有远端表现因收不到新姿态而冻结/淡出，本机Pawn在World销毁前继续本地模拟。

僚机Pawn忽略`ECC_Pawn`，移动探针显式排除其他`AGuLiWingmanPawn`，运行时不做邻机碰撞、分离或避让。存活Pawn即使收到零移动请求或遇到障碍，也会累计无进展时间；0.5秒后本地选择安全点或轮换稳定角度/高度重定位并恢复最低速度，不等待服务器重定位响应。

## 验证

- `GuLiStrike.Wingman`全量自动化完成95/95：93成功、2项既有预期警告、0失败。证据：[最终报告](../../TestResults/WORK-20260909-002/ClientOwnedContinuityFinal/index.json)。
- Relay死亡竞态定向回归23/23通过；正常帧中的服务器确认死亡成员被过滤，伪造或换代身份仍拒绝。证据：[Relay报告](../../TestResults/WORK-20260909-002/ClientAuthRelayTestsPostDeathFilter/Report/index.json)。
- UE StateTree资产审计为ready：六个任务、六个条件、根转移齐全，任务不写Transform或Velocity。证据：[资产审计](../../TestResults/WingmanPlan/WingmanStateTreeAsset/state_tree_asset_audit.json)。
- 200 Actor门禁通过：25架Owner模拟加175架Remote插值，P50 0.324ms、P95 0.4248ms、最大0.6368ms。
- 300秒双进程PIE的持续飞行子门禁全部通过：两端各25架Owner Pawn/StateTree持续存在，Relay Active，5秒静止违规0，普通/原子Candidate拒绝0，服务器Wingman Movement写入0；Ship最大位移约219.9m，最大旋转约180°。证据：[长测报告](../../TestResults/WingmanPlan/ListenMultiprocessSmoke/runs/20260909-160541-normal/index.json)。
- 源码版`GuLiStrikeEditor Win64 Development`和`GuLiStrike Win64 Development`构建退出码均为0。引擎与项目`UnrealEditor.modules` BuildId均为`9872a344-2108-49e2-b40c-34c4e2c3ccfe`。

## 遗留问题

300秒持续目标长测中，两端25架均获得合法分配并进入攻击行为，但达到至少两轮有效开火的稳定槽位只有主机17/25、远端7/25，因此长测总结果为失败。用户决定本工作项暂不处理空战射界循环；需求中涉及完整空战和每成员两轮开火的两项验收保持未勾选。对地攻击、冻结俯冲、手动覆盖和空地混合回归均已通过。

关联：[需求](../RequirementDocument/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md) · [开发案](../DevelopmentDocumentation/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md) · [玩法基线](../Gameplay/飞船.md)
