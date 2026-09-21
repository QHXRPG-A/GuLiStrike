---
name: gulistrike-progress
description: 'GuLiStrike (UE5.7, D:\UE5.7\test1) development documentation, validation handoff, and archiving system. Use for requirements, ideas, technical plans, task lists, code/feature development handoffs (代码静态检查/对应 Map 测试场景交付), archives, gameplay records, and project art-standard changes or visual-approval records (需求/技术方案/归档/玩法/美术规范/美术审核); also for any file operation under D:\UE5.7\test1\Progress.'
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

**美术资产制作不套用下方的代码与功能开发交付流程。** 模型按 [guli-model-production](../guli-model-production/SKILL.md)、特效按 [ue5-vfx-production](../ue5-vfx-production/SKILL.md)、场景美术按 [ue5-scene-building](../ue5-scene-building/SKILL.md) 及项目美术规范执行；贴图、材质等视觉资产按对应制作要求执行。保留各自要求的参考/成品审核、预览、截图或渲染、视觉迭代及引擎效果验证，不能用实体数据核对代替视觉检查。本技能负责维护相关文档与审核记录。

- 用户确认新的美术规则后，同步规范正文、`art_revision`、日期和资产例外；按现有归档策略追加增量记录，写清依据、影响范围及旧/新规则。实验结果和助手判断不能自动成为已验收标准。
- 对具体资产记录参考/源文件版本、路径或哈希、用户审核 A/B 的决定与消息依据、预览证据、UE验证和剩余偏差。审核与编译、技术验证、性能验证分别记录；没有用户决定的阶段不得填写“通过”。
- 复用已有明确过审记录，不重复索要批准。规范新增默认规则不自动撤销扫荡者无线稿等明确例外，不自动重做已导入资产；用户改变例外时记录具体资产和版本。
- 两台兵种的非三视图效果图在 `ArtSource/UI/UnitPortraits`，战争机器补充参考在 `ArtSource/ArtDirection/References`。它们是长期源资源；清理临时文件时保留原图、来源清单和必要的审核证据。
- 普通玩法、构建或文档任务不触发美术双审。美术任务需要停在审核阶段时，先完成可供审核的产物，并链接实际约束的技能和规范说明原因。

## 固定工作流

1. 未确认的点子写入当月 `Progress/Backlog/YYYY-MM.md`，编号 `IDEA-YYYYMM-NNN`；不要提前创建正式需求。
2. 用户确认后创建正式需求。需要实施方案时创建同名开发文档；文件名便于人读，稳定 `work_id` 才是权威配对关系。
3. 实施中同步任务勾选、`status`、`verification`、`status_note` 和 `next_action`。代码与功能开发按下方“开发验证与场景交付”先完成代码静态检查，最后搭建对应 Map 测试场景；保存并核对相关实体数据后即可总结交付，玩家验收单独记录。美术制作按对应制作与审核流程交付。没有证据不得写“完成/通过”。
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

本地界面在 `Tools/ProgressDashboard/start.ps1`。除一处例外均为只读：总览「当前推进队列」支持右键条目直接设置开发状态（规划/实施/验收/完成/废弃），以及「记录阻塞点」弹窗——阻塞说明写入 `status_note` 且状态置为 `blocked`（阻塞为独立阶段，`blocked` 要求 `status_note` 非空）。服务端仅改写根开发文档的 `status`/`updated`/`status_note` front matter（活跃状态要求已有 `next_action`）并自动重建 `_Index`；新增、修改正文、删除仍不被授权。

## 开发验证与场景交付

**本节仅适用于代码与功能开发，以及其收尾测试场景，不适用于以视觉资产为交付目标的美术制作。** 同时包含代码逻辑与美术资产的任务分别执行对应流程并记录证据；美术制作中使用脚本、材质节点或组件，不会因此改走本节流程。

代码与功能开发默认采用 **完成功能实现与代码静态检查 → 最后在对应 Map 中搭建并保存测试场景 → 读取场景相关实体数据确认 → 总结交付**。代码检查和场景准备属于开发交付范围，直接实施，不逐次询问测试许可。测试场景搭建是此类开发的最后一步；场景保存且实体数据核对通过后，本轮助手即可总结，不等待玩家实际操作或反馈。

- 助手默认只做与当前改动直接相关、不会触发构建或运行游戏的代码静态验证：源码审查、语法/类型检查、接口与依赖核对、差异检查。编译是单独的可选动作，须按下节先询问用户；不能因修改原生代码或准备验证场景就自动编译。
- 默认不运行 PIE、Standalone、自动化测试、无头 Editor 运行验收、运行时性能测试或打包验证；已有验收测试也不自动运行。不新增或扩充测试文件、用例、测试框架、Gate 或相邻模块的测试范围。只有用户明确要求额外测试时，才执行其指定范围。
- **实现和代码静态检查完成后，最后实际搭建玩家验证场景，不能只交付验证步骤。** 使用当前需求指定或该功能已有的对应 Map，记录完整 `/Game/...` 资产路径；不要统一放进另建的通用测试图。主地图 `/Game/Maps/LVL_Main` 仅用于确实属于该地图的功能。
- 在对应 Map 中布置验证该改动所需的对象、配置、触发入口和观察位置，按需设置玩家起点、输入或 UI 入口，复用真实玩法流程并保存地图及关联资产。可以用编辑器、MCP 或 Python 制作和保存场景、核对对象与引用；这不授权助手启动游玩或自动验证运行效果。
- 场景搭建后的确认**只读取本次场景相关 entity（Actor、组件等）的数据**，按需核对所属 Map、对象类型与数量、位置/变换、关键属性、引用/绑定及触发入口，并结合保存操作结果判断场景是否按预期落地。通过编辑器、MCP 或 Python 查询，不直接读取二进制地图/资产内容。若发现布置或配置缺失，修正后再次读取相关数据；不做截图或视口视觉验证，不套用通用地编技能的截图检查循环。
- 交付说明写清：Map 路径、场景位置或 Actor 标识、进入方式、前置条件、玩家操作步骤、每步预期可观察效果，以及需要玩家反馈的结果。场景未保存、最新代码无法加载或入口未接通时，如实记录阻塞与剩余工作，不宣称场景已可玩。
- 静态检查通过、场景已保存且相关实体数据核对通过后，即可同步文档并总结交付。开发文档置 `status: verification`、`verification: partial`，`status_note` 写“代码静态检查通过，测试场景已保存并完成实体数据核对，待玩家验证效果”，`next_action` 指向对应 Map 的玩家验收；待玩家验证的状态不阻塞本轮总结，也不要求继续运行测试。后续收到玩家确认后再置 `status: done`、`verification: passed`；收到失败反馈时记录问题并继续修复。未执行或失败的检查按实际结果记录，实体数据核对只证明场景布置与配置，不代表运行效果通过。
- 纯文档、方案或确实没有玩家可观察效果的工具维护无需制作无关场景；说明不适用原因并按实际验证范围记录，不虚构玩家验收。

