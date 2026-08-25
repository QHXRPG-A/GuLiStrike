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
| 2026-08-24 | Mass 框架启用与源码导读（大规模部队技术预研） | —（对话演进出的技术方向，未立需求文档） | [开发](./DevelopmentDocumentation/20260824-Mass框架启用与源码导读.md) |
| 2026-08-21 | 数据管线：Excel 配置飞船数值 | [需求](./RequirementDocument/20260821-数据管线Excel配置.md) | [开发](./DevelopmentDocumentation/20260821-数据管线Excel配置.md) |
| 2026-08-20 | DIY 飞船 | [需求](./RequirementDocument/20260820-DIY飞船.md) | [开发](./DevelopmentDocumentation/20260820-DIY飞船.md) |
| 2026-08-16 | 示例-商店系统 | [需求](./RequirementDocument/20260816-示例-商店系统需求.md) | [开发](./DevelopmentDocumentation/20260816-示例-商店系统需求.md) |

### 归档（倒序）

| 日期 | 事项 |
|---|---|
| 2026-08-24~25 | **[飞船 3C 与相机避障总归档：滚轮缩放（8000~50000 默认 15000）+ 舰体避障六轮演进（舰心外扫→舰外回扫→端点重叠→凸包资产化→由外向内→间隙 20m）+ 命中过滤（只认舰/地形）与两段式扫掠 + 单写者纪律；含引擎扫掠语义/Live Coding/工具链踩坑实录](./Archive/20260825-飞船3C与相机避障总归档-0824至0825.md)** |
| 2026-08-24 | **[CombatAvatarFly 归位勘误：重巡舰体从 Blender 导出入库（SM_Maelstrom_Hull），无畏舰/重巡全套分驻 CombatAvatarFly-01/02，主控舰体换为无畏舰裸舰体](./Archive/20260824-CombatAvatarFly归位勘误-重巡舰体入库与两舰归位.md)**（勘误早前误把 fly-01/02 当目标舰的记录，已并入上篇总归档） |
| 2026-08-23~24 | **[第一批飞船组件拆分入库（总归档）：A/B 两舰 14 件武器组件 + 舰体入库 ShipComponent，固化 Blender→UE 拆件流水线](./Archive/20260824-第一批飞船组件拆分入库-总归档.md)** |
| 2026-08-22~24 | **[第二轮资产整合（总归档）：VFX/阵营舰/GroundFire/Scifi_Skies 入库 /Game/Assets，222 stub 清理与迁移后遗症修复，含包迁移方法论](./Archive/20260824-第二轮资产整合-总归档.md)** |
| 2026-08-22 | [修复 LVL_Main 按 Play 无飞船可控制（加 GameMode 覆盖 + 删手摆无人船）](./Archive/20260822-修复LVL_Main按Play无飞船可控制.md) |
| 2026-08-22 | [数据管线 v2：Excel 三行元数据驱动，自动生成 C++ 行结构（删 tables.json 与手写 ShipData.h）](./Archive/20260822-数据管线v2-Excel元数据驱动自动生成行结构.md) |
| 2026-08-21~22 | [数据管线开发总归档：Excel→JSON→DataTable（MVP → 通用化 → 校验配置化）](./Archive/20260822-数据管线开发总归档-0821至0822.md) |
| 2026-08-20~21 | [DIY 飞船开发总归档（MVP → 手感调校 → 架构演进）](./Archive/20260821-DIY飞船开发总归档-0820至0821.md) |
| 2026-08-16 | [资产整合-Marketplace 资产包统一归档至 Assets](./Archive/20260816-资产整合-Marketplace资产包统一归档至Assets.md) |
| 2026-08-16 | [搭建进度文档体系](./Archive/20260816-示例-搭建进度文档体系.md) |

### 玩法模块

| 模块 | 文档 |
|---|---|
| 飞船 | [飞船](./Gameplay/飞船.md) |
| 战斗 | [战斗](./Gameplay/战斗.md) |
