---
schema: guli-progress/v1
id: DEV-20260926-001
work_id: WORK-20260926-001
kind: development
role: root
title: 三棵指挥官StateTree分层重构 — 技术方案与交付
areas: [commander, resources, building]
categories: [gameplay]
status: verification
verification: partial
created: "2026-09-26"
updated: "2026-09-28"
summary: 三棵V2树与审核地图已交付，现已为74个状态补齐中文描述并重新编译保存，运行效果待玩家验收。
next_action: 在 /Game/Maps/LVL_CommanderMassPrototype 按审核步骤确认三棵树的持续阶段、局部恢复和公共打断效果。
relations:
  requirement: REQ-20260926-001
status_note: 原生编译及资产回读通过；2026-09-28补齐三树74个状态的中文描述，三包再次编译保存且结构未变。测试场景已保存，运行效果待玩家验收。
---

# 三棵指挥官 StateTree 分层重构 — 技术记录

## 实现与模块边界

保持在现有 GuLiStrike 模块内，不新增运行时模块、网络协议、插件或玩法超时。

| 变更点 | 文件 | 职责 |
|---|---|---|
| 请求与回执 | `Source/GuLiStrike/Commander/Behavior/GuLiCommanderOperationTypes.h` | 保留旧枚举序号；版本／执行 ID／动作序号；延后、接受、失败和过期 |
| 持续任务 | `Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeNodes.h/.cpp` | Actor／Mass 适配共用 Enter、Tick、Exit；接受后只观察；退出注销未提交动作 |
| 操作提交 | `Source/GuLiStrike/Commander/Orders/GuLiUnitTaskOperations.cpp` | 按单位排队，提交前校验完整身份；预算不足保留阶段，避免重复路径／占位 |
| 生命周期 | `Source/GuLiStrike/Commander/Orders/GuLiUnitTaskSubsystem.cpp` | Observe 与 Finish 分离；终态锁存；资格消费和错误记录只进入一次；保留四轮即时处理 |
| 一次性迁移 | `Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeHierarchy.cpp` | 新层级、原路径备份、Schema 属性复制、V2 元数据、编译保存；已迁移资产仅检查 |
| 常规命令 | `Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp` | 删除旧平铺生成器；Build 命令别名只读；Refresh 仍显式编译已有图且检查结构哈希 |
| 编辑器与导图脚本 | `Scripts/inspect_commander_state_trees.py`、`migrate_commander_state_trees_v2.py`、`export_commander_xmind.py` | 从实际 EditorData 回读，核对四兵种绑定，以回读内容生成 Xmind 源稿 |

旧的一次性 BehaviorTask 类型保留，便于读取和回滚迁移前资产；V2 树使用 PersistentTask。动作枚举旧值保持，新生命周期动作追加。

## 调度、回执与收尾

1. 每个叶状态分配操作 token；请求以单位为外层键，token 包含命令 Version、ExecutionId、Serial。
2. 同一单位同轮至多保留当前操作请求。转换时 Exit 注销旧 token；新叶进入后重新绑定，Mass 遍历中只排队。
3. 主线程在遍历后再次验证三字段与当前单位执行身份。已离开的阶段／旧命令不能提交业务动作。
4. Deferred 表示未接受，继续按原节拍等待；Applied 后持续阶段改为 Observe，恢复时用已有业务 Phase 避免再次发起路径或重置累计量。
5. 工作终态保留在任务中，Poll 不释放 Active；Finish 负责一次性的资格消费、日志与正常释放。运输尚未安全退出时转入公共 SafeExit。
6. 原有地面移动 `FAIRequestID`、预算路径句柄、Mass 批次版本／Epoch 继续保护底层回调；新 token 保护树到业务桥接的提交和观察，不替换导航已有校验。
7. 保留 10Hz 与最多四轮即时处理。持续等待是正常运行状态；最后一条人工采矿命令的新循环仍在下一调度步启动。

## 状态组织与恢复

