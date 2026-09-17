---
schema: guli-progress/v1
id: DEV-20260905-001
work_id: WORK-20260905-001
kind: development
role: root
title: 指挥官兵种技能、Ship 武器通道与 Roguelike 升级归属 — 技术方案
areas:
- wingman
- commander
- ship
- ui
- network
categories:
- gameplay
status: in_progress
verification: partial
created: '2026-09-05'
updated: '2026-09-05'
summary: 整体方案使用现有 GuLiStrike Runtime 模块、GAS、服务器 Mass 战斗、Wingman Relay 和 WeaponPart，不新增插件或改变蓝图类路径。目标是由共享层提供稳定通道身份、配装/奖励事务、纯参数解析；各执行域消费其最终配置，保留现有伤害和网络权威。陆军多槽和
  Ship/僚机通道执行适配已经写入；完整地空 Roguelike 奖励事务、空中换装请求/UI 和…
next_action: P1：稳定绑定、可扩展通道定义、兼容校验、启用/移除与完整 Loadout 版本；代码已覆盖 Army/Wingman，仍待 P3 协议定向验证
relations:
  requirement: REQ-20260905-001
status_note: 实施中；第一批（共用身份基础＋陆军）定向验证已通过，P3 Ship/僚机通道代码已接入并到达测试确认门，P4/P5 尚未完成
---

# 指挥官兵种技能、Ship 武器通道与 Roguelike 升级归属 — 技术方案

- 对应需求：[v0.3 草案](../RequirementDocument/20260905-指挥官兵种技能与Roguelike升级归属.md)
- 本轮授权：实施草案；第一批 T1–T3 已经用户确认并执行，P3 测试尚未授权；不运行无关测试

## 技术选型

整体方案使用现有 GuLiStrike Runtime 模块、GAS、服务器 Mass 战斗、Wingman Relay 和 WeaponPart，不新增插件或改变蓝图类路径。目标是由共享层提供稳定通道身份、配装/奖励事务、纯参数解析；各执行域消费其最终配置，保留现有伤害和网络权威。陆军多槽和 Ship/僚机通道执行适配已经写入；完整地空 Roguelike 奖励事务、空中换装请求/UI 和 Ship 本体 WeaponPart 适配尚未交付，不能把后续阶段当作当前能力。

本轮采用草案推荐值：陆军成长属于 Team；空中成长属于本队玩家配装；同局 Ship 重生恢复配装/成长但清理生命期冷却；同生命换装不清冷却；兵种编成变化先作用于后续生成/补充，不把现存单位隐式改型。新攻击行为通过注册接口扩展，不宣称已支持激光、燃烧、分裂。

依赖方向：静态配置/共享通道类型与解析 → Army、Ship、Wingman 适配 → 各自战斗执行与只读 UI。共享层不依赖具体 Pawn、Mass Fragment、导航或 UI。既有 Army 解析公式保持兼容。

## 涉及模块

| 变更点 | 路径/类 | 类型/职责 |
|---|---|---|
| 共享通道合同 | `Source/GuLiStrike/Gameplay/Skills/` | C++，ID、最终配置、修饰/配装状态与事务 |
| 陆军解析/复制 | `GuLiArmySkillSubsystem`、`GuLiSkillResolver`、`GuLiArmySkillReplicationActor` | 服务器 Team 权威，固定步提交，客户端只读 |
| 陆军多槽执行 | `Commander/Mass/GuLiBattleAuthoritySubsystem`、`GuLiSoldierCombat` | 一实体位置/生命，多通道执行状态 |
| 空中配装与 GAS | `BattlePlayerState`、`Gameplay/Ship/Abilities/` | 玩家本局成长、Ship 生命周期入口、按绑定授权/输入 |
| 僚机执行/协议 | `Gameplay/Wingman/`、`Battle/Contracts`、Relay、Combat | 成员/通道身份、版本投影、服务器校验与冷却 |
| 本体武器 | `GuLiStrikeShip`、`GuLiStrikeWeaponPart` | 通道供参，组件开火，挂点冷却迁移 |
| 技能视图/换装入口 | 对应 PlayerState/Controller、Army/Ship HUD 视图 | 客户端提交候选 ID，服务器校验并返回版本 |

