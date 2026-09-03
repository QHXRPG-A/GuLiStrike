---
name: gulistrike-progress
description: 'GuLiStrike (UE5.7, D:\UE5.7\test1) development documentation and archiving system. Use whenever working on the GuLiStrike project and the user mentions requirements, new ideas, gameplay/UI/tech proposals (需求/点子/想法), technical design or task lists (开发文档/技术方案/任务清单), session summaries or change records (归档/总结/变更记录), or gameplay module documentation (玩法记录/战斗/循环/技能/刷怪) — also trigger for any file operation under D:\UE5.7\test1\Progress. English triggers: requirement doc, dev doc, spec, task breakdown, archive, changelog, gameplay record.'
---

# GuLiStrike Progress — 开发归档体系

管理 `D:\UE_5.7\test1\Progress\` 下的四类文档，覆盖"点子 → 方案 → 实施 → 沉淀"全流程。

## 目录契约

| 目录 | 职责 | 命名规则 | 文档性质 |
|---|---|---|---|
| `RequirementDocument/` | 游戏开发新点子：gameplay、UI、技术架构 | `YYYYMMDD-名称.md` | 定稿后只读 |
| `DevelopmentDocumentation/` | 技术手册：选型、拟用技术、任务清单（spec） | 与对应需求文档**同文件名** | 随实施勾选更新 |
| `Archive/` | 每次开发的变更记录：细节与结果 | `YYYYMMDD-解决了什么事.md` | 默认只增不改；允许合并，合并后删除被替代的旧归档 |
| `Gameplay/` | 游戏玩法总册，按模块组织 | `模块名.md`（循环、战斗、技能、刷怪、拾取…） | 活文档，原地演进 |

- 日期一律取**当天**（紧凑格式，如 `20260816`），名称用中文短词（`20260816-冲刺手感优化.md`）。
- 需求 ↔ 开发文档严格同名配对；引用时用相对链接 `[名称](../RequirementDocument/同名.md)`。
- 名称不足以区分类别时加前缀（如 `20260816-资产整合-….md`），参考 Archive 现有条目。

## 工作流

### A. 需求落档（用户提出点子时）

1. 先澄清再落笔：类型（gameplay/UI/技术架构）、动机、边界、验收标准。能问就问，不能问就按最小合理假设并标注"假设"。
2. 写入 `RequirementDocument/YYYYMMDD-名称.md`（模板 A）。
3. 回复用户文档路径，询问是否立即生成技术文档（工作流 B）。

### B. 技术文档生成（基于需求文档）

1. 读对应需求文档；不存在同名文件则先确认对应关系。
2. 评估技术选型时优先项目已有栈（见下方"项目技术栈速查"）——避免引入项目未启用的新插件，确需引入要在文档里单列风险。
3. 写入 `DevelopmentDocumentation/同文件名.md`（模板 B），任务清单必须可直接执行、可勾选。
4. 实施过程中完成一项勾一项（`[x]`），发现新工作就追加条目。

### C. 会话归档（每次开发收尾时）

1. 收集本次事实：改了哪些文件（准确路径）、解决了什么、如何验证（PIE/编译/测试证据）、遗留问题。
2. 默认写入新的 `Archive/YYYYMMDD-解决了什么事.md`（模板 C），不修改旧归档。用户明确要求合并时，创建覆盖全部原始事实与引用的新合并归档；确认新归档完整、相关引用已迁移后，删除被替代的旧归档。
3. 同步更新两处：`Progress/README.md` 归档索引表**顶部**加一行（倒序）；发生归档合并时，同时移除被删除旧归档的索引行并搜索、修正所有旧路径引用；若玩法行为有变，走工作流 D。
4. 涉及代码/资产的具体细节写全（类名、函数、资产路径、参数、资产计数基线）——归档的价值在于半年后还能据此复现。合并归档必须保留被替代旧归档中的复现信息与验证结论，不能只保留摘要。参考现有归档 `20260816-资产整合-Marketplace资产包统一归档至Assets.md` 的粒度。

### D. 玩法记录（玩法行为变更时）

1. 定位模块文件（不存在则创建）：战斗→`战斗.md`、核心循环→`循环.md`、刷怪→`刷怪.md` 等。
2. 在"机制细节"追加/修订对应小节，并更新文档头部"最近更新"行。
3. 数值（伤害、冷却、概率）集中在"数值速查"表维护，改动必须写明旧值→新值。

## 项目技术栈速查

写技术文档选型时优先对照，避免引入未启用插件：

- **C++ 模块**：`Source/GuLiStrike/`（单模块，子目录 AI/ Gameplay/ UI/）
- **已启用插件**：GameplayAbilities、StateTree、GameplayStateTree、ModelingToolsEditorMode、UnrealMCP（编辑器自动化）
- **Build.cs 依赖**：EnhancedInput、StateTree/GameplayStateTree、Niagara、UMG/Slate、AIModule、NavigationSystem
- **未启用**（引入需单列风险）：Mover、CommonUI、PCG、GameplayFeatures(ECS)、网络复制相关
- **内容路径约定**：玩法内容 `/Game/GuLiStrike/`，主地图 `/Game/Maps/LVL_Main`，外部资产包 `/Game/Assets/`（Environments/Props/SampleMaps 三类，管理规范见 Archive 的资产整合归档）

## 铁律

- 归档默认只增不改；用户明确要求合并时，允许新增合并归档，并在内容完整性与引用迁移确认后删除被替代旧归档。玩法文档只改不增（原地演进）；索引每次归档或合并后更新。
- 没有验证证据的结论写"未验证"，不写"完成"。
- 模板字段可增不可减；不确定的信息标注来源或"假设"。

## 模板

### 模板 A：需求文档

```markdown
# <需求名称>

