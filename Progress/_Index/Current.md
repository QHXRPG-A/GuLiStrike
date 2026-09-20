# 当前工作

> 自动生成，请勿手改。运行 `progress_docs.py build` 刷新。

## 待确认需求

| 工作项 | 模块 | 任务 | 下一步 | 更新 |
|---|---|---|---|---|
| [基地建造玩法探索草案 v0.1](../RequirementDocument/20260831-基地建造玩法探索.md) | assets, building, combat, commander, ship | — | 玩家能说明选址和建筑选择的原因、放弃的其他投入，以及队友的作用 | 2026-09-17 |
| [GuLiStrike：5v5 大战场玩法策划草案](../RequirementDocument/20260826-5v5大战场玩法草案.md) | building, commander, network, ship, wingman | — | 按首版 3v3 需求细化 Ship 战略机动与三线选牌节奏，验证操作者如何制造可被指挥官兑现的突破。 | 2026-09-17 |
| [指挥官兵种技能、Ship 武器通道与 Roguelike 升级归属 — 技术方案](../DevelopmentDocumentation/20260905-指挥官兵种技能与Roguelike升级归属.md) | commander, network, ship, ui, wingman | 9/18 (50%) | P1：稳定绑定、可扩展通道定义、兼容校验、启用/移除与完整 Loadout 版本；代码已覆盖 Army/Wingman，仍待 P3 协议定向验证 | 2026-09-07 |
| [非 Mass 大规模弹道与特效架构 — 技术方案](../DevelopmentDocumentation/20260906-非Mass大规模弹道与特效架构.md) | combat, commander, network, vfx, wingman | 0/17 (0%) | 用户确认范围假设：仅特效/弹道栈不用 Mass，不移除单位系统 Mass | 2026-09-06 |
| [据点混凝土巨构模型 — 技术方案](../DevelopmentDocumentation/20260905-据点混凝土巨构模型.md) | assets, building, combat, commander, vfx | 9/9 (100%) | — | 2026-09-05 |

## 规划中

| 工作项 | 模块 | 任务 | 下一步 | 更新 |
|---|---|---|---|---|
| [实时势力范围与阵营覆盖](../RequirementDocument/20260916-实时势力范围与阵营覆盖.md) | building, commander, map, ui | — | 围绕现有据点与Territory梳理势力源、扩散规则、刷新方式及覆盖渲染，形成技术方案。 | 2026-09-16 |
| [首版 Demo 3v3、Ship 战略机动与高频选牌](../RequirementDocument/20260909-Demo3v3与Ship战略机动及高频选牌.md) | combat, commander, economy, ship, ui | — | 细化战略机动的调用与支付方式、出入口和僚机随行规则，以及三线选牌频率、单次强化幅度和 Ship 卡牌内容规划。 | 2026-09-09 |
| [蓝矿、红矿、据点维护与三线 Roguelike 成长](../RequirementDocument/20260908-蓝矿红矿与据点维护及三线Roguelike成长.md) | building, combat, commander, economy, ship | — | 细化维护及断供恢复、战略机动支付与采集加工参数，以及三线经验曲线、Ship 高频选牌和强力牌费用。 | 2026-09-09 |

## 实施中