## API 与生命周期合同

- 配装输入只含稳定所有者/类型/槽、候选 ID、请求 ID 与预期版本。正式奖励由服务器登记可领取选项；客户端不能提交任意倍率。
- Profile 和通道清单由服务器生成；数值、启用成员和版本一致提交，失败保持旧状态。同一请求重发返回原结果。
- 升级账本不依赖 SpecHandle、Pawn、武器 Component 或模拟租约；成员目标、单发冷却和当前激活只存在于运行层。
- Blueprint 暴露只读通道视图和请求接口；原生注册/事务准备函数保持服务器调用。新通道/武器类型必须在当前构建已注册。
- 没有跨局 SaveGame；跨新会话继承不在本次范围。Team/玩家局内记录按 MatchEpoch 清理；复制仅发布接收方需要的视图与执行参数。

## 任务清单

- [x] P0：建立同名开发文档，记录本轮默认值和测试暂停点。
- [ ] P1：稳定绑定、可扩展通道定义、兼容校验、启用/移除与完整 Loadout 版本；代码已覆盖 Army/Wingman，仍待 P3 协议定向验证。
  - [x] 第一批代码接入：共用 Domain/Binding/View；Army 候选兼容、初始锁定/来源解锁、显式装备选择和完整快照版本。
  - [x] 空中静态配置接入：AbilitySet 目录支持一个编队与 0–8 个武器绑定，同类 GA 可复用；完整快照带 Loadout/Profile 版本。
- [ ] P2：陆军每兵多槽执行，共用唯一目标实体网格，单槽升级/换装与冷却迁移；整阶段未验收。
  - [x] 第一批代码接入：共享只读 Profile、每兵逐槽状态、实体/攻击通道分离、逐槽诊断接口。
  - [x] 获准的解析、换装事务、多槽战斗和新局清理定向测试通过。
  - [ ] 最小内容切片；当前没有编辑源表或创建第二武器槽资产。
- [ ] P3：僚机多通道配置、按绑定 GAS/输入、成员逐槽执行、可靠协议与恢复。
  - [x] 生产代码接入并通过源码构建：协议 v9、完整通道集合、精确 GAS 路由、逐成员逐槽自动射击、显式齐射冷却组、换装冷却迁移和来源快照。
  - [ ] P3 定向自动化尚未获准，停在测试确认门；未创建或修改 P3 测试。
- [ ] P4：地空成长记录、合法奖励与动态换装事务、只读视图和请求入口。
  - [x] 前置代码接入：Army 局内换装 RPC、只读通道视图、版本/权限校验及有界重复请求缓存。
  - [ ] 正式奖励登记/领取、空中成长持久所有者、编成切换和实际 HUD 控件尚未实施。
- [ ] P5：Ship 本体 WeaponPart 参数/挂点冷却适配与弹丸来源快照。
- [x] 源码版 `GuLiStrikeEditor Win64 Development` 完整构建与 BuildId 核对通过，见下文。
- [x] 第一批 T1–T3 经用户确认后实施并执行，4 个精确自动化条目通过。
- [ ] P3 测试交接：下文已列具体用例、文件与边界，暂停等待用户确认。

子项勾选仅表示代码/文档已写入，并不表示功能验证通过。按草案 11.1 的分阶段验收安排，先交接第一批，再进入 P3 和后续阶段；本次不是整份草案完成交付。

## 第一批已接入代码与使用合同

### 身份、配置与解析