- 类型：gameplay | UI | 技术架构
- 日期：YYYY-MM-DD
- 状态：草案 | 已确认 | 已归档

## 背景
为什么想要这个？（玩家体验/开发痛点/技术动机）

## 需求描述
具体要什么。分条列出，每条可独立验收。

## 边界
明确不做什么，防止范围蔓延。

## 验收标准
- [ ] 可观察、可测试的通过条件

## 关联
- 上游：<相关旧需求/讨论，无则写"无">
- 下游：[开发文档](../DevelopmentDocumentation/同文件名.md)
```

### 模板 B：开发文档（spec）

```markdown
# <需求名称> — 技术方案

- 对应需求：[<需求名称>](../RequirementDocument/同文件名.md)
- 日期：YYYY-MM-DD
- 状态：设计中 | 实施中 | 已完成

## 技术选型
候选方案对比与结论（含"为什么不用 X"）。优先项目已有栈（见 SKILL 的技术栈速查）。

## 涉及模块
| 变更点 | 路径/类 | 类型 |
|---|---|---|
| 例：冲刺技能 | Source/GuLiStrike/Gameplay/… | C++ |
| 例：UI_商店 | Content/GuLiStrike/UI/… | 蓝图 |

## 任务清单
- [ ] 任务1（产出物 + 完成判据）
- [ ] 任务2
  - [ ] 子任务

## 风险与备忘
- 风险、依赖、待验证假设

## 结果链接
完成后填：[归档记录](../Archive/YYYYMMDD-….md)
```

### 模板 C：归档文档

```markdown
# YYYY-MM-DD 解决了：<一句话事情>

- 对应开发文档：<文件名或"无（独立修复/资产整理等）">
- 变更类型：C++ | 蓝图 | 内容资产 | 文档

## 变更清单
| 文件/资产 | 变更 |
|---|---|
| `Source/GuLiStrike/…/X.cpp` | 新增 DoY()，修改 Z() 的… |

## 做了什么
按时间线叙述关键步骤与决策原因。

## 验证
- <证据：编译通过/PIE 行为/日志摘录/资产计数>（未验证则明写）

## 遗留问题
- <已知未解决项 + 影响 + 建议后续>
```

### 模板 D：玩法模块文档

```markdown
# <模块名>（如：战斗）

- 最近更新：YYYY-MM-DD（<一句话摘要>）

## 模块概述
这个模块在游戏循环中的位置与玩家可感知的目标。

## 机制细节
### <机制1（如：普通攻击）>
- 触发：<输入/条件>
- 流程：<发生什么>
- 反馈：<表现/音效/数值>

## 数值速查
| 数值 | 值 | 备注 |
|---|---|---|

## 演进记录
- YYYY-MM-DD：<机制/数值 变更，旧值→新值及原因>
```

## 首次使用检查

若 `Progress/` 目录缺失，按上表重建四个子目录与 README 索引。文档编号冲突（同日同名）时在名称后加序号（`20260816-商店系统-2.md`）。
