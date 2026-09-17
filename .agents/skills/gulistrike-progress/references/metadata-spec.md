# guli-progress/v1 元数据规范

仅在创建、迁移、修复或检查 `Progress/` 文档时读取本文件。Front matter 是机器状态唯一来源；正文自由文本不得覆盖它。

## 公共字段

每篇正式文档必须包含以下字段，字段名固定：

| 字段 | 规则 |
|---|---|
| `schema` | 固定 `guli-progress/v1` |
| `id` | 全局唯一、稳定；文件改名不重编号 |
| `work_id` | 需求/开发及其子页共享；其他文档可为空字符串 |
| `kind` | `requirement / development / archive / gameplay / backlog` |
| `role` | 根文档为 `root`，拆分子页为 `detail` |
| `title` | 人读标题 |
| `areas` | 模块标识数组，如 `combat`、`wingman`、`ui` |
| `status` | 使用下方对应类型枚举 |
| `verification` | 使用统一验证枚举 |
| `created` | `YYYY-MM-DD` |
| `updated` | `YYYY-MM-DD`，发生事实变化时更新 |
| `summary` | 一至两句可脱离正文理解的摘要 |
| `next_action` | 下一项具体动作；活跃状态不可为空 |
| `relations` | ID 关系映射，不以文件名替代关系 |
| `status_note` | 迁移前状态、约束、阻塞和补充说明；允许自由文本 |

可以增加工具维护字段，但不得删除公共字段。

## 分类字段 categories

可选多值字段，取值 `art / gameplay / performance`，面向进度面板「归档 / 需求 / 开发」筛选，与 `areas`（模块粒度）互补：

- `art`：美术方向、模型资产、特效、渲染与表现；
- `gameplay`：指挥官、飞船、僚机、战斗、建筑、经济等玩法机制；
- `performance`：性能分析与优化。

规则：

- 归档、需求、开发三类文档应填写（不属于任何分类时写 `[]`）；玩法、Backlog、参考文档可省略。
- 同一 `work_id` 的需求与开发根文档保持一致；拆分子页继承父文档；归档取关联工作项与其自身 `areas` 推导结果的并集。
- 取值必须与 `areas` 语义一致：工具按 `progress_docs.py` 的 `CATEGORY_AREA_MAP`（`suggest_categories`）从 `areas` 推导，人工改动时不得与之冲突。
- `check` 校验取值合法，并对缺失分类的三类根文档给出维护提示。分类回填属于元数据维护，不更新 `updated`。

## 状态枚举

- 需求：`draft / approved / superseded / cancelled`
- 开发：`planned / in_progress / verification / done / abandoned`
- 归档：`recorded / superseded`
- 玩法：`current`
- Backlog 月文档：`current`
- 验证：`not_run / partial / passed / failed / not_applicable`
- Backlog 条目：`inbox / promoted / discarded`

状态迁移必须修改 front matter；旧中文状态和上下文保留在 `status_note`。没有证据时不得写 `passed` 或 `done`。

## ID 与关系

- 工作项：`WORK-YYYYMMDD-NNN`
- 需求：`REQ-YYYYMMDD-NNN`
- 开发：`DEV-YYYYMMDD-NNN`
- 归档：`ARC-YYYYMMDD-NNN`
- 点子：`IDEA-YYYYMM-NNN`
- 玩法：使用稳定语义 ID，如 `GAMEPLAY-COMMANDER`
- 拆分子页：父 ID 后追加 `-DNN`

同日创建新工作项时，读取目录后取尚未使用的下一个 `NNN`。需求与开发根文档共享 `work_id`，并用 `relations.requirement` / `relations.development` 双向连接。独立开发文档明确写 `relations.standalone: true`。

拆分子页沿用父级 `work_id`、`kind`、状态和验证值，设置 `role: detail`、`relations.parent: <父ID>`；父级使用有序 `relations.children`。

归档可用 `relations.work_items` 连接一个或多个工作项。勘误归档用 `relations.supersedes` 指向旧归档，并在可行时给旧归档补 `relations.superseded_by`、状态改为 `superseded`；旧正文不重写。

## 文档预算

- 活跃需求超过 20KB：提示按验收边界拆分。
- 开发超过 30KB 或 25 个任务项：提示按实施阶段拆分。
- Gameplay 超过 30KB：提示按机制分册。
- 大型冷归档默认不拆，只给维护提示。

尺寸提示不是结构错误，也不授权自动拆分。

## 工具命令

在项目根目录运行：

```powershell
python .agents/skills/gulistrike-progress/scripts/progress_docs.py migrate --dry-run
python .agents/skills/gulistrike-progress/scripts/progress_docs.py migrate --apply
python .agents/skills/gulistrike-progress/scripts/progress_docs.py build
python .agents/skills/gulistrike-progress/scripts/progress_docs.py check
python .agents/skills/gulistrike-progress/scripts/progress_docs.py snapshot --json
```

迁移只能在审阅 dry-run 后应用；重复运行必须不再产生变化。每次修改 Progress 文档后运行 `build` 和 `check`。