- 新增 `Gameplay/Skills/GuLiWeaponChannelTypes.h/.cpp`：`FGuLiWeaponBindingKey` 由 MatchEpoch、Team、OwnerPlayerGuid、Domain、SubjectId、SlotId 构成。Army 的 OwnerPlayerGuid 为空，SubjectId 为 UnitTypeId 的规范十进制名称；Wingman 已使用服务器玩家 Guid、WingmanTypeId 和 WeaponSlotId 生成同一结构的稳定绑定。ShipMounted 仍只保留域定义，待 P5 适配。
- `GuLiSkillTypes` / `GuLiSkillResolver`：新增初始解锁、最终解锁/装备状态、来源解锁记录和显式配装选择。空 SkillId 表示卸下，槽位身份和来源保留；重新装备后按最终 SkillId 重算限定词条。来源中的强制替换优先于手动选择，不允许请求成功却装备成另一个武器。
- 继续使用现有 `Skills` / `UnitSkills` 数据管线定义技能、兵种/槽位候选和参数。同一兵种/槽位须恰有一个默认候选，新增已有行为的候选不需要按新 ID 增加代码分支。
- `GuLiCommanderDataSettings` / `GuLiCommanderDataSubsystem`：`MaximumWeaponSlotsPerUnit` 默认 8，可配置 1–32；解析器硬上限 32。`WeaponSlotRules` 按 UnitTypeId/SlotId 覆盖初始解锁状态；未知或重复规则拒绝加载。本批不修改 Excel、DataTable 资产或生成表头。
- 可在 `[/Script/GuLiStrike.GuLiCommanderDataSettings]` 下配置 `+WeaponSlotRules=(UnitTypeId=2,SlotId="SecondaryWeapon",bInitiallyUnlocked=False)`；此示例**未写入配置文件**，且前提是 UnitSkills 已有该兵种/槽位的合法候选。解锁来源通过已有服务器 `ExecuteArmySkillCommand` 路径提交，不等于已经交付正式肉鸽选卡入口。

### 权威提交与战斗

- `GuLiArmySkillSubsystem` 持有 Team 配装和来源，按固定步提交最终 Profile；`LoadoutRevision` 描述完整已提交快照，不再使用最大单槽 Revision 判断整个配装变化。`GuLiArmySkillReplicationActor` 同步该版本及逐槽解锁/装备状态。
- `GuLiBattleAuthoritySubsystem` 按 Team/UnitTypeId 共享不可变 Profile 数组；每兵只增加逐槽目标/冷却状态，不新增逐兵 ASC。位置、生命和目标网格保持每兵一项，攻击通道单独展开。
- `GuLiSoldierCombat::CollectChannelAttacks` 从独立攻击通道消费配置；原单槽 `CollectAttacks` 接口保留。停用槽不攻击；替换或重新装备清理该槽目标并等待完整新间隔，同时保留更长的旧冷却。仅数值变化沿用原有冷却比例迁移规则；其他槽不重置。
- 单次伤害事件附带来源 UnitTypeId、SlotId 和 ProfileRevision。`TryGetSoldierWeaponDebug(Id, SlotId, Out)` 可按槽读取诊断状态，原 `TryGetSoldierCombatDebug` 继续查询 BasicAttack。
- 卸下表示已配置槽位的停用，不是运行时删除静态目录。本批未实现热加载新目录、现存兵种即时换型或正式编成编辑入口。已有来源清理命令 `ClearAll` 清理升级来源/GM 覆盖，不清理显式配装选择；新局清理两者。

### 客户端调用入口

1. 对拥有的 `AGuLiBattlePlayerState` 调用 `GetWeaponChannels(Army)`，读取通道 Binding、候选列表、解锁/装备状态、最终参数及 LoadoutRevision。当前仅 Commander 的 Army 返回视图；空中域返回空数组。
2. 从该视图选择合法候选，调用 `ServerRequestEquipWeapon(新 RequestId, Binding, SkillId, 视图 LoadoutRevision)`；SkillId 为 None 表示卸下。服务器校验世界/局次、当前指挥官席位、所属 Team、候选/槽位、版本和请求频率。客户端不能提交任意数值或升级倍率。
3. 通过 `OnWeaponChangeResult` 接收 `Rejected`、`AwaitingCommit` 或 `Committed`。AwaitingCommit 仅表示排队成功，不代表已生效。快照复制和结果 RPC 来自不同 Actor，UI 必须等通道视图的版本达到结果版本后再展示其参数。
4. 同 RequestId、同内容重发返回缓存结果；同 ID 不同内容拒绝。每个 PlayerState 最多缓存最近 128 条，不承诺被淘汰旧 ID 的无限期幂等；新请求间隔至少 0.05 秒。被拒后改变请求或重试可重试错误应使用新 ID 和最新视图版本。
5. 本批将配装事务与其他来源变更串行到固定步；有待提交配装时，后续配置变更先拒绝，不允许覆盖在途配置。没有实现多请求排队、跨域联合事务或奖励消耗。

