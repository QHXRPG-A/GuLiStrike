---
schema: guli-progress/v1
id: DEV-20260921-003
work_id: WORK-20260921-003
kind: development
role: root
title: FireReview双阵营靶场与敌方描边 — 技术方案
areas: [ground-mech, commander, combat, presentation]
categories: [art, gameplay]
status: done
verification: partial
created: '2026-09-21'
updated: "2026-09-21"
summary: 已修复装饰石持续更新导航造成的0单位问题，当前双端PIE读回各16台；修复飞行导航校验器ensure，源码Editor构建及地图资产验证通过，地图已保存重载。
next_action: 玩家在/Game/Maps/LVL_GroundMech_FireReview确认两队待命、敌方红色描边及受击销毁效果。
relations:
  requirement: REQ-20260921-003
status_note: 用户要求检查当前PIE并解决问题；在该会话修复后权威与两端名册均16台、满血存活，四类模型批次与阵营模板正确。沿用此前结束PIE、Editor编译重启授权，保存修复并重载，资产验证1通过0错误0警告；未主动启动新PIE或自动化测试，视觉效果待玩家确认。
---

# FireReview双阵营靶场与敌方描边 — 技术方案

## 技术选型与边界

复用真实Mass权威部署、名册与客户端表现池。部署点增加`bAllowAutomaticFire`，默认true，只在本图四个部署点设false；服务器在构建主动攻击通道时检查此值，单位仍保留在目标采样、伤害账本和碰撞数据中。其他地图及后续生产单位沿用默认允许开火。

正常机体实例按`UnitTypeId + Team`组织，新增批次按需创建，模板值为现有阵营枚举Red=1/Blue=2。未分配阵营的模型批次继续提供网格/血条高度等公共查询。服务器不创建描边表现；网络继续复用已传输的Team，不增加协议字段。

换阵营和换兵种触发实例迁移，旧槽隐藏并回收，保留SoldierId与选择环槽。换阵营分配失败时隐藏旧阵营机体，等待已有维护节奏重试。受击闪白覆盖层不写CustomDepth，由正常机体提供模板；相位与残骸不新增描边。相机继续通过VfxId 15材质判断本地敌我，保持可见表面判定。

## 场景参数

地图：`/Game/Maps/LVL_GroundMech_FireReview`，新增对象文件夹`Gameplay/FireReviewRange`。

| 部署点 | 阵营/兵种 | 中心XY，cm | 阵型/间距 |
|---|---|---|---|
| FireReview_Red_Sweeper | Red / 1 | 5500, -5000 | 2×2 / 700 |
| FireReview_Red_WarMachine | Red / 2 | 5000, 0 | 2×2 / 2000 |
| FireReview_Blue_Sweeper | Blue / 1 | 13000, -5000 | 2×2 / 700 |
| FireReview_Blue_WarMachine | Blue / 2 | 13000, 0 | 2×2 / 2000 |

四组朝向Yaw=-90，高度来自CommanderSoldier导航。原定红方扫荡者中心X=5000的一个位置距导航415cm，超过权威150cm容差，因此整体向东平移500cm；保留环境、数量、阵型与间距。修正后16个位置均通过投影及基于155/625cm单位半径的两两间距核对。

`GroundMech_Start_1/2`分别位于XY=(8600,-7500)/(9400,-7500)，Yaw=90，高度以半径235、半高380cm胶囊落地检测再加40cm余量确定。`FireReview_RangeNavigation`中心(9000,-3250,600)，半尺寸(8000,6250,2600)，覆盖Default与CommanderSoldier两类导航。保留原参考模型与FireReview GameMode。

## 任务清单

