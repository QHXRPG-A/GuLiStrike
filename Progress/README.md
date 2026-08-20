# ProjectTitan 进度文档体系

> 由 `gulistrike-progress` skill 驱动（`.agents/skills/gulistrike-progress/SKILL.md`）。本文件是体系入口与人读索引。

## 四类文档

| 目录 | 职责 | 命名规则 |
|---|---|---|
| [RequirementDocument](./RequirementDocument/) | 需求点子（gameplay/UI/技术架构） | `YYYYMMDD-名称.md` |
| [DevelopmentDocumentation](./DevelopmentDocumentation/) | 技术方案与任务清单，与需求文档同名配对 | 同需求文档 |
| [Archive](./Archive/) | 每次开发的变更归档（只增不改） | `YYYYMMDD-解决了什么事.md` |
| [Gameplay](./Gameplay/) | 玩法模块总册（活文档） | `模块名.md` |

**流程**：点子 → 需求文档（A）→ 技术文档+任务清单（B）→ 实施（勾选任务）→ 归档（C）→ 玩法册更新（D）→ 本索引刷新。

## 文档索引

### 需求 → 开发（成对）

| 日期 | 名称 | 需求 | 开发 |
|---|---|---|---|
| 2026-08-20 | DIY 飞船 | [需求](./RequirementDocument/20260820-DIY飞船.md) | [开发](./DevelopmentDocumentation/20260820-DIY飞船.md) |
| 2026-08-16 | 示例-商店系统 | [需求](./RequirementDocument/20260816-示例-商店系统需求.md) | [开发](./DevelopmentDocumentation/20260816-示例-商店系统需求.md) |

### 归档（倒序）

| 日期 | 事项 |
|---|---|
| 2026-08-20 | [DIY 飞船 MVP（模块化装配+6DOF+热切换框架）](./Archive/20260820-DIY飞船MVP.md) |
| 2026-08-16 | [资产整合-Marketplace 资产包统一归档至 Assets](./Archive/20260816-资产整合-Marketplace资产包统一归档至Assets.md) |
| 2026-08-16 | [搭建进度文档体系](./Archive/20260816-示例-搭建进度文档体系.md) |

### 玩法模块

| 模块 | 文档 |
|---|---|
| 飞船 | [飞船](./Gameplay/飞船.md) |
| 战斗 | [战斗](./Gameplay/战斗.md) |
