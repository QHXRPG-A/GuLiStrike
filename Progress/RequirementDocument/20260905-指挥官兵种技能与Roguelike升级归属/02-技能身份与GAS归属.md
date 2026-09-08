---
schema: guli-progress/v1
id: REQ-20260905-001-D02
work_id: WORK-20260905-001
kind: requirement
role: detail
title: 指挥官兵种技能、Ship 武器通道与 Roguelike 升级归属 · 技能身份与 GAS 归属
areas:
- wingman
- commander
- ship
- ui
- network
status: draft
verification: not_applicable
created: '2026-09-05'
updated: '2026-09-07'
summary: 陆军稳定键继续为：ArmySkillBindingKey = MatchEpoch + Team + UnitTypeId + SlotId。它是下述统一身份规则的陆军形式，不要求为本次规划更换现有陆军 ID
next_action: 确认技能计数口径、冷却归属和奖励叠加规则后，再批准需求并推进实施任务。
relations:
  parent: REQ-20260905-001
status_note: 草案，待确认；不是已批准的开发任务
split_order: 2
split_segment_sha256: 0db515f2086d5adc8a9825af7a0844e6ca36b3b0a0d707b575439d4bcd19995f
---

# 指挥官兵种技能、Ship 武器通道与 Roguelike 升级归属 · 技能身份与 GAS 归属

<!-- guli-progress:split-content -->
## 4. 技能身份：定义、绑定和升级不要混成一个对象

陆军稳定键继续为：`ArmySkillBindingKey = MatchEpoch + Team + UnitTypeId + SlotId`。它是下述统一身份规则的陆军形式，不要求为本次规划更换现有陆军 ID。

- `SkillId`：使用哪一种行为定义，不是某位指挥官独有的升级容器。
- `SlotId`：同一兵种可独立装备、替换、解锁的技能位置；例如 `BasicAttack`、`SecondaryWeapon`。
- `BindingKey`：本局这支军队的这个兵种的这个槽位。换武器后键不变，绑定的 SkillId 改变。
- `UpgradeInstanceId`：某一次已获得奖励的稳定实例 ID，用于幂等、撤销和追溯。
- `GameplayAbilitySpecHandle`：可选 GAS 入口的运行时句柄，不作为跨换任或重建的成长身份。

例如两个兵种都使用 `Strafe` 时，提升 WM01 的 `BasicAttack` 不能修改共享的 Strafe 定义，否则另一个兵种也会受影响。同一兵种左右两个槽即使装备相同 SkillId，也应是两个独立绑定。

“一武器一技能”在这里表示一个独立装备/成长单元，不表示每个模型、等级、词条或弹丸都是新技能。给机枪附加燃烧通常仍是原来的一个技能，而不是多出一个武器槽。

### 4.1 地空共用的通道身份

建议统一逻辑形式：`WeaponBindingKey = MatchEpoch + OwnerKey + Domain + SubjectId + SlotId`。这是待实现的设计合同，不是当前已有的 C++ 类型。

| 字段 | 陆军 ArmyUnit | 僚机 Wingman | 飞船本体 ShipMounted |
|---|---|---|---|
| OwnerKey | 本队 Team | 建议为本队内稳定玩家身份对应的 AirLoadoutOwnerId | 同一玩家的 AirLoadoutOwnerId |
| SubjectId | UnitTypeId，例如 WM01 | WingmanTypeId；首版可只注册当前一种僚机 | 稳定 ShipLoadoutId，关联舰体/挂载布局 |
| SlotId | BasicAttack / SecondaryWeapon | BasicWeapon / Missile / 后续 SecondaryWeapon | 稳定武器挂点 ID，显式映射 SocketName |
| 成长作用对象 | 本队该兵种该槽的全部单位 | 此所有者配装下该僚机类型该槽的全部成员 | 此所有者该飞船配装中的指定武器挂点 |

`AirLoadoutOwnerId` 建议由服务器本局稳定玩家身份绑定，不直接使用 Pawn 指针、ASC、ShipInstanceId 或当前执行模拟的客户端。租约备份客户端获得的是模拟职责，不能因此获得武器升级选择权。空中成长的玩家/队伍归属仍是产品待确认项，见第 12 节。

约束：

- `Domain` 必须参与键和选择器匹配；相同名字的陆军 BasicAttack、僚机 BasicWeapon 或 Ship 主炮不会互相升级。
- 僚机类型身份要在 Roster、成员和通道快照中有明确映射。当前仅有一种型号，不代表已有通用 WingmanTypeId 配装支持。
- 武器 `SlotId` 与僚机编队的 Flight / 成员 SlotIndex 是两回事；协议字段建议明确称 `WeaponSlotId`，避免误把编队位置当武器槽。
- 同槽替换保持 BindingKey；本稿统一以 `SkillId` 表示当前武器技能定义。若资产使用 WeaponId，只通过明确映射接入 SkillId，不维护两套可各自修改的“当前武器”。
- `AbilityId` 表示 GAS 入口定义，`SpecHandle` 表示一次运行期授予，均不代替 BindingKey。可有相同 SkillId 或 GA 类对应多个独立绑定。
- Ship 换舰体时，按挂载兼容规则重新匹配；不存在的挂点保持未装备/升级暂不生效，不能按同名 Socket 擅自转移成长。