- [x] 接通部署点到服务器初始单位的停火配置。
- [x] 接通按阵营拆分的正常机体批次与模板值，保留池、血条及表现生命周期。
- [x] 检查声明/调用、默认值、死亡和换阵营路径、差异空白；仅适配既有类型路由测试的批次查询，不新增或运行测试。
- [x] 完成定向布置脚本及语法检查；保存起点与导航，核对16个计划位置。
- [x] 完成获准Editor构建、BuildId核对并加载新停火字段。
- [x] 保存四个停火部署点，重新加载地图并读回相关实体数据，确认重复布置不增加对象。
- [x] 同步交付记录，等待玩家实际效果确认。
- [x] 修复当前PIE中导航持续构建导致整批单位无法生成的问题，并永久保存装饰组件导航排除项。
- [x] 修复飞行导航World校验器适用范围，重新编译加载后验证FireReview资产。

## 用户开启PIE后的只读检查（2026-09-21 14:12—14:15）

用户明确要求检查已开启的PIE是否创建成功，据此只读取当前主机和远端客户端，没有启动、停止或操控游戏，也没有新增或运行自动化测试。

- 四个部署点确实进入主机世界，Red/Blue和兵种1/2组合齐全，均为2×2、`allow_automatic_fire=false`。部署点不加载到客户端符合原有服务器部署设计。
- 权威子系统`GetAuthoritativeMemberCount()`为0，`HasSpawnedAuthorityPopulation()`为false；主机和客户端名册均为0，正常机体与选择环实例均为0。当时没有生成计划中的16台部队，不能验收敌方描边。
- 两端资源子系统`IsRuntimeReady()`均为true；服务器发布组件Tick启用。主机的`IsNavigationBeingBuilt()`和`IsNavigationBeingBuiltOrLocked()`在多次读取中持续为true，初始构建锁配置为false；`TrySpawnAuthorityPopulation`在该导航就绪检查处直接返回，尚未提交单位生成。
- 对当前PIE中CommanderSoldier导航重新执行16个部署位置的只读投影，全部有效，最大XY修正88.09cm，小于150cm限制。导航持续报告构建的更深层原因尚未确定，不能归因为出生点无效或部署配置丢失。
- 证据：`outputs/firereview-20260921/pie-creation-readback.json`、`pie-creation-gates.json`、`pie-navigation-readback.json`。前次静态与编译结果仍成立，本次运行生成检查失败；保留既有静态交付归档，开发状态退回实施。

## 导航生成阻塞与资产校验修复（2026-09-21后续）

复现为用户开启的FireReview双端PIE，预期16台，实际0台。资源就绪、部署存在、出生点投影有效且游戏未暂停；首次错误转移位于导航就绪到批量生成之间。按先检查配置、再观察导航更新来源的顺序排查，没有绕过投影校验或增加超时强制生成。

- 开启3秒`LogNavigationDirtyArea VeryVerbose`后恢复原级别，捕获4组装饰石的12个动态网格产生4296次脏区更新。导航队列持续有更新，`TrySpawnAuthorityPopulation`一直等待。证据`pie-navigation-dirty.log`。
- 只将本图实例的12个动态网格`CanEverAffectNavigation`设false：两个悬浮石各5个组件、两个旋转石各`RockFlat2`。旋转石的静止`RockLong2`底座保持true。修改属性会重新构造Blueprint组件，脚本每次按名称取回当前组件，避免后续写入失效引用；最终统一重新读回。
- 在用户已开启的同一会话完成修复后，导航空闲、权威生成标记true；主机与客户端名册均为16个唯一且相同的ID，全部满血存活。Red/Blue各4扫荡者和4战争机器；四类正常机体批次各4台，CustomDepth启用、Stencil为1/2。证据`pie-after-navigation-repair.json`。没有主动操作射击或扩展压力测试。
- 沿用用户此前结束PIE、完成场景的授权，在确认没有未保存包后退出该会话。地图应用同一修复、构建并保存导航，重新加载并比较4个装饰Actor的类、变换、网格、相对变换和碰撞，无其他差异；12个排除项和16台部署配置保留。重启后再次读回通过，重复修复组件变化数0。证据`scene-after-navigation-repair.json`、`scene-final-readback.json`、`scene-repair_navigation.json`。
- 保存地图暴露独立的飞行导航校验器问题：`CanValidateAsset`接收所有World，`ValidateLoadedAsset`却对非必需且无飞行体积的World返回NotValidated，触发UE5.7接口ensure。FireReview不在4张必需地图中，也没有飞行体积；地面导航两份数据检查为Hit，地图并未因该ensure损坏。
- 将必需地图/启用体积判断移入`GuLiFlightNavigationWorldValidator::CanValidateAsset_Implementation`；无关World不接手，接手后的有效World仍执行原Cook检查并返回Valid/Invalid。必需地图缺失体积仍报错，没有放宽Cook要求。
- 沿用既有源码Editor构建重启许可，完成`GuLiStrikeEditor Win64 Development`构建，退出0、用时16.46秒，8份BuildId仍一致。新进程加载后使用Save场景的资产验证入口检查FireReview：1通过、0无效、0警告、0未验证、0跳过。证据`editor-build-validator.log`、`buildids-validator.json`、`asset-validation-after-fix.json`。不新增或运行自动化测试，不重新启动PIE。

