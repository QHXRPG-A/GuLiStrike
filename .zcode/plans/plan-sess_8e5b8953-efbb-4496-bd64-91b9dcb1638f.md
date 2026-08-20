# 创建 titan-progress skill：ProjectTitan 开发归档体系

## 现状依据（已探查）

- `D:\UE_5.7\ProjectTitan` 是独立 UE 5.7 项目（Epic 示例衍生：Mover 2.0、GAS、CommonUI、PCG、Water）
- `Progress\` 下四个空目录今天已建好：RequirementDocument / DevelopmentDocumentation / Archive / Gameplay——等 workflow 驱动
- 当前 test1 工作区与 ProjectTitan 无关；skill 按工作区发现，**以后在 ZCode 打开 ProjectTitan 工作区即生效**

## 交付物

### 1. `D:\UE_5.7\ProjectTitan\.agents\skills\titan-progress\SKILL.md`

**Frontmatter**：name `titan-progress`；description 同时含英文与中文触发词（requirement / dev doc / task list / gameplay record / archive / 需求 / 开发文档 / 任务清单 / 玩法记录 / 归档 / Progress），确保中英文请求都能命中。

**Body 结构**（<250 行）：

1. **目录契约**：四个目录的职责、命名规则、一一对应关系
   - 命名统一 `YYYYMMDD-名称.md`（年月日紧凑格式，如 `20260816-冲刺手感优化.md`）；Gameplay 例外：`模块名.md`（循环、战斗、技能、交友、组队联机…）
2. **四条工作流**：
   - **A 需求落档**：用户提出点子 → 先澄清（玩法/UI/技术边界、验收标准）→ 写 `RequirementDocument/YYYYMMDD-名称.md`
   - **B 技术文档**：基于对应需求文档，同名生成 `DevelopmentDocumentation/YYYYMMDD-名称.md`（技术选型、涉及类/资产、风险、**可勾选任务清单** spec 风格）
   - **C 会话归档**：每次开发收尾时写 `Archive/YYYYMMDD-解决了什么事.md`（变更文件清单、结果、验证证据、遗留问题）——append-only，不改旧档
   - **D 玩法记录**：行为变更时更新对应 `Gameplay/模块.md`（活文档，原地演进，带版本小节）
3. **四套文档模板**（内嵌，含字段：背景/目标/验收标准；技术方案/任务清单；变更/验证/遗留；模块概述/机制细节/数值）
4. **铁律**：日期取当天；需求↔开发文档严格同名配对；归档只增不改；玩法文档原地更新；文档间相对链接互引；每次归档后更新 `Progress/README.md` 索引表

### 2. `Progress\README.md`（人读索引 + 体系说明）

四目录说明、命名规则速查、最近文档索引表（skill 每次归档后追加维护）。

### 3. 示例文档一套（格式范本，日期 20260816）

- `RequirementDocument/20260816-示例-商店系统需求.md`
- `DevelopmentDocumentation/20260816-示例-商店系统需求.md`（同名列演示配对，含勾选任务清单示例）
- `Archive/20260816-示例-搭建进度文档体系.md`（把本次建 skill 这件事作为第一条真实归档）
- `Gameplay/战斗.md`（以占位结构演示模块活文档格式）

## 执行步骤

1. 写 SKILL.md（含模板与工作流）
2. 写 Progress/README.md
3. 写 4 个示例/首批文档
4. 校验 frontmatter（name 与目录名一致、description 完整）

## 不做的事

- 不动 test1 工作区任何文件；不装 MCP；不改 ProjectTitan 的 UE 工程内容
- 不做自动 git 提交（ProjectTitan 尚无仓库，是否 init 由你后续决定）