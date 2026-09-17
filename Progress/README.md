# GuLiStrike 进度文档体系

`Progress/` 是项目需求、开发计划、归档和玩法说明的唯一事实源。文档由
[`gulistrike-progress`](../.agents/skills/gulistrike-progress/SKILL.md) Skill 维护，人工不再维护长索引表。

## 快速入口

- [GuLiStrike 美术规范](./RequirementDocument/GuLiStrike美术规范.md)：已审参考、默认线稿/三渲二、模型双审流程、灯光和爆炸风格；由美术制作技能共同读取。
- [当前工作](./_Index/Current.md)：草案、规划、实施、验收、完成五段工作流。
- [待验收](./_Index/Verification.md)：处于验收阶段或验证未完全通过的工作。
- [模块视图](./_Index/ByArea.md)：按模块查看需求、开发、玩法与参考资料。
- [归档时间线](./_Index/Archive.md)：按时间倒序查看增量记录与里程碑摘要。
- [文档质量](./_Index/Quality.md)：元数据、链接、关系和尺寸检查结果。
- [AI 目录](./_Index/catalog.jsonl)：不含全文的精确检索目录。
- [本月 Backlog](./Backlog/2026-09.md)：尚未确认、未进入正式需求的点子。

本地只读战情面板位于 [`Tools/ProgressDashboard`](../Tools/ProgressDashboard/README.md)，运行其
`start.ps1` 后访问 `http://127.0.0.1:4317`。

## 文档职责

| 目录 | 机器类型 | 用途 |
|---|---|---|
| `RequirementDocument/` | `requirement` | 已确认需求、边界与验收口径 |
| `DevelopmentDocumentation/` | `development` | 同工作项的技术方案与任务清单 |
| `Archive/` | `archive` | 不回写历史的增量记录与里程碑摘要 |
| `Gameplay/` | `gameplay` | 当前玩法模块总册 |
| `Backlog/` | `backlog` | 尚待确认的月度点子 |

所有核心文档使用 `guli-progress/v1` front matter。需求与开发文档以稳定 `work_id` 配对；文件名仅用于人读，重命名不得改变既有 ID。嵌套教材与参考资料保持原貌，由目录工具作为 `reference` 收录。

## 固定流程

1. 未确认点子写入当月 Backlog，使用 `IDEA-YYYYMM-NNN`。
2. 点子确认后保留原条目并链接新需求；需求与开发文档同名创建、共享 `work_id`。
3. 实施时更新开发任务、机器状态、验证结论和 `next_action`。
4. 每次文档修改后重建索引并运行检查。
5. 完成工作写增量归档；跨阶段节点可写里程碑摘要，但只链接增量事实，不复制全文。

```powershell
python .agents/skills/gulistrike-progress/scripts/progress_docs.py build
python .agents/skills/gulistrike-progress/scripts/progress_docs.py check
```

需求超过 20KB、开发超过 30KB 或 25 个任务项、玩法总册超过 30KB 时，检查器会提示拆分。提示不代表自动删除或改写历史。

## 归档与勘误

增量归档创建后原则上不可改写。勘误新增一篇归档并通过 `relations.supersedes` 保留历史链；只有文件完全重复且用户明确要求时才允许物理删除。

2026-08-30 已清理历史测试日志、检查结果、性能采样、自动化报告和运行截图。旧归档中缺失的非 Markdown 附件仅作为历史提示，不是结构坏链；正式文档与 `TerrainGeneration/` 地形资料未删除。原清理记录文件已随历史整理移除，此处保留事实说明，不再建立失效链接。

## 维护边界

- `_Index/` 全部自动生成，不手改。
- Front matter 中的枚举状态是唯一机器状态；旧自由文本完整保留在 `status_note`。
- 链接使用相对路径；Markdown 内链必须存在。
- 网页面板只读、仅绑定 `127.0.0.1`，不得通过它修改文档。