### 4.2 配装身份与执行身份

共享通道配置不存每个成员的锁定目标和冷却。执行侧另用带生命周期的身份：陆军 `SoldierId + WeaponSlotId`，僚机 `WingmanHandle（含代际）+ WeaponSlotId`，本体武器 `ShipInstance/Generation + MountSlotId`。组级齐射冷却则属于当前 Ship / Group 的显式 CooldownGroup。

成长键可以跨同局 Ship 重生保持稳定；开火请求必须同时携带当前 Ship/Group/成员代际、权限与版本。保留成长不能让上一条生命的迟到请求对新僚机生效。

### 4.3 建议的数据归属

| 数据/状态 | 权威归属 | 职责 |
|---|---|---|
| SkillDefinition、通道与候选配置 | 静态配置 | 定义行为、基础参数、所属 Domain/类型、默认装备、允许候选和初始解锁状态 |
| UpgradeDefinition | 静态配置，待新增 | 定义可选目标、修改内容、叠加组、上限和互斥规则；不在此稿指定平衡数值 |
| RunUpgradeInstance / 局内装备选择 | 服务器按 OwnerKey 分区的本局权威记录，待新增 | 陆军按 Team、空中按已确认归属保存；记录目标和随机结果，支持重算 |
| ResolvedSkillProfile + LoadoutSnapshot | 共享解析规则与各域适配层 | 每个绑定有唯一最终配置出口和版本；GA、客户端预测与服务端结算消费同一版本的适用投影 |
| Target、开火时刻、暂停进度等 | 各执行层的实体/通道运行状态 | 陆军每兵、僚机每成员、本体每挂点分别运行；共享资源另有显式组状态 |
| 可选 GA 入口与映射 | Army ASC 或 Ship ASC + 对应适配层 | 引用绑定并管理激活流程；不成为成长唯一存储 |

沿用现有 GuLiStrike 模块。共用身份、选择器和修饰计算规则放在现有 Gameplay/Skills 可复用层，陆军 Team 管理仍留在 Army 子系统，空中由独立适配接入 Ship 配装与投影；不把 `GuLiArmySkillSubsystem` 直接变成飞船逐帧战斗管理器。UI只读技能视图；共享解析不反向依赖 Commander/Mass、Wingman、Ship ASC 或表现层。

### 4.4 武器定义与最终 Profile

通道配置应至少描述：允许候选 SkillId、解锁/装备状态、触发方式、执行行为、目标规则、冷却范围、可选共享火控/资源组，以及表现定义引用。武器的自动/主动触发方式与独立/共享冷却是分开的字段。

最终配置采用“公共字段 + 执行行为专用参数”。伤害、射程和标签可共用解析规则；逻辑导弹的转向/寿命、部件弹丸类与炮口、其他行为参数由各自类型校验。自动射击的攻速与间隔只选一个作为权威输入并显式转换；齐射冷却是单独属性，不能被普通射速升级隐式修改。

陆军现有表、Ship WeaponDefinition DataAsset 和部件配置可先各自作为静态输入，再由适配器输出统一绑定下的最终 Profile。首版不强制搬成一张巨型表，也不假定三种执行器能直接使用同一结构。服务器结算不能继续绕过 Profile 从原始 DataAsset 或部件默认值拿伤害，否则升级只会改变 UI/预测。

### 4.5 新增兵种与武器的扩展合同（用户明确要求）

| 扩展内容 | 应通过什么接入 | 核心运行层的要求 |
|---|---|---|
| 新陆军兵种 / 新僚机型号 | 类型定义、资源/表现配置、通道模板、初始武器和可用候选 | 按注册类型和模板创建配置/状态；不把 WM01 或当前 Wingman 型号写死在解析、索敌或 UI 分支里 |
| 新武器，复用已有攻击行为 | 新 SkillId、基础参数、标签、表现引用、适用 Domain/类型/槽条件 | 注册并解析后即可加入候选与局内换装；无需新增 GA 类或硬编码 SkillId 分支 |
| 新攻击行为 | 实现并注册 ExecutorId 及其参数校验、目标/命中与表现适配 | 新行为通过接口接入，既有行为的执行与成长解析不重写；未注册执行器不能进入正式装备候选 |
| 新并行槽位 | 通道模板增加稳定 SlotId、解锁规则、候选、触发/资源组配置 | 运行状态、复制、技能视图按集合处理；不再只支持固定 BasicWeapon / Missile 两字段 |
| 新本体挂载武器 | 挂点兼容配置 + 接受最终 Profile 的部件执行器 + 弹丸/表现资源 | 已有挂点支持候选换装；新增挂点需要舰体资源提供实际 socket，不由表格凭空生成 |