- 公共根下分为 `Control`、`SelectOrder`、`ExecuteOrder`。根上的显式转换处理控制变化（Critical）和命令身份变化（High），选择顺序仍为安全退出、停止、替换。暂停通过执行父层转换处理。
- Miner：`AcquireResource` 与 `ReturnCargo` 共用一个资源任务。矿位失败恢复、换卸货点和换工厂均有局部出口。ReturnFirst 只约束新一轮选矿入口，带部分货物的采矿暂停恢复仍可进入原采矿阶段。
- Builder：`ApproachConstructionSite` 包含选目的地、持续前往、到位抢位；持续施工是独立阶段。失败先进入 `ReturnConstructionOrder`，保留原排除记录再收尾。
- Mass：移动和占领持续运行。共享小组可能被另一成员先推进阶段，各成员通过业务父分支同步到当前移动／占领／等待阶段。无目标等待沿用原 0.5 秒观察节拍。
- 所有阶段的进入条件只用于选择；运行中的退出由显式 OnTick／完成转换处理。

## 资产主版本与工具

固定路径：

- `/Game/GuLiStrike/Commander/Behavior/ST_CommanderMiner`
- `/Game/GuLiStrike/Commander/Behavior/ST_CommanderBuilder`
- `/Game/GuLiStrike/Commander/Behavior/ST_CommanderMass`

一次性入口：在新原生节点已加载、编辑器空闲且目标资产无未保存修改时运行 `Scripts/migrate_commander_state_trees_v2.py`。顺序 Miner → Builder → Mass，原包复制到 `Artifacts/CommanderStateTree/HierarchyV2/Backup/`，元数据为 `GuLiCommanderHierarchyVersion=2`。已有 V2 不再生成；单资产编译失败恢复原编辑数据并保留原磁盘包。

日常入口：在 UE 中手工编辑、编译、保存，再运行 `Scripts/inspect_commander_state_trees.py`。`gs.Commander.InspectStateTrees` 以及原 `BuildStateTrees`／`BuildBuilderStateTree` 别名均不改 UE 资产。原两个 author 脚本也改为调用只读检查，不再导入 Soldiers 或删除其他资产。

递归导出 `Artifacts/CommanderStateTree/hierarchy-readback.json` 包含真实父子关系、节点类型和参数、条件表达式、转换 GUID／目标／优先级／触发器、启用标记、编译哈希和 Schema。Xmind 源稿只读取该 JSON，并记录 SHA256；不从 C++ 初始化模板推测实际结构。原 20260924 Xmind 保留为历史审核件。

## 任务清单

- [x] 实现 Actor／Mass 持续任务、版本回执、终态观察与一次性收尾。
- [x] 实现三树层级初始化、局部恢复及父状态打断。
- [x] 删除常规入口的平铺重建逻辑，增加递归只读检查与四兵种绑定核对。
- [x] 准备一次性迁移、Xmind 实际资产导出、原地图审核入口脚本。
- [x] 完成源码静态核对并记录结果。
- [x] 按项目规则取得本次原生编译授权，构建并加载新节点。
- [x] 备份、迁移、编译、保存并回读 Miner → Builder → Mass。
- [x] 从新资产实际回读生成 Xmind 并验证文件。
- [x] 最后更新 `/Game/Maps/LVL_CommanderMassPrototype` 审核入口，保存并核对相关实体数据。
- [x] 为 Miner、Builder、Mass 的全部74个状态填写中文描述，编译保存并核对结构未变。
- [ ] 玩家按下方步骤确认运行效果。

## 静态检查与当前证据

