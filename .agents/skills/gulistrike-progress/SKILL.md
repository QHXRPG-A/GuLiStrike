---
name: gulistrike-progress
description: 'GuLiStrike (UE5.7, D:\UE5.7\test1) development documentation and archiving system. Use for requirements, ideas, technical plans, task lists, archives, gameplay records, and project art-standard changes or visual-approval records (需求/技术方案/归档/玩法/美术规范/美术审核); also for any file operation under D:\UE5.7\test1\Progress.'
---

# GuLiStrike Progress

维护 `D:\UE5.7\test1\Progress\` 的 Markdown 唯一事实源，覆盖“点子 → 需求 → 开发 → 实施 → 验收 → 归档/玩法沉淀”。自动索引和本地网页都只读取这些文档。

## 按需读取

- 创建、迁移、修复元数据或分配 ID：读取 [`references/metadata-spec.md`](references/metadata-spec.md)。
- 新建需求、开发、归档、玩法或 Backlog：再读取 [`references/templates.md`](references/templates.md)。
- 新增归档、里程碑、勘误或删除讨论：再读取 [`references/archive-policy.md`](references/archive-policy.md)。

不要为普通状态更新加载所有参考文件。

## 美术规范与审核维护

涉及项目美术方向、参考版本、资产例外或美术审核时，先读[《GuLiStrike 美术规范》](../../../Progress/RequirementDocument/GuLiStrike美术规范.md)和[配套变更与验收台账](../../../Progress/DevelopmentDocumentation/GuLiStrike美术规范.md)。规范是当前规则的唯一来源；模型、特效、地编技能仅链接并执行，不各自复制一份。

- 用户确认新的美术规则后，同步规范正文、`art_revision`、日期和资产例外；按现有归档策略追加增量记录，写清依据、影响范围及旧/新规则。实验结果和助手判断不能自动成为已验收标准。
- 对具体资产记录参考/源文件版本、路径或哈希、用户审核 A/B 的决定与消息依据、预览证据、UE验证和剩余偏差。审核与编译、技术验证、性能验证分别记录；没有用户决定的阶段不得填写“通过”。
- 复用已有明确过审记录，不重复索要批准。规范新增默认规则不自动撤销扫荡者无线稿等明确例外，不自动重做已导入资产；用户改变例外时记录具体资产和版本。
- 两台兵种的非三视图效果图在 `ArtSource/UI/UnitPortraits`，战争机器补充参考在 `ArtSource/ArtDirection/References`。它们是长期源资源；清理临时文件时保留原图、来源清单和必要的审核证据。
- 普通玩法、构建或文档任务不触发美术双审。美术任务需要停在审核阶段时，先完成可供审核的产物，并链接实际约束的技能和规范说明原因。

## 固定工作流

1. 未确认的点子写入当月 `Progress/Backlog/YYYY-MM.md`，编号 `IDEA-YYYYMM-NNN`；不要提前创建正式需求。
2. 用户确认后创建正式需求。需要实施方案时创建同名开发文档；文件名便于人读，稳定 `work_id` 才是权威配对关系。
3. 实施中同步任务勾选、`status`、`verification`、`status_note` 和 `next_action`。没有证据不得写“完成/通过”。
4. 完成阶段事实后写增量归档；跨多个增量时可加里程碑摘要。归档原则上不可改写，勘误通过 `supersedes` 保留历史链。
5. 玩法行为变化时更新对应 `Gameplay/模块.md`；数值集中维护，写清旧值→新值与原因。
6. 每次修改 `Progress/` 后运行索引构建和全量检查：

   ```powershell
   python .agents/skills/gulistrike-progress/scripts/progress_docs.py build
   python .agents/skills/gulistrike-progress/scripts/progress_docs.py check
   ```

7. 需求超过 20KB、开发超过 30KB 或 25 个任务项、Gameplay 超过 30KB 时，提示按边界拆分；大型冷归档默认不拆。

批量迁移或拆分前记录目标文件哈希。若实施期间文件变化，重新读取并合并，禁止盲写旧版本。`Progress/_Index/` 是生成物，不手改；`Progress/README.md` 只保留体系入口。

## 工具

主工具：`scripts/progress_docs.py`

- `migrate --dry-run/--apply`：迁移或规范化核心元数据；必须先 dry-run。
- `build [--check]`：生成或核对 `_Index/`。
- `check [--json]`：检查元数据、ID、配对、Markdown 链接、状态和尺寸。
- `snapshot --json`：生成网页与 AI 使用的只读快照。
- `split --dry-run/--apply/--check`：维护当前三篇已拆分长文档的内容哈希合同。

本地只读界面在 `Tools/ProgressDashboard/start.ps1`。它不授权新增、修改、删除或状态变更。

## 测试范围契约

- **新增测试必须先询问用户并取得明确许可。** 未经许可，不得创建测试文件、追加测试用例、扩充测试角色/Gate、扩大测试矩阵，或把当前验证延伸到相邻模块。
- 只能提议与已确认需求直接相关的测试；提议时说明对应需求、拟新增内容和涉及文件。
- 用户许可只覆盖当次明确范围。发现额外风险时先记为待确认项，不自行扩散。
- 运行当前需求已经存在的验收测试不算新增测试；修改或扩充它们仍须先询问。

## 源码版编译完成门禁

- 正式环境为源码版 UE5.7：`D:\UnrealEngine-5.7`。修改 C++、`Build.cs`、`Target.cs` 或原生插件后，收尾至少构建 `GuLiStrikeEditor Win64 Development`；涉及游戏运行、打包或用户明确要求 Game 目标时，再构建 `GuLiStrike Win64 Development`。
- 不修改引擎源码时，不要无故重编整个 `UnrealEditor` 引擎目标。
- Editor 命令固定为：

  ```powershell
  & 'D:\UnrealEngine-5.7\Engine\Build\BatchFiles\Build.bat' GuLiStrikeEditor Win64 Development '-Project=D:\UE5.7\test1\GuLiStrike.uproject' -WaitMutex -NoHotReloadFromIDE
  ```

- 需要 Game 目标时：

  ```powershell
  & 'D:\UnrealEngine-5.7\Engine\Build\BatchFiles\Build.bat' GuLiStrike Win64 Development '-Project=D:\UE5.7\test1\GuLiStrike.uproject' -WaitMutex -NoHotReloadFromIDE
  ```

- 自动化和无头 Editor 验证也必须由 `D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe` 启动。Launcher 版 `C:\Program Files\Epic Games\UE_5.7` 只能做明确要求的兼容性对照，不能作为最终完成证据。
- 构建退出码为 0 后，直接读取并比较以下清单的 `BuildId`，禁止搜索整个 `Binaries/`：
  - 引擎：`D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor.modules`
  - 项目：`D:\UE5.7\test1\Binaries\Win64\UnrealEditor.modules`
  - 本次涉及或构建过的原生插件：各自 `Binaries\Win64\UnrealEditor.modules`
- 项目和插件 `BuildId` 必须与源码版引擎一致。两套 UE 会覆盖同一项目产物，所以最后一次原生构建必须是源码版；门禁后再改原生代码或运行 Launcher 构建，必须重新执行源码版门禁。
- 归档写明实际引擎路径、构建目标、退出码和 BuildId 结果。失败或未运行写“未验证/实施中”。纯文档、分析或不影响原生模块的资产整理不触发编译门禁，除非用户明确要求。

## 项目选型基线

- C++：`Source/GuLiStrike/`；玩法内容：`/Game/GuLiStrike/`；主地图：`/Game/Maps/LVL_Main`。
- 已用能力包括 GameplayAbilities、StateTree、GameplayStateTree、Niagara、UMG/Slate、AIModule、NavigationSystem；优先复用现有栈。
- 引入尚未启用的插件或新基础设施时，在需求/开发文档单列价值、依赖、失败回退和验证成本。

## 铁律

- ID 写入后不因改名重编号；front matter 状态是唯一机器状态，自由文本进入 `status_note`。
- Markdown 内链必须有效。2026-08-30 已清理的非 Markdown 历史测试附件只提示，不算结构错误。
- 归档默认只增不删；只有完全重复且用户明确要求时才物理删除。
- 不确定信息标注来源或“假设”，验证边界必须可追溯。