类型定义描述“这个兵种/型号有哪些槽、能装什么”，武器定义描述“这件武器如何攻击”，局内配装记录描述“这个所有者现在装了什么”。默认模板只用于初始化，不能在每次生成/补员时覆盖已经动态更换的局内配装。

新增类型与武器沿用项目已有表或 DataAsset 制作路径，通过稳定 ID 和注册/校验适配进入运行时。通道候选以兼容条件及显式允许列表约束；两个执行域能共用设计参数不等于可以直接共用弹丸实现。缺失类型、重复 ID、不兼容槽位、未实现行为和非法参数均在候选阶段报错并保留旧有效配装。

所有动态集合须有可配置的容量上限，结合目标规模确定；本稿不承诺无限兵种/武器，也不把上限固定为当前 2 个兵种或 2 个僚机武器。这里的“可扩展”指开发期可添加并交付内容，“局内动态”指切换本次构建已注册、可加载且已解锁的内容；不额外要求在已发布进程里热编译 C++、读取未烹饪资源或实时重导 Excel。

## 5. GAS 接入方案与 Ship 的保留职责

### 5.1 陆军的两种可行方案

| 方案 | 指挥官端形式 | 适用情形与代价 |
|---|---|---|
| A：数据核心 + 按需 GA（当前推荐） | x+y 个兵种技能绑定；通用军队桥接 GA，另为需要能力生命周期的技能提供 GA | 复用现有解析与 Mass 执行；最直接满足定向数值升级、替换和解锁。技能清单、等级与解释信息由兵种技能视图提供，不能直接当作 ASC 列表读取 |
| B：数据核心 + 每绑定一个 GA 入口 | x+y 个武器 Spec，按行为复用 GA 类；此外仍可有指挥官自身技能和系统桥接 GA | 适合大量技能需要 GAS 激活、事件、阻塞/取消或统一能力工作流。需要维护绑定映射、生命周期和复制适配，但不必逐兵、逐发激活 |

两者都保留同一套局内成长记录、最终 Profile 与 Mass 执行。A 可以逐步接入 B，不需要换掉成长模型。不能仅凭 Spec 数量认定 B 性能不可接受，也不能把 B 的能力授予误当作已经实现了多武器战斗。

选择建议：若近期主要做“伤害/攻速/射程升级 + 换武器 + 解锁副武器”，先做 A；若近期已有具体需求要求每种武器独立受 GAS 事件、取消或能力阻塞控制，则接入 B。自动/被动技能同样可以成为这些 GA，手动输入不是唯一门槛。

不推荐为当前 Mass 士兵逐兵建立 ASC，也不推荐只在 GA 实例中保存升级数值、让模拟逐兵查询 ASC。

### 5.2 本稿推荐的地空组合

| 对象 | 建议 GAS 接法 | 参数和执行归属 |
|---|---|---|
| 陆军自动武器 | 优先方案 A，按具体生命周期需求增加 GA | 绑定/Profile + 服务器 Mass 多槽战斗 |
| 僚机 BasicWeapon | 保留常驻自动武器 GA，改为授权指定 BindingKey | 每架僚机按该通道寻敌/生成意图，服务器逐槽校验和结算 |
| 僚机 Missile | 保留主动导弹 GA，引用指定通道和共享冷却组 | 服务器选 Flight、校验目标并创建逻辑导弹 |
| 后续僚机并行槽 | 可复用自动授权或主动 GA 类，为需要授权的绑定授予轻量 Spec | 相同类不会合并不同槽位的配置或运行状态 |
| Ship 本体挂载武器 | 默认继续部件开火；有蓄力、取消、费用等具体需求时再接 GA | 挂点通道 Profile + 现有服务器部件/弹丸链 |
| Formation | 保留独立编队 GA | 继续输出导航/编队配置，不参与武器槽计数和武器奖励匹配 |

输入应按 `InputAction / CommandId → 明确 BindingKey 或显式通道集合` 路由。当前右键保留为僚机 Missile 指令；飞船本体开火继续只路由已装本体武器。多个 Spec 复用同一 GA 类或基础 InputTag 时，不能默认一起激活。将来允许一个“齐射”指令驱动多个通道时，应声明集合和共享资源规则，指令本身不重复计作额外武器。

Ship 当前用 AbilityId 索引 Spec，需要增加 BindingKey 映射；同定义两槽授予、换装撤销和输入定位均以绑定为准。纯数值升级更新 Profile 和投影，不重授无关 GA；行为或入口需要改变时，只更新该绑定相关 Spec。Formation 的活动状态、种子和导航版本不因普通武器升级而重置。

主动请求继续由服务器确认实际成功与冷却消费。现有导弹冷却是标签/计时器及预留流程，不假定它已是通用 GameplayEffect 冷却；扩展时要维护一份权威冷却状态，再投影给 GA 与 HUD，避免 GA 和战斗协调器各计一遍。