以上第一批代码合同已通过下文列出的 4 个精确定向自动化条目；尚未做最小游戏内内容切片、广域网络验收或实际换装菜单。

## P3 Ship / 僚机武器通道已接入代码

### 目录、GAS 与可靠投影

- `FGuLiShipAbilityGrant` 增加 `WeaponSlotId`、`SkillId`、`ProfileRevision` 和 `CooldownGroupId`。AbilitySet 目录仍使用稳定且唯一的 `AbilityId`，但允许多个目录项复用同一个 GA 类；配装要求恰好一个 Formation，武器可为 0–8 个且 `WeaponSlotId` 唯一。零武器配装仍是合法编队。
- Ship ASC 继续只给 Ship 授予 Spec；它增加 `Binding → SpecHandle` 精确索引。武器输入和授权按完整 Binding 路由，同一个输入标签若匹配多个 Spec 会拒绝隐式扇出。Formation/自动武器保持常驻，导弹保持输入触发；SpecHandle 仍不进入网络或成长身份。
- `FGuLiGroupAbilityConfigSnapshot` 升为协议 v9，可靠快照携带 MatchEpoch、Team、OwnerPlayerGuid、WingmanTypeId、完整 LoadoutRevision 和有界 `WeaponChannels`。每个通道冻结 Binding、SkillId、AbilityId、类型、Profile/Definition 版本、校验和及最终数值。原 Basic/Missile 固定字段仅作为现有 HUD/表现兼容镜像，战斗校验不再以其为真值。
- 通道列表和上下文字段都参与稳定 Hash；失效发布为显式 tombstone。Roster 和本地成员同时携带 WingmanTypeId，补员保留型号，租约拥有者/备份仍只消费可靠投影，不依赖远端 ASC 或 DataAsset 查询。

### 自动武器、导弹与冷却

- 所有启用的 `BasicAutomatic` 通道共享一次目标目录和空间索引，但逐成员、逐 WeaponSlot 独立选择目标和推进冷却。每成员保持一个 DomainFireSequence，协议意图再携带 Binding、SkillId、LoadoutRevision、ProfileRevision 和 DefinitionRevision。
- 本地 Mass 使用固定上限 8 的平凡数组保存逐槽下次开火时间，避免把动态容器放进 Fragment。服务端使用 `WingmanHandle → WeaponSlotId → NextFireTime` 权威校验；新/替换通道等待完整间隔，纯冷却数值变化按剩余比例迁移，其他槽不重置。补充生成的新 EntityGeneration 在客户端和服务端都从完整首发间隔开始，旧代际状态不会继承。
- 仅 Formation 投影变化才取消路径并重新初始化编队。纯武器快照变化保留成员身份、Transform、导航指导、Swarm 状态、已接受姿态和 DomainFireSequence。
- 右键从完整快照选择一个明确的 Missile Binding，再按该 Binding 激活/释放 GA。请求携带绑定和完整版本；服务器从当前已提交通道取目标规则、伤害、射程、弹速、寿命、扫掠半径和转向率。
- 主动武器冷却由 Ship ASC 按 `CooldownGroupId` 保存：相同组共享一次原子预留，不同组互不阻塞。默认 `WingmanMissileSalvo` 继续镜像旧 GAS 松散标签以兼容当前 HUD；批量发射失败只能由原 ActivationId 回滚对应组。Ship 死亡清理生命期冷却，同生命配装重授不会清空它。
- 自动通道名义总提交速率按当前 25 成员和 Relay 的 40 intent/s 预算校验；超过预算或超过 8 槽的快照拒绝发布，而不是在运行时无限扩张请求量。

### 服务端结算与来源追踪

- Relay 和 Combat Coordinator 校验协议、组/成员代际、Binding、SkillId、AbilitySet/Loadout/Profile/Definition 版本及当前 ASC 配置。自动伤害与导弹不再绕过提交快照读取原始 DataAsset 数值。
- ShotId 纳入通道及版本；直接伤害、逻辑导弹、导弹表现事件和死亡记录携带 Binding、SkillId、Loadout/Profile 版本及 RootEventId（终止表现保留所需来源字段）。已经发出的导弹保存发射时快照，后续换装不会改写其飞行或伤害。
- 本阶段提供的是可扩展的运行与协议底座，以及现有 AbilitySet 重新应用时的通道切换语义。玩家空中成长账本、已解锁候选、空中换装 RPC/只读技能视图和 UI 属于 P4；Ship 本体 WeaponPart 挂点通道属于 P5，均未冒充完成。