| 工作项 | 模块 | 任务 | 下一步 | 更新 |
|---|---|---|---|---|
| [游戏对象与效果统一缩放至0.2倍 — 实施记录](../DevelopmentDocumentation/20260918-游戏对象与效果统一缩放至0.2倍.md) | building, combat, commander, economy, navigation, network, ship, ui, vfx, wingman | 10/13 (77%) | 继续其余0.2倍真实运行门禁（Mass、Ship/僚机、建造、空运、护盾占领传送）及用户视觉验收 | 2026-09-18 |
| [Q导弹圆面打击与赠品建筑防卡死 — 技术方案](../DevelopmentDocumentation/20260918-Q导弹圆面打击与赠品建筑防卡死.md) | building, combat, commander | 8/9 (89%) | 定位原LogUtils错误的实际调用者；复核工程车辆/玩家清场、受阻改选及边界越程等未覆盖行为。 | 2026-09-18 |
| [GuLiStrike 自然地编资源包 — 制作与验收](../DevelopmentDocumentation/20260917-自然地编资源包.md) | art, assets, rendering | 6/9 (67%) | 用户审核v3工艺及外形参考后，以图集和卡片制作六件Blender代表样板。 | 2026-09-17 |
| [Ship导入与扫荡者战争机器风格重制 — 实施](../DevelopmentDocumentation/20260916-Ship导入与扫荡者战争机器风格重制.md) | combat, commander, rendering, ship | 9/12 (75%) | 继续实战轰炸、僚机死亡、机械姿态和完整性能验收。 | 2026-09-17 |
| [指挥官白模据点占领与建筑体系 — 技术方案](../DevelopmentDocumentation/20260914-指挥官白模据点占领与建筑体系.md) | building, commander, data, economy, map | 18/20 (90%) | 补齐包围、厂内延后指令及运输边界观察；坡道断言已由9月16日落点修复处理。 | 2026-09-16 |
| [游戏内 GM 分页浮层面板 — 技术方案](../DevelopmentDocumentation/20260910-游戏内GM分页浮层面板.md) | combat, commander, network, ui | 5/6 (83%) | 重启源码版 Editor，完成三分辨率、Commander/Ground/Air、Standalone/Listen/客户端的人工 PIE 验收。 | 2026-09-10 |
| [地图战略点标注与数据导出工具 — 技术方案](../DevelopmentDocumentation/20260906-地图战略点标注与数据导出工具.md) | assets, building, commander, data-pipeline, map-authoring | 15/22 (68%) | M2 完整验收（实现完成，交互矩阵待验收） | 2026-09-07 |
| [DIY 飞船（模块化装配 + 飞行中热切换） — 技术方案](../DevelopmentDocumentation/20260820-DIY飞船.md) | assets, network, ship, ui | 28/29 (97%) | 用户手动加 7 个 socket（教程见下，坐标已定稿） | 2026-09-05 |
| [僚机世界空间近距编队与租约恢复 — 技术方案](../DevelopmentDocumentation/20260904-僚机世界空间近距编队与租约恢复.md) | network, ship, wingman | 8/10 (80%) | 在优化配置复跑完整 GuLiStrike.Wingman：DebugGame 已执行 79 项，仅既有 H4000 ServerValidator 性能预算失败 | 2026-09-04 |
| [指挥官与飞船共享 HUD 及 Ship UI v1 — 技术方案](../DevelopmentDocumentation/20260902-指挥官与飞船共享HUD及Ship UI v1.md) | assets, commander, network, ship, ui | 20/25 (80%) | 保留 Commander 全部既有数据、输入、MiniMap、Tooltip 和生命周期行为 | 2026-09-04 |
| [指挥官相机、编队导航与移动射击优化 — 技术方案](../DevelopmentDocumentation/20260901-指挥官相机编队导航与移动射击优化.md) | building, combat, commander, ui | 12/16 (75%) | 完成Server target构建；Launcher版UE5.7明确拒绝Server targets are not currently supported from this engine distribution，属于环境限制，需要源码版引擎或支持Server的发行环境 | 2026-09-01 |
| [数据管线：Excel 配置飞船数值 — 技术方案](../DevelopmentDocumentation/20260821-数据管线Excel配置.md) | assets, data-pipeline, ship | 15/15 (100%) | 无头 JSON→DataTable 导入对 FText/嵌套 FVector 的兼容性 → 降级 CSV 后端（导出脚本双格式输出） | 2026-08-25 |

## 待验收