当前重新回到`verification / partial`：单位生成与复制、组件模板数据和资产校验已确认；实际红色描边、待命及命中销毁视觉仍由玩家确认。

## 静态检查与场景交付

- 脚本：[author_fire_review_range.py](../../Scripts/GroundMech/author_fire_review_range.py)。`prepare`设置装饰导航排除项、起点与导航；`repair_navigation`只修复装饰导航并完成导航构建和保存；`deploy`要求新原生字段及排除项已加载，再配置四个部署点；`readback`核对当前地图实体。固定标识更新，不清理场景其他对象。
- 静态检查：源码审查、调用扫描、`git diff --check`和Python AST解析已通过。
- 编译许可：用户明确回复“允许编译并重启 UE，完成场景”。目标仅`GuLiStrikeEditor Win64 Development`，引擎`D:\UnrealEngine-5.7`；初次构建退出0，最终构建退出0。首次日志出现此前特效迁移的12处主头文件顺序诊断，已仅调整include顺序，最终构建不再出现。未构建Game目标。
- 引擎、项目及6个项目插件共8份`UnrealEditor.modules`的BuildId均为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`；重启编辑器读回新字段默认true。
- 起点、导航和四个停火部署点已保存并重载；两队各8台、全部`allow_automatic_fire=false`，16个位置通过导航与间距核对。重复执行布置脚本后四个部署点和导航体积的路径相同，没有重复对象。原两台展示参考的类、位置、旋转和缩放与本轮前一致。
- 实际描边资源仍由目录ID 15提供，读回默认颜色`(1,0.015,0.025,1)`、相机管理器绑定及Ground优先角色配置通过。保存后无脏地图或脏内容包。
- 初次静态交付没有执行PIE验收；后续用户明确要求读取其已启动的双端PIE并解决问题，按上节完成定向诊断、修复及单位读回。助手没有主动启动PIE、Standalone、自动化、性能测试或截图验收。

## 玩家操作与预期效果

1. 完成部署交付后打开上述地图，普通单人进入，继续控制地面机甲，默认属于红方。
2. 前往两组靶场，必要时滚轮拉远镜头。每队应为4扫荡者和4战争机器，保持待命且不会自动交火；保留正常友军碰撞退让。
3. 蓝方敌军的可见轮廓为红色，红方友军没有敌方红色描边；遮挡后不新增透墙显示。
4. 按住左键射击蓝方，观察机枪受击特效、闪白、血量及销毁；死亡后的残骸不保留敌方描边。退出并重新进入可重置目标。

以上是玩家待确认项，本轮静态和场景实体读回不代替运行效果验收。

## 结果链接

- [需求](../RequirementDocument/20260921-FireReview双阵营靶场与敌方描边.md)
- [导航阻塞与资产校验修复归档](../Archive/20260921-FireReview导航阻塞与资产校验修复.md)
- 本轮证据目录：`outputs/firereview-20260921`，包含原始代码副本、任务增量diff、原场景参考数据、导航读回、构建及后续部署读回报告。