## 可选编译：先询问用户

本节适用于项目原生代码构建；美术制作若涉及此类构建，同样先询问用户。

- **不要求强制编译。执行编译前先询问用户是否现在编译，并等待明确同意。** 其他 agent 可能正在测试代码，用户也可能正在验证效果；说明拟构建的目标及可能干扰当前测试的原因。用户已明确要求本次编译或已同意同一范围时，沿用该授权，不重复询问。
- 未获编译许可时，不运行构建、Live Coding 或 Hot Reload，也不为构建擅自停止其他 agent 的测试、中断玩家测试、关闭或重启编辑器。不能把“加载新代码所需”或旧的编译门禁作为自动执行的理由。
- 用户暂不编译或尚未答复时，继续已授权的静态检查、文档更新和可完成的场景准备。交付写明“编译未执行，新代码运行效果待编译后验证”；不能将旧产物表现作为新代码的验证结果，也不能因未编译将已通过的静态检查记为失败。
- 获得许可后，正式构建环境为源码版 UE5.7：`D:\UnrealEngine-5.7`。涉及 C++、`Build.cs`、`Target.cs` 或原生插件时，通常提议 `GuLiStrikeEditor Win64 Development`；`GuLiStrike Win64 Development` 仅在用户要求 Game 目标或游戏/打包产物时提议。只执行获准的目标和范围，编译许可不自动授权运行时测试或打断当前测试会话。
- 不修改引擎源码时，不要无故重编整个 `UnrealEditor` 引擎目标。
- Editor 命令固定为：

  ```powershell
  & 'D:\UnrealEngine-5.7\Engine\Build\BatchFiles\Build.bat' GuLiStrikeEditor Win64 Development '-Project=D:\UE5.7\test1\GuLiStrike.uproject' -WaitMutex -NoHotReloadFromIDE
  ```

- 需要 Game 目标时：

  ```powershell
  & 'D:\UnrealEngine-5.7\Engine\Build\BatchFiles\Build.bat' GuLiStrike Win64 Development '-Project=D:\UE5.7\test1\GuLiStrike.uproject' -WaitMutex -NoHotReloadFromIDE
  ```

- 只有用户明确要求额外自动化或无头 Editor 验证时，才使用 `D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe` 执行其指定范围。Launcher 版 `C:\Program Files\Epic Games\UE_5.7` 只能做明确要求的兼容性对照，不能作为正式构建证据。
- 构建退出码为 0 后，直接读取并比较以下清单的 `BuildId`，禁止搜索整个 `Binaries/`：
  - 引擎：`D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor.modules`
  - 项目：`D:\UE5.7\test1\Binaries\Win64\UnrealEditor.modules`
  - 本次涉及或构建过的原生插件：各自 `Binaries\Win64\UnrealEditor.modules`
- 获准构建后的项目和插件 `BuildId` 必须与源码版引擎一致。两套 UE 会覆盖同一项目产物；之后再改原生代码或运行 Launcher 构建，不能声称原产物已包含最新代码或仍符合源码版基线。若需要再次构建，仍按本节确认授权，不自动重编。
- 执行过构建时，归档写明用户许可、实际引擎路径、构建目标、退出码和 BuildId 结果，并与玩家效果验收分别记录。失败或未运行时写明实际状态及新代码加载边界。纯文档、分析或不影响原生模块的资产整理不主动提议构建，除非用户明确要求。

## 项目选型基线

- C++：`Source/GuLiStrike/`；玩法内容：`/Game/GuLiStrike/`；主地图：`/Game/Maps/LVL_Main`。
- 已用能力包括 GameplayAbilities、StateTree、GameplayStateTree、Niagara、UMG/Slate、AIModule、NavigationSystem；优先复用现有栈。
- 引入尚未启用的插件或新基础设施时，在需求/开发文档单列价值、依赖、失败回退和验证成本。

## 铁律

- ID 写入后不因改名重编号；front matter 状态是唯一机器状态，自由文本进入 `status_note`。
- Markdown 内链必须有效。2026-08-30 已清理的非 Markdown 历史测试附件只提示，不算结构错误。
- 归档默认只增不删；只有完全重复且用户明确要求时才物理删除。
- 不确定信息标注来源或“假设”，验证边界必须可追溯。
