---
schema: guli-progress/v1
id: GAMEPLAY-COMMANDER-D03
work_id: ''
kind: gameplay
role: detail
title: 指挥官 · 数据、技能与武器表现
areas:
- commander
- ui
- network
- vfx
- combat
status: current
verification: not_applicable
created: '2026-09-07'
updated: '2026-09-07'
summary: 数据、技能与武器表现
next_action: ''
relations:
  parent: GAMEPLAY-COMMANDER
status_note: 2026-09-06（接入两兵种枪口/瞬时弹道、WM01 独立导弹槽与通用法术场；本次验收边界见对应开发文档）
split_order: 3
split_segment_sha256: ebcf50a16d7a5ce5a1a7d623281a85a3e950ea58bf5afb1f89f5eabda87c11a7
---

# 指挥官 · 数据、技能与武器表现

<!-- guli-progress:split-content -->
### Soldier 数据与运行时 GM

- `GuLiStrikeCommander.xlsx` 的Soldiers表包含A（ID1/100生命）和B（ID2/300.5生命）。Skills定义扫射及同执行器测试变体；UnitSkills按兵种+技能+槽保存伤害、次/秒、厘米射程与默认装备。旧Soldiers攻击力/射程列已移除。
- 每个Game/PIE World初始化时加载和校验，不逐兵逐Tick查表。兵种解析保留安全回退；技能目录出现未知兵种/技能、重复默认槽、无效标签或参数时禁用并报告错误，不偷偷选择其中一行。
- 服务器非Shipping使用`gs.GM.Skill`查看最终配置/来源、覆盖数值、添加/更新/撤销中性来源、替换普攻、查询目标/冷却/停火原因及生成调试兵。Set覆盖位于正常结果之后，Reset只撤该槽数值覆盖，不删除其他来源。
- 导航诊断使用`gs.GM.Commander.Nav.Soldier <SoldierId>`查询目标槽、游标、冷却、恢复状态和失败上下文，使用`gs.GM.Commander.Nav.Stats`查看活动、到达、恢复、Blocked、Surface调用/失败及路径查询聚合；`gs.GM.Commander.Camera.Debug <0|1>`独立控制相机调试，并显示整帧请求/实际平移比例、硬净空/预测/保持Z、重锚、请求有效性与应急抬升量/累计次数。以上命令仅存在于非Shipping构建。
- `gs.GM.Commander.Nav.LastMove [CohortId]`输出最近请求目标、候选、掩码、冲突、拆分、查询和规划耗时；`Nav.Stats`增加Pending规划、候选投影、路径查询、单兵部分接受和结构化失败阶段。候选失败只计数，最终命令才输出一次汇总Warning。
- 原`gs.GM`保留生命、移动等现有调参；最大生命按原生命比例换算，死亡单位保持0。旧攻击力/射程键明确提示改用`gs.GM.Skill`。两套调试入口均不写回Excel、DataTable、ini。

### 扫射与指挥官 GAS 桥接