| 工作项 | 模块 | 任务 | 下一步 | 更新 |
|---|---|---|---|---|
| [玩家地面机甲开火与Excel升级配置](../DevelopmentDocumentation/20260920-玩家地面机甲开火与升级配置.md) | combat, input, network, vfx | 8/9 (89%) | 用户审核FireReview的枪口、弹道及缓冲后坐；批准后将已验证配置接到正式BP_GroundMech_Light。 | 2026-09-20 |
| [玩法地图对齐Demo光照](../DevelopmentDocumentation/20260918-玩法地图对齐Demo光照.md) | art, rendering | 6/7 (86%) | 用户查看实际PIE截图确认明暗；战争机器拥挤按独立诊断决定后续实施范围。 | 2026-09-18 |
| [松树林原地图全资源风格重构 — 实施记录](../DevelopmentDocumentation/20260918-松树林原地图全资源风格重构.md) | art, assets, rendering | 11/11 (100%) | 等待用户审核原Demo_Map整图；按反馈修订，动态录像、GPU性能及编辑器崩溃根因不冒充已验证。 | 2026-09-18 |
| [松树林与三单位同场景对照 — 试摆记录](../DevelopmentDocumentation/20260917-松树林与三单位同场景对照.md) | art, assets, rendering | 11/13 (85%) | 在WORK-20260918-002完成原地图全资源适配与整图终验。 | 2026-09-18 |
| [地面机枪飞行弹丸与僚机弹效复用](../DevelopmentDocumentation/20260917-地面机甲弹幕肉鸽与塔防建造.md) | building, combat, network, vfx | 6/6 (100%) | 跟踪既有导弹夹具和挂点断言失配；地面弹丸、配色与5Hz专项留待后续安排。 | 2026-09-17 |
| [僚机对地轰炸动漫爆炸样板 — 实施与验收](../DevelopmentDocumentation/20260916-僚机对地轰炸动漫爆炸样板.md) | combat, vfx, wingman | 5/7 (71%) | 用户先在 UE 评审视觉；随后补齐稳定 GPU 对照、尺寸精测和真实客户端/专用服务器验收。 | 2026-09-16 |
| [工程车地面动态避障 — 技术方案](../DevelopmentDocumentation/20260915-工程车地面动态避障.md) | commander, navigation, resources | 4/4 (100%) | 后续确认旧BuildingWorld测试更新范围，补密集狭路及大规模性能观察。 | 2026-09-15 |
| [次级单位武器独立Excel维护 — 技术方案](../DevelopmentDocumentation/20260914-次级单位武器独立Excel维护.md) | combat, commander, data, wingman | 8/8 (100%) | 本次归并已交付；前次4项现有回归适配仍等待用户范围确认。 | 2026-09-14 |
| [地图资源密度涂绘与导出 — 技术方案与实施记录](../DevelopmentDocumentation/20260910-地图资源密度涂绘与导出.md) | data-pipeline, map-authoring, outpost, resource | 8/9 (89%) | 保存并重启旧 Editor，在隔离验证地图执行人工验收矩阵并记录证据。 | 2026-09-10 |
| [Ship僚机战斗表现、HUD与相机调整 — 技术方案](../DevelopmentDocumentation/20260910-Ship僚机战斗表现HUD与相机调整.md) | combat, ship, ui, vfx, wingman | — | 在交互式源码Editor中补做三种目标分辨率、分屏、贴地绕山、Listen/Dedicated/Late Join及10枚齐射GPU/Bounds截图验收。 | 2026-09-10 |
| [指挥官双机甲骨骼与武器挂点 — 技术方案](../DevelopmentDocumentation/20260906-指挥官双机甲骨骼与武器挂点.md) | assets, combat, commander, network, vfx | 7/12 (58%) | 完成主体、武器、四足/六足 FK 和 IK 控制，保留辅助结构 | 2026-09-07 |
| [指挥官 WM01 第二兵种与多 ISM 表现 — 技术方案](../DevelopmentDocumentation/20260904-指挥官WM01第二兵种与多ISM表现.md) | assets, combat, commander, network, ui | 9/12 (75%) | 在最终 WM01 资产和数据上运行获准的聚焦测试（本轮按要求停在测试阶段） | 2026-09-05 |
| [僚机无规则护航盘旋技能重构 — 开发文档](../DevelopmentDocumentation/20260904-僚机无规则护航盘旋技能重构.md) | combat, commander, network, ship, wingman | 11/13 (85%) | 向用户说明拟新增测试及文件，取得明确测试许可 | 2026-09-05 |
| [指挥官相机稳定巡航 — 技术方案](../DevelopmentDocumentation/20260904-指挥官相机稳定巡航.md) | commander, network, ship | 11/12 (92%) | 完成30/60/120 FPS、三臂长、16:9/超宽屏、18.5°坡道、50.5°坑壁、四角与小地图跳转的人工PIE矩阵 | 2026-09-04 |
| [公共战局框架与三类角色接入 — 技术方案](../DevelopmentDocumentation/20260831-公共战局框架与三类角色接入.md) | assets, combat, commander, network, ship | 12/13 (92%) | 全部网络验收门通过：最终 NetworkGate 的 ACK P95=138.1ms 达标，但未标记硬跳变 1 次，仍为 FAIL | 2026-08-31 |
| [指挥官小兵表现层两阶段性能优化 — 技术方案](../DevelopmentDocumentation/20260829-指挥官小兵表现层两阶段性能优化.md) | assets, commander, data-pipeline, network, ui | 8/18 (44%) | 实例到镜头实际距离 100000cm ± 1cm 的精确硬切边界 | 2026-08-30 |
| [指挥官 3C、Soldier 数据化与运行时 GM 调参 — 开发文档](../DevelopmentDocumentation/20260828-指挥官3C与运行时GM调参.md) | commander, data-pipeline, network, ship, ui | 9/10 (90%) | 在有画面的 PIE 中完成人工体验矩阵：四档相机高度、边角点选、25/50/100 人复杂导航、连续 Q/E 旋转与小地图点击 | 2026-08-28 |
| [GuLiStrike：Mass 双端同步架构草案 — 技术方案](../DevelopmentDocumentation/20260827-Mass双端同步架构草案.md) | assets, commander, network, ui | 42/46 (91%) | 协议版本/纪元拒绝门控已实现；仍需制造一次 v0.2/v0.3 不匹配连接并保存明确拒绝日志，完成后才勾选 | 2026-08-28 |

