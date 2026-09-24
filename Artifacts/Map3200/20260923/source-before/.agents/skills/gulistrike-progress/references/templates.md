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
categories: [<art|gameplay|performance>]
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

代码与功能的玩家效果验收注明对应 Map 和可观察结果；静态检查或编译成功不替代效果验收。美术资产按项目美术规范和对应制作技能写参考、成品及视觉验收标准。

## 关联
```

## 开发根文档

下方静态检查、收尾测试场景及其状态说明仅用于代码与功能开发。纯美术制作保留公共元数据，将任务清单和“静态检查与场景交付”替换为对应制作、预览与审核阶段，记录源文件/参考版本、视觉证据、用户审核决定、导入及所需效果验证；不强制另建功能测试场景，也不取消截图或视觉审核。混合任务分别记录代码与美术部分。

```markdown
---
schema: guli-progress/v1
id: DEV-YYYYMMDD-NNN
work_id: WORK-YYYYMMDD-NNN
kind: development
role: root
title: <需求名称> — 技术方案
areas: [<area>]
categories: [<art|gameplay|performance>]
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
- [ ] 完成当前改动的代码静态检查并记录结果。
- [ ] 作为开发最后一步，在对应 Map 搭建并保存测试场景；读取相关实体数据确认布置和配置后，总结交付。

## 静态检查与场景交付

- 静态检查：<不触发构建或运行游戏的检查范围、方法或命令、实际结果>
- 可选编译：<如需编译，先询问用户并记录许可/待答复/暂不编译；获准后单独记录构建结果>
- 对应 Map：<完整 /Game/... 路径>
- 场景位置与入口：<位置或 Actor 标识、进入方式、前置条件>
- 场景准备：<对象、配置、触发方式、地图及关联资产保存情况>
- 场景数据确认：<读取相关 entity/Actor/组件的属性、变换、引用/绑定和入口配置所得的结果；不做截图验证>
- 玩家操作与预期效果：<按操作顺序写明步骤及各步可观察结果>
- 后续玩家反馈：<待玩家验证；不阻塞本轮总结，收到反馈后记录消息依据与实际结果>

场景搭建是开发最后一步；保存场景并读取相关实体数据确认后即可总结交付，无需截图或等待玩家反馈。静态检查和场景数据核对通过、玩家尚未确认时，使用 `status: verification`、`verification: partial`，并在 `status_note` 与 `next_action` 写清场景数据核对结果、待玩家验收及地图入口，此状态不阻塞本轮交付。新原生代码未编译时注明“编译未执行，新代码运行效果待编译后验证”，不使用旧产物作为新代码的效果证据。场景未就绪时保持 `in_progress` 并记录剩余工作；没有玩家可观察效果的任务按实际范围记录，说明场景不适用原因。

## 风险与备忘

## 结果链接
```

## 增量归档

“验证”字段按任务类型选用。下方列项是代码与功能开发示例；美术制作改记参考/成品版本、预览或截图/渲染证据、视觉检查、用户审核决定及对应引擎验证，不以实体数据核对代替美术验收。

```markdown
---
schema: guli-progress/v1
id: ARC-YYYYMMDD-NNN
work_id: ''
kind: archive
role: root
title: <本次开发事实标题>
areas: [<area>]
categories: [<art|gameplay|performance>]
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

## 资源清单（外部资源迁移时必填）

| 最终项目内文件夹 | 外部来源 | 大致内容与用途 | 依赖、示例及迁移边界 |
|---|---|---|---|
| `<Content/... 或 /Game/...>` | `<原包/原目录>` | `<按文件夹概括，不逐个枚举资产>` | `<插件/共享资源/外部 Actor/Object/未迁入项/验证范围>` |

非外部资源迁移归档删除本节。外部资源发生目录重组时，以最终项目内路径为清单主键，并保留原包名称或来源映射。

## 验证

- 代码静态检查：<范围、方法或命令、实际结果>
- 可选编译：<记录用户许可/待答复/暂不编译；如执行，记录引擎路径、目标、退出码和 BuildId；否则说明未执行或不适用及新代码加载边界>
- 玩家验证场景：<对应 Map、场景位置、入口、保存状态及操作/预期效果说明链接>
- 场景实体数据核对：<相关 entity/Actor/组件的数据与配置检查结果；无需截图证据>
- 后续玩家验收：<待验证/玩家反馈及依据；不阻塞本轮总结，尚无反馈时不得写效果通过>

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