## 源码构建与静态核对记录

- 引擎：`D:\UnrealEngine-5.7`，目标 `GuLiStrikeEditor Win64 Development`。
- 使用项目规定命令：`& 'D:\UnrealEngine-5.7\Engine\Build\BatchFiles\Build.bat' GuLiStrikeEditor Win64 Development '-Project=D:\UE5.7\test1\GuLiStrike.uproject' -WaitMutex -NoHotReloadFromIDE`。
- 第一批初次构建曾因编辑器占用 `UnrealEditor-GuLiStrike.dll` 报 `LNK1104`；用户退出编辑器后使用同一命令重建成功。该阻塞已经解除。
- P3 最终源码构建：UHT/编译/链接/WriteMetadata 全部成功，UBT 退出码 0，`Result: Succeeded`。中间只出现一次 `ClearTimer` 的 const 句柄编译错误，修正后连续构建成功；没有用测试规避编译错误。
- 精确读取 `D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor.modules` 和 `D:\UE5.7\test1\Binaries\Win64\UnrealEditor.modules`：两者 BuildId 均为 `9872a344-2108-49e2-b40c-34c4e2c3ccfe`，项目模块为 `UnrealEditor-GuLiStrike.dll`。
- `git diff --check` 对本需求涉及的共享技能、Ship/Wingman、协议、Relay 和 Combat 源码没有空白错误；仅报告仓库既有的 LF→CRLF 工作区提示。
- Editor 目标会编译模块内既有测试源文件，这是构建依赖；P3 阶段没有启动自动化、PIE 或联机场景，也没有新增/修改 P3 测试文件。

## 风险与备忘

- 当前工作区已有大量用户改动；按当前文件增量接入，不清理/覆盖无关修改，也不修改已有测试来掩盖接口变化。
- Wingman 协议已由 v8 升到 v9；既有测试夹具仍有只填写 Basic/Missile 兼容镜像的旧构造，必须在获准的 P3 测试阶段显式迁移为完整上下文和 `WeaponChannels`，不能用放宽生产校验掩盖旧夹具。
- 通道数量增长影响寻敌与网络事件预算，保持有界集合和已有权威检查，不顺带扩展导航或压力测试。
- 自定义非默认 `CooldownGroupId` 目前由服务器权威管理；只有默认 `WingmanMissileSalvo` 继续以旧 GAS 标签向拥有者/HUD 提供冷却状态。后续若 UI 要显示多个主动组，应从通道冷却账本增加拥有者可见投影，而不是为每个武器硬编码标签。
- 本体自定义蓝图 Fire 必须消费最终参数接口；仅现有基类迁移不能证明所有资产都兼容。当前无畏舰默认空挂载不在本轮补武器资产。
- 兵种即时改型、跨玩家空中成长、跨域联合奖励和特殊攻击行为保留后续需求边界。

## 第一批定向验证结果（已获准、已执行）

第一批只运行用户确认的 T1–T3 范围，没有扩成模块前缀或完整 Gate。最终结果：

| 精确用例 | 结果 | 覆盖重点 |
|---|---|---|
| `GuLiStrike.Skills.Resolver.WeaponSlotEquipmentIsolation` | 通过 | 兵种/槽位隔离、替换及词条重算 |
| `GuLiStrike.Skills.Lifecycle.WeaponEquipmentTransaction` | 通过 | 权威换装事务、版本、幂等及固定步提交 |
| `GuLiStrike.Commander.Combat.MultiWeaponChannelsAndCooldownMigration` | 通过 | 每兵多槽执行、来源和冷却迁移 |
| `GuLiStrike.Skills.Lifecycle.NewMatchEpochClearsLedger` | 通过 | 新局清理配装与来源账本 |