## 已完成

| 工作项 | 模块 | 任务 | 下一步 | 更新 |
|---|---|---|---|---|
| [GuLiStrike 美术规范 — 维护与验收台账](../DevelopmentDocumentation/GuLiStrike美术规范.md) | art, assets, rendering, vfx | 9/9 (100%) | 后续美术任务按规范制作，持续登记规则版本、资产例外与用户审核证据。 | 2026-09-20 |
| [轻型装甲地面玩家接入](../DevelopmentDocumentation/20260919-轻型装甲地面玩家接入.md) | art, combat, input, network | 8/8 (100%) | — | 2026-09-19 |
| [两组机甲资源风格统一参考 — 制作记录](../DevelopmentDocumentation/20260919-两组机甲资源风格统一参考.md) | art, assets, rendering | 7/8 (88%) | 查看整批UE展示；后续按反馈处理外观，性能和武器玩法另行接入。 | 2026-09-19 |
| [战争机器导弹范围与特效调整](../DevelopmentDocumentation/20260919-战争机器导弹范围与特效调整.md) | combat, commander, data, vfx | — | — | 2026-09-19 |
| [战争机器模型大小适配](../DevelopmentDocumentation/20260919-战争机器模型大小适配.md) | commander, data, navigation, performance | — | — | 2026-09-19 |
| [Mass单位体型与最小净距 — 运行时开发](../DevelopmentDocumentation/20260919-Mass单位体型与最小净距.md) | commander, data, navigation, performance | 12/12 (100%) | 已按用户指示撤回算法，转入WORK-20260919-002战争机器模型大小适配。 | 2026-09-19 |
| [指挥官单位体型与避障距离配置及接入分析](../DevelopmentDocumentation/20260918-指挥官单位体型与避障距离配置及接入分析.md) | commander, data, navigation | 5/5 (100%) | 后续先接入体型数据和合法生成，再完成Mass位置约束、工程车双向参与及显示预测限幅。 | 2026-09-18 |
| [战争机器Q导弹与通用地面预警 — 实施与验证](../DevelopmentDocumentation/20260917-战争机器Q导弹与通用地面预警.md) | combat, commander, network, ui, vfx | 8/8 (100%) | 单独处理两项既有回归失败；本次功能与崩溃修复已交付。 | 2026-09-17 |
| [Ship剩余四组件贴图与框线制作 — 实施与审核](../DevelopmentDocumentation/20260917-Ship剩余四组件贴图与框线制作.md) | art, assets, ship | 6/6 (100%) | 查看实际交付总览；现有舰体bottom_mid_0缺失和六个组件未配兼容槽位留作独立玩法工作。 | 2026-09-17 |
| [Ship第三批支援组件贴图与框线制作 — 实施与审核](../DevelopmentDocumentation/20260917-Ship第三批支援组件贴图与框线制作.md) | art, assets, ship | 6/6 (100%) | 查看实际交付总览；现有舰体bottom_mid_0缺失和六个组件未配兼容槽位留作独立玩法工作。 | 2026-09-17 |
| [Ship第二批三组件贴图与框线制作 — 实施与审核](../DevelopmentDocumentation/20260917-Ship第二批三组件贴图与框线制作.md) | art, assets, ship | 6/6 (100%) | 查看实际交付总览；现有舰体bottom_mid_0缺失和六个组件未配兼容槽位留作独立玩法工作。 | 2026-09-17 |
| [Ship三组件风格样板制作 — 实施与审核](../DevelopmentDocumentation/20260917-Ship三组件风格样板制作.md) | art, assets, ship | 12/12 (100%) | 查看实际交付总览；现有舰体bottom_mid_0缺失和六个组件未配兼容槽位留作独立玩法工作。 | 2026-09-17 |
| [进度面板分类筛选与文档分类元数据 — 技术方案](../DevelopmentDocumentation/20260917-进度面板分类筛选与文档分类元数据.md) | project | 8/8 (100%) | — | 2026-09-17 |
| [Ship动漫低模风格Blender样板 — 制作与交付](../DevelopmentDocumentation/20260916-Ship动漫低模风格Blender样板.md) | art, assets, ship | 4/4 (100%) | 等待用户美术评审；后续UE材质适配与模型替换另按确认范围实施。 | 2026-09-16 |
| [GPU渲染降耗 — 技术方案与实施](../DevelopmentDocumentation/20260916-GPU渲染降耗.md) | commander, performance, wingman | 9/9 (100%) | 继续归因未下降的ShadowDepths；后续实机游玩关注FXAA远距闪烁和快速转镜头下的Lumen收敛。 | 2026-09-16 |
| [据点矿厂落点与大规模避让修复 — 技术记录](../DevelopmentDocumentation/20260916-据点矿厂落点与大规模避让修复.md) | building, commander, navigation, performance | 7/7 (100%) | — | 2026-09-16 |
| [客户端 CPU 增量维护与 10Hz 刷新](../DevelopmentDocumentation/20260915-客户端CPU增量维护与10Hz刷新.md) | commander, network, performance, ui | 12/12 (100%) | — | 2026-09-16 |
| [导航预烘焙与哈希判新 — 技术方案](../DevelopmentDocumentation/20260915-导航预烘焙与哈希判新.md) | commander, navigation, performance, resources | 11/11 (100%) | — | 2026-09-15 |
| [指挥官姿态预测差分压缩与性能对比 — 技术方案](../DevelopmentDocumentation/20260915-指挥官姿态预测差分压缩与性能对比.md) | commander, network, performance | 9/9 (100%) | — | 2026-09-15 |
| [指挥官10Hz权威循环与移动容量压测 — 技术方案](../DevelopmentDocumentation/20260915-指挥官10Hz权威循环与移动容量压测.md) | commander, network, performance | 8/8 (100%) | 后续先解决初始名册分片与姿态带宽预算，再以打包分机、长时多轮条件复测部署容量。 | 2026-09-15 |
| [Ship组件能力与指挥官技能去GAS重构 — 实施记录](../DevelopmentDocumentation/20260914-Ship组件能力与指挥官技能去GAS重构.md) | combat, commander, network, ship, wingman | 8/8 (100%) | — | 2026-09-15 |
| [僚机短激光与炮弹池 — 技术方案](../DevelopmentDocumentation/20260914-僚机短激光与炮弹池.md) | combat, vfx, wingman | 7/7 (100%) | 后续另行处理既有失败断言；独立GPU计时与强制扩容未纳入本次实测。 | 2026-09-14 |
| [单位受击白光与摧毁爆炸 — 技术方案](../DevelopmentDocumentation/20260913-单位受击白光与摧毁爆炸.md) | combat, commander, resources, ship, vfx, wingman | 10/10 (100%) | — | 2026-09-14 |
| [Soldiers统一单位与全局法术场及矿车调度](../DevelopmentDocumentation/20260913-Soldiers统一单位与全局法术场及矿车调度.md) | combat, commander, data, resources, ship | 7/7 (100%) | 单独维护旧网络包预算和Ship快照测试夹具；跟踪场外绕矿簇减速及已有距离场ensure。 | 2026-09-13 |
| [激光采矿与矿车进出厂](../DevelopmentDocumentation/20260913-激光采矿与矿车进出厂.md) | resources | 7/7 (100%) | 等待矿车受击专项测试范围确认；保留距离场渲染警告供后续定位。 | 2026-09-13 |
| [Ship僚机对地轰炸与对空盘旋攻击 — 技术方案](../DevelopmentDocumentation/20260907-Ship僚机对地轰炸与对空盘旋攻击.md) | commander, network, ship, ui, wingman | 12/12 (100%) | — | 2026-09-13 |
| [ShipComponent正式部件蓝图 — 制作与迁移](../DevelopmentDocumentation/20260912-ShipComponent正式部件蓝图.md) | assets, ship | 6/6 (100%) | — | 2026-09-12 |
| [空战测试关卡远端地面单位 — 实施](../DevelopmentDocumentation/20260912-空战测试关卡远端地面单位.md) | commander, level, ship | 4/4 (100%) | 在原地图直接PIE体验；可在GroundUnits文件夹调整小队位置、数量和间距。 | 2026-09-12 |
| [通用敌方描边与普通PIE僚机跳变修复 — 技术方案](../DevelopmentDocumentation/20260912-通用敌方描边与普通PIE僚机跳变修复.md) | network, presentation, ship, wingman | 4/4 (100%) | 用户直接PIE复核手感；旧Coordinator限制测试迁移需另获授权，公网长期丢包表现未验证。 | 2026-09-12 |
| [僚机玩法门精简 — 技术方案](../DevelopmentDocumentation/20260912-僚机玩法门精简.md) | movement, networking, presentation, wingman | 8/8 (100%) | 待用户授权后更新已删除射程/归队门、v7金样和旧Pawn攻击状态的测试预期。 | 2026-09-12 |
| [僚机视觉插值与远端缓冲 — 技术方案](../DevelopmentDocumentation/20260912-僚机视觉插值与远端缓冲.md) | networking, presentation, wingman | 9/9 (100%) | 更新旧 `PerMemberAirGroundAttackExecution` 的空/地攻击状态预期后，重跑完整Pawn套件。 | 2026-09-12 |
| [红蓝矿棋盘与自动采矿闭环 — 技术方案](../DevelopmentDocumentation/20260911-红蓝矿棋盘与自动采矿闭环.md) | commander, economy, map, navigation, network | 15/15 (100%) | — | 2026-09-12 |
| [Ship 僚机对空攻击—盘旋冷却循环 — 技术方案](../DevelopmentDocumentation/20260911-Ship僚机对空攻击盘旋冷却循环.md) | combat, network, ship, wingman | 5/5 (100%) | — | 2026-09-11 |
| [飞船骨骼部件与Socket保留 — 技术方案](../DevelopmentDocumentation/20260911-飞船骨骼部件与Socket保留.md) | assets, combat, ship | 7/7 (100%) | — | 2026-09-11 |
| [指挥官双点传送技能 — 技术方案](../DevelopmentDocumentation/20260910-指挥官双点传送技能.md) | combat, commander, network, ship, ui, vfx, wingman | 8/8 (100%) | 用户自测最新范围、500米光柱和边缘渐变 | 2026-09-11 |
| [Ship 僚机三维往返缠斗与随机转向 — 技术方案与验证](../DevelopmentDocumentation/20260907-Ship僚机三维往返缠斗与随机转向.md) | combat, commander, network, ship, wingman | — | — | 2026-09-11 |
| [双矿单位矿模型 UE 导入](../DevelopmentDocumentation/20260910-双矿单位矿模型UE导入.md) | art, economy | 5/5 (100%) | 用户在UE中审核资源；后续另行确认矿量与采矿状态接入。 | 2026-09-10 |
| [双矿单位矿模型与 Blender 审核](../DevelopmentDocumentation/20260910-双矿单位矿模型与Blender审核.md) | art, economy | 7/7 (100%) | UE导入已按后续指令交付，用户可在UE中继续审核造型、材质和尺度。 | 2026-09-10 |
| [僚机飞行尾焰与拖尾 — 实施记录](../DevelopmentDocumentation/20260910-僚机飞行尾焰与拖尾.md) | ship, vfx, wingman | 7/7 (100%) | — | 2026-09-10 |
| [Ship僚机俯冲轰炸门槛简化 — 技术方案](../DevelopmentDocumentation/20260909-Ship僚机俯冲轰炸门槛简化.md) | combat, network, ship, wingman | 6/6 (100%) | — | 2026-09-09 |
| [Ship 僚机客户端 Pawn 与逐架 StateTree 全面重构 — 技术方案](../DevelopmentDocumentation/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md) | ai, combat, network, ship, wingman | 8/8 (100%) | 后续另立空战射界循环工作项，使持续目标场景的25个成员均完成至少两轮有效开火。 | 2026-09-09 |
| [资源加工厂正式模型与门动画 — 实施记录](../DevelopmentDocumentation/20260909-资源加工厂正式模型与门动画.md) | art, building | 13/13 (100%) | 使用 BP_ResourceProcessingFactory 的公开接口，后续另行接入建造菜单和加工玩法。 | 2026-09-09 |
| [Ship 僚机空地统一匈牙利自动选敌 — 技术方案](../DevelopmentDocumentation/20260908-Ship僚机空地统一匈牙利自动选敌.md) | combat, network, ship, wingman | 8/8 (100%) | — | 2026-09-09 |
| [Ship 空中部队原型关卡与三倍航速 — 技术方案与验证](../DevelopmentDocumentation/20260908-Ship空中部队原型关卡与三倍航速.md) | data-pipeline, level, navigation, ship, wingman | 13/13 (100%) | — | 2026-09-08 |
| [僚机体系、空中三维导航与客户端校验转发 — 技术方案](../DevelopmentDocumentation/20260902-僚机体系与空中三维导航.md) | combat, commander, network, ship, wingman | — | — | 2026-09-07 |
| [指挥官武器特效与独立法术场 — 技术方案](../DevelopmentDocumentation/20260905-指挥官武器特效与独立法术场.md) | commander, network, ship, vfx, wingman | 10/10 (100%) | — | 2026-09-06 |
| [据点巨构导入与占位替换 — 技术方案](../DevelopmentDocumentation/20260905-据点巨构导入与占位替换.md) | assets, building | 8/8 (100%) | — | 2026-09-05 |
| [WM01 程序化六足行走动画 — Blender 到 UE 完整管线教程](../DevelopmentDocumentation/20260826-WM01程序化六足行走动画-Blender到UE管线教程.md) | assets, commander, learning, network | — | — | 2026-09-05 |
| [Mass 框架启用与源码导读（UE 5.7）](../DevelopmentDocumentation/20260824-Mass框架启用与源码导读.md) | assets, combat, commander, network, wingman | — | — | 2026-09-05 |
| [飞船世界空间环绕 HUD 与技能准星 — 技术方案](../DevelopmentDocumentation/20260904-飞船世界空间环绕HUD与技能准星.md) | combat, commander, ship, ui, wingman | 8/8 (100%) | — | 2026-09-04 |
| [指挥官与地面战争机器最小建造系统 — 技术方案](../DevelopmentDocumentation/20260903-指挥官与地面战争机器最小建造系统.md) | building, combat, commander, network, ui | 10/10 (100%) | — | 2026-09-03 |
| [飞船 GAS 与僚机技能归属 — 开发文档](../DevelopmentDocumentation/20260902-飞船GAS与僚机技能归属.md) | combat, commander, network, ship, wingman | 15/16 (94%) | — | 2026-09-03 |
| [移动命令自由扩散与静态寻路线 — 技术方案](../DevelopmentDocumentation/20260901-移动命令自由扩散与静态寻路线.md) | combat, commander, network, ship, ui | 17/21 (81%) | — | 2026-09-01 |
| [小兵扫射与可扩展技能桥接 — 技术方案](../DevelopmentDocumentation/20260901-小兵扫射与指挥官GAS桥接.md) | commander, data-pipeline, network, ship, ui | 10/10 (100%) | — | 2026-09-01 |
| [指挥官精确选兵与快捷提示栏 — 技术方案](../DevelopmentDocumentation/20260831-指挥官精确选兵与快捷提示栏.md) | commander, ui | 15/15 (100%) | — | 2026-09-01 |
| [小兵客户端先行移动拖拽诊断 — 技术方案与证据](../DevelopmentDocumentation/20260831-小兵客户端先行移动拖拽诊断.md) | combat, commander, network, ship, ui | 9/9 (100%) | — | 2026-09-01 |
| [指挥官脚环优化与兵种面板 — 技术方案](../DevelopmentDocumentation/20260830-指挥官脚环优化与兵种面板.md) | combat, commander, network, ui | 9/9 (100%) | — | 2026-09-01 |
| [指挥官 UI 与小兵血条视觉设计：开发文档](../DevelopmentDocumentation/20260829-指挥官UI与小兵血条视觉设计.md) | assets, commander, data-pipeline, network, ui | 8/8 (100%) | — | 2026-08-31 |
| [指挥官 HUD 逻辑接入与批量小兵血条 — 技术方案](../DevelopmentDocumentation/20260829-指挥官HUD逻辑接入与批量小兵血条.md) | assets, commander, network, ui | 9/9 (100%) | — | 2026-08-31 |
| [飞船与场景模型尺寸归一 — 技术方案](../DevelopmentDocumentation/20260827-飞船与场景模型尺寸归一.md) | assets, network, ship | 9/9 (100%) | — | 2026-08-27 |