- 审查三树的选入和出口、控制优先级、请求身份与阶段退出失效、终态和资格消费路径；对照本机 UE 5.7 StateTree 类型与转换选择实现。
- `git diff --check` 通过，六个相关 Python 脚本的 `ast.parse` 通过。证据：`Artifacts/CommanderStateTree/HierarchyV2/static-review.json`。无新增自动化测试文件，未运行 PIE／Standalone／测试框架／性能测试。
- 用户明确回复“编译”，随后明确选择“关闭该无窗口进程，立即编译”。重新确认 PID 75028 无 PIE、无未保存资产后，经编辑器正常退出释放模块。
- 使用 `D:\UnrealEngine-5.7` 构建 `GuLiStrikeEditor Win64 Development`，首次 40 个动作完成，退出码 0。回读发现默认参数被文本导出省略，修正为完整属性导出后，同一范围增量编译 4 个动作完成，退出码 0。只读导出修正不改变已保存树结构。
- 引擎、项目和 6 个项目原生插件的 `BuildId` 均为 `dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。普通编辑器重新启动后从磁盘加载新资产，未使用无头 Editor 验收。证据：`build-attempt-1.json`、`build-attempt-2.json`、`build-ids.json`、`disk-reload-response.json`（均在下述交付目录）。
- 原包备份保留于 `Artifacts/CommanderStateTree/HierarchyV2/Backup/`：Miner 649537 字节、Builder 215998 字节、Mass 214631 字节。迁移报告 `migration.json` 确认三包编译、保存成功且 Soldiers 绑定未变。

## 资产与 Xmind 交付结果

交付目录：`Artifacts/CommanderStateTree/HierarchyV2/`。完整回读快照为 `assets-readback.json`；默认值明确包含 `Any`、`False`、`InitialOnce` 等，不以省略文本推断参数。

| 树 | 状态数 | 最大深度（根为0） | 持续适配任务数 | 持续Running叶状态数 | 转换数 |
|---|---:|---:|---:|---:|---:|
| Miner | 30 | 4 | 23 | 10 | 71 |
| Builder | 23 | 4 | 17 | 8 | 39 |
| Mass | 21 | 3 | 16 | 7 | 32 |

三包均标记 V2、无脏标记，编译哈希与编辑结构一致。Miner／Builder 为 Actor Schema、Persistent；Mass 为 Mass Schema、InitialOnce。兵种 1／2 绑定 Mass，3 绑定 Miner，4 绑定 Builder。只读检查前后三包大小和修改时间相同，见 `readonly-evidence.json`。

新版文件：`Artifacts/CommanderStateTree/HierarchyV2/GuLiStrike_StateTree_HierarchyV2.xmind`。共 7 页、291 个主题：总览、三张真实层级页、三张真实转换页。采用免费 `OrgChart-1` 分叉层级布局；持续叶标出 Running，条件、任务、目标 GUID、优先级置于对应主题备注。CLI 回读确认 74 个状态和 142 条转换的 GUID 全部存在，三张层级页逐层标题与实际导出一致。文件校验 0 错误、0 警告，实际 Xmind 积分消耗 0。证据：`xmind-delivery.json`、`xmind-description.json`、`xmind-content-readback.json`。

Xmind 使用固定回读快照，SHA256 为 `5b0d9bf1ddd3145cae7056db7343d6455579930733ca5c20002f17e0e6c63b71`。日常检查可继续更新通用回读文件，不影响本次审核件的来源追溯。

## 2026-09-28：状态中文描述

用户指出 UE 状态详情中的“描述”为空。使用当前普通编辑器读取三树实际 EditorData，逐一为 Miner 30、Builder 23、Mass 21 个状态填写业务含义、等待条件或失败去向的中文描述；此前3处英文描述也改为中文。保留原英文状态名称，方便与 C++ 操作名和既有转换目标核对。

操作脚本：`Scripts/annotate_commander_state_trees_zh.py`；文案、迁移前备份及完整回读在 `Artifacts/CommanderStateTree/ChineseDescriptions/`。执行前确认目标资产无未保存修改，并等其他地图 PIE 自行结束；没有中断该会话。脚本只改 `UStateTreeState.Description`，沿用现有 `gs.Commander.RefreshStateTrees` 对三棵原路径资产编译保存，不修改原生代码，也没有启动 PIE 或自动化测试。

`delivery.json` 和 `refresh.json` 确认 74 项已写入，三棵树都编译、保存且 Ready。`before.json` 与 `after.json` 对比显示除描述和对应编译哈希外，层级、状态 GUID、进入条件、任务、转换、Schema 与策略均相同。更新后的三包无脏标记，编译哈希与编辑内容一致；四兵种绑定仍匹配。新审核文件 `GuLiStrike_StateTree_HierarchyV2_ZH.xmind` 为独立七页副本，保留原 Xmind；CLI 核对74处中文描述和142条转换，校验0错误0警告，实际 Xmind 积分消耗0。

## 玩家审核入口与操作预期

Map：`/Game/Maps/LVL_CommanderMassPrototype`。使用现有正常指挥官／B 建造入口、资源地图和初始部队；审核 Note 放在 `GuLiStrike/Review/CommanderStateTree`，包括 Entry、MiningFactory、Construction、StrongholdAdvance 与新增 Control。脚本读取 Soldiers、经济配置、资源布局哈希、四个卸货点和相关 Actor 实体数据；不启动游戏。

已更新并保存 5 个编辑器专用 Note，当前选中 `StateTreeReview_Entry`。入口坐标约 `(0, 72500, -1266)`；在 Outliner 按名称选中后查看 Note 文本。玩家自行启动本地图正常流程，按表操作，并在对应 StateTree 编辑器选择服务端单位实例观察阶段。

保存后实体数据核对通过：`GuLiCommanderGameMode`、PlayerStart、导航及相关据点存在；128 个非本次审核对象的变换保持；资源布局为 81 个领地、240 个矿簇、6240 个矿节点，哈希 `61a8ab9f86f2f37336d20e11572540739258fa9e` 未变。配置对应 500 个 Mass 单位与 32 个工程车 Actor，共 532 个初始实例；这是配置回读数量，未启动游戏计数。矿厂 4 个卸货组件位置唯一，四兵种绑定保持。证据：`scene-delivery.json`、`editor-delivery-state.json`；最终无未保存包。

| 范围 | 玩家操作 | 预期可观察结果 |
|---|---|---|
| 公共 | 持续动作中按 S；运输中按 S | 安全退出后持续停止，不自动回到旧任务 |
| 移动替换 | 连续给不同目的地，等待新路径返回 | 新路接受前保留旧路；过期回执不能替换最新命令 |
| 暂停与队列 | 用既有运输／外部控制暂停并释放；Shift 追加 | 回到适当阶段，不重复占位／重置动作；在原业务阶段结束时交接 |
| 矿车 | 正常采卸、独立返厂、采集中断／目标失效 | 持续采卸；共用返货；带货不丢失；自动重试与人工失败按原规则 |
| 矿厂 | 通过正常建筑／阻挡变化使卸货点或工厂失效 | 先换卸货点，再换工厂；自动失败等待，人工失败报告 |
| 建造车 | 放置本地与远端工地；途中出现本地工地；抢最后施工位 | 本地优先；途中可退单换单；100cm 内抢位；失败位置排除1秒；施工开始不自动换单 |
| Mass | 观察出生推进、无目标、不可达、占领后接续，之后人工接管 | 保持移动／占领阶段；失败排除并重选；无目标限频；人工接管后 InitialOnce 不再恢复 |

本表是玩家审核范围，不是自动化测试用例。失败／过期回执等需玩家观察真实运行；实体数据检查仅证明入口与配置存在。

## 关联

- [已批准需求](../RequirementDocument/20260926-三棵指挥官StateTree分层重构.md)

- [源码阶段归档](../Archive/20260926-指挥官StateTree分层重构源码阶段.md)

- [编译、资产迁移与审核交付归档](../Archive/20260926-指挥官StateTree分层资产与审核交付.md)

- [三树中文描述补全归档](../Archive/20260928-指挥官StateTree中文描述补全.md)