- `WeaponEquipmentTransaction` 的第一次测试实现暴露出隔离 World 不会像真实网络驱动那样分派生成 RPC。生产入口因此抽出与 RPC 共用的 `ProcessEquipWeaponRequest` 服务器处理函数，再以同一真实校验路径测试；最终条目通过。没有因为夹具限制放宽权限、版本或候选校验。
- 第一批阶段未运行最小游戏内切片、PIE、联机矩阵、导航/避障、性能、打包、WM01 烘焙或 P3 测试。

## P3 定向测试结果（T4–T8 已完成）

用户已明确批准“只运行 T4–T8”。已迁移 5 个用例所需的 v9 内存夹具，并逐项以完整名称启动；每次均显示 `Found 1 automation tests`，没有运行 `GuLiStrike`、`Wingman`、`Ship` 或其他宽前缀。

| 编号与精确用例名 | 结果 | 实际覆盖 | 既有测试文件 |
|---|---|---|---|
| T4 `GuLiStrike.Ship.Abilities.WingmanWeaponBindingProjection` | 通过 | Formation-only 合法；四个武器通道；两个自动槽/两个主动槽可复用各自 GA 类且按 Binding 精确映射；共享 InputTag 不扇出；重复槽、8 槽上限和 40 intent/s 预算拒绝 | `Gameplay/Ship/Abilities/Tests/GuLiShipAbilityTests.cpp` |
| T5 `GuLiStrike.Wingman.Protocol.MultiWeaponChannelWire` | 通过 | FireIntent 的 Binding/Skill/Loadout/Profile 网络往返及确定性；旧协议、旧 Loadout、未知 Binding、Skill/Profile 不匹配精确拒绝；同成员跨槽序列连续 | `Battle/Contracts/Tests/GuLiWingmanProtocolTests.cpp` |
| T6 `GuLiStrike.Wingman.Simulation.WeaponOnlyConfigPreservesFormation` | 通过 | 纯武器配置不重置成员、编队、导航或已接受姿态；新增/替换完整间隔、冷却比例迁移和兄弟槽隔离；补员新代际不继承旧来源、冷却和序列 | `Gameplay/Wingman/Tests/GuLiWingmanMassSimulationTests.cpp` |
| T7 `GuLiStrike.Wingman.Combat.MultiChannelCooldownAndProvenance` | 通过 | 服务端同成员不同槽独立；相同主动 CooldownGroup 共享、不同组独立；提交 Profile 数值结算；直接伤害/逻辑导弹来源与发射快照；补员服务端首发门 | `Battle/Combat/Tests/GuLiWingmanCombatCoordinatorTests.cpp` |
| T8 `GuLiStrike.Wingman.Relay.WeaponChannelBootstrapRecovery` | 通过 | 完整通道数组、LoadoutRevision、兵种类型/稳定成长所有者进入 Hash；可靠 bootstrap 与备份接管冻结同一快照；tombstone 清槽且保留上下文；旧协议拒绝 | `Battle/Relay/Tests/GuLiWingmanRelayTests.cpp` |

- 源码构建：`GuLiStrikeEditor Win64 Development` 通过。
- 模块一致性：引擎与项目 `UnrealEditor.modules` 的 `BuildId` 均为 `9872a344-2108-49e2-b40c-34c4e2c3ccfe`。
- T4 首次命令在自动化队列启动前被 Fab 恢复浏览器标签的引擎断言中止，没有执行测试；随后仅增加 `-DisablePlugins=Fab` 并重跑同一个 T4，精确匹配 1 项且通过。T5–T8 采用同一启动规避参数。
- 编译前发现一个仍占用项目 DLL 的本项目 `UnrealEditor.exe` 残留进程；确认命令行为该 `GuLiStrike.uproject` 后只结束该 PID，再完成构建。
- 明确未运行：广域导航/避障回归、压力或长时间性能、打包、WM01 资产烘焙/ISM、Excel、P4 Roguelike/UI、P5 WeaponPart、本体资产、任何宽前缀或任何其他自动化测试。

## 结果链接

第一批与 P3 定向结果均已记录在本文件。P3 代码与获准的 T4–T8 已完成；P4 Roguelike/UI 与 P5 WeaponPart 等后续阶段尚未实施，因此整体仍保持“实施中”，不写完成归档。