- 停止、移动和导航恢复期间都自动攻击射程内敌方Mass小兵，不追击；移动状态不清目标、不刷新冷却。每200ms分批索敌、每次开火复核，目标失效后选最近者，同距按SoldierId。
- 两兵种共用扫射执行器，A为10伤害/2次每秒/100m，B为7.5伤害/4次每秒/150m。独立冷却，换目标不刷新，无补射积累；攻速变化保留冷却比例，主体替换等完整新冷却。同一步先收集再结算，允许互杀。
- 公共BattlePlayerState增加ASC，仅Commander获得军队能力。ServerOnly GA向World/Team账本提交来源；Mass只读已解析缓存，不为兵种或单兵新建ASC。客户端只接收最终配置和权威战斗结果，不逐发RPC、不结算伤害。
- 来源ID更新不重复叠加；两个独立+20%为×1.44。技能优先级替换和数值修改整笔校验，冲突保留旧状态。来源随团队保留，重生/指挥官换人不靠旧ASC自动撤销，新战局清空。
- 执行器注册支持新行为，附加效果仅留接口；没有科技树、卡牌、火炮实体、燃烧或穿透。详细规则见[战斗](../战斗.md#小兵普通攻击扫射)，GM示例及本期验证见[开发记录](../../DevelopmentDocumentation/20260901-小兵扫射与指挥官GAS桥接.md)。

### 指挥官武器特效与独立技能运行层

- FourFRobot、WM01 的机枪保留既有伤害、攻速、射程和升级解析。权威射击通过后才发布表现；每客户端常驻一个共享 Niagara/Data Channel 批处理容器。弹道默认 0.075 秒、宽 30 cm，以同一呈现时刻的一对端点写入中点/方向/全长，并强制旋转为 0、速度为 0，因此严格从枪口瞬时连到目标，不生成实体子弹。枪口在最近一发后的 2 秒战斗活动期内以 30 Hz 刷新，尺寸 420×1200 cm，并以 9 Hz 频闪。
- 枪口和弹道都能用暖色动态光照亮地面；每个表现更新只启用离相机最近的 12 个枪口灯和 6 个弹道灯，不生成逐发 PointLight Actor/Component。材质零纹理，不使用 UE 默认占位图。该修正只涉及机枪弹道/枪口，WM01 导弹轨迹没有变化。
- 枪口与 WM01 双导弹仓来自已校准的最终 Crowd 模型局部坐标，表现时乘同一帧单位变换，包含缩放及枢轴偏移；不假设 Crowd 保留 socket，不增加炮塔转向或骨骼动画。
- 独立骨骼制作位于 `/Game/GuLiStrike/Robots/<Unit>/Rig`：尺度修正与 Control Rig 草稿已保存，另建 6 个 `FX_` Mesh Socket 供用户精调；武器绑定和可视验收待完成。**未替换静态 Crowd，也未回填开火/瞄准坐标**；`FX_AimTarget` 仅预留为视觉目标位置，不改变导弹或伤害判定。见[Socket 记录](../../Archive/20260906-双机甲可调Socket创建.md)。
- WM01 默认另装 `MissileLauncher`，与 `BasicAttack` 分别索敌、分别冷却。导弹调试初值：3 秒一枚、30 伤害、150 米射程、8 米爆炸半径；速度 60 米/秒、最长 8 秒。源表为 `GuLiStrikeCommander.xlsx`：Skills 通过 `EffectConfigId` 关联 SpellFields，SpellFields 控制法术场伤害、半径和时序，UnitSkills 控制兵种槽、冷却、射程和升级入口；数据资产只配置飞行/爆炸表现及视觉标尺。
- 当前 `SpellFields.WM01_MissileExplosion` 为 Damage=30、RadiusCentimeters=800、Timing=Instant、DissipationSeconds=3。该槽的 UnitSkills Damage 必须保持 0，加载器会把表中的唯一基础伤害注入现有技能修饰链，固定值/百分比升级仍有效。只改现有数值无需编译：改 Excel 后导出、导入四张 Commander DataTable，并结束/重进 PIE；改列结构才需 C++ 编译。
- Mass 仅提交开火及公共目标/位置适配。`Gameplay/CombatEffects` 独立推进导弹和法术场，公共蓝图接口为 `LaunchProjectile`、`CreateSpellField`、`CancelEffect`、`QueryEffect`，不接收 Mass Fragment/EntityHandle。客户端没有权威伤害提交入口。
- 导弹抬升、弧线追踪及末段收敛由服务器种子驱动；丢失目标后飞向最后位置。碰撞/抵达仅创建一次爆炸法术场，不叠加直接伤害；超时、取消和换局不补爆炸。三个项目爆炸变体等概率，服务器选定，客户端使用相同变体。
- 资源集中在 `/Game/GuLiStrike/FX/CommanderWeapons`；商城原件不改。爆炸父材质与翻页纹理已修复为项目副本，不使用引擎棋盘或缺失纹理。法术场时序与账本规则见[战斗](../战斗.md#独立导弹与通用法术场)。
- 本次源码版 Editor/Game、15/15 聚焦测试、Listen+1、Dedicated+2、后加入和低画质验证通过；最终 44 帧窗回归证明两个渲染端进入交火后枪口粒子持续存在、动态光按来源错相频闪，专服保持零表现组件。500 单位开关性能与图片见[归档](../../Archive/20260906-指挥官武器特效与独立法术场.md)，接口/范围见[开发文档](../../DevelopmentDocumentation/20260905-指挥官武器特效与独立法术场.md)。不把以下历史相机/导航结论当成本次特效验收。

