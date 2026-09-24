# 待验收与验证边界

> 自动生成，请勿手改。运行 `progress_docs.py build` 刷新。

| 工作项 | 阶段 | 验证 | 下一步 | 更新 |
|---|---|---|---|---|
| [Mass历史插值与三倍速度纠偏](../DevelopmentDocumentation/20260923-Mass历史插值与三倍速度纠偏.md) | verification | partial | 定位位置队列长等待中引擎补额、保守预留与命令状态屏障的占比；独立追查导航单步跳点，保留晚加入/重连及紧急状态专项验收。 | 2026-09-23 |
| [游戏左上角帧率与延迟显示](../DevelopmentDocumentation/20260923-游戏左上角帧率与延迟显示.md) | verification | partial | 由玩家核对原地图左上角显示、窗口适配和RTT；不自动启动PIE。 | 2026-09-23 |
| [导航内存优化与对局容量预算](../DevelopmentDocumentation/20260922-导航内存优化与对局容量预算.md) | verification | partial | 玩家核对连续纠偏与HUD；服务端异常XY位移、姿态供给及专服容量测量继续后续处理。 | 2026-09-23 |
| [三张视差3D卡牌交互演示 — 技术方案](../DevelopmentDocumentation/20260922-三张视差3D卡牌交互演示.md) | verification | partial | 在 LVL_CardRevealDemo 体验两次点击流程、±12° 悬停以及窗口失焦回正，确认视觉与手感。 | 2026-09-22 |
| [指挥官部队StateTree接入与特殊任务退役 — 技术方案](../DevelopmentDocumentation/20260921-指挥官部队StateTree接入与特殊任务退役.md) | verification | partial | 在LVL_CommanderMassPrototype补齐矿位、施工、改令及通道异常矩阵，再完成新逻辑10/30台负载与30分钟性能验收，并收集玩家效果反馈。 | 2026-09-22 |
| [地面机甲辅助瞄准 — 技术方案](../DevelopmentDocumentation/20260921-地面机甲辅助瞄准.md) | verification | partial | 玩家在既有FireReview观察射向修正、最近目标切换、离圈自由瞄准及鼠标位置不变。 | 2026-09-21 |
| [FireReview双阵营靶场与敌方描边 — 技术方案](../DevelopmentDocumentation/20260921-FireReview双阵营靶场与敌方描边.md) | done | partial | 玩家在/Game/Maps/LVL_GroundMech_FireReview确认两队待命、敌方红色描边及受击销毁效果。 | 2026-09-21 |
| [地面机甲动画蓝图与空中战斗 — 技术方案](../DevelopmentDocumentation/20260921-地面机甲动画蓝图与空中战斗.md) | done | partial | 按后续实际使用反馈调整；未覆盖专项和容量条视觉终验见本文边界。 | 2026-09-21 |
| [地面机甲火箭跳 — 实施与验证](../DevelopmentDocumentation/20260920-地面机甲火箭跳.md) | done | partial | 容量条单独视觉终验及未覆盖专项保留在验收边界；后续按实际反馈调整。 | 2026-09-21 |
| [星际UI拆解与指挥官界面重构 — 技术方案](../DevelopmentDocumentation/20260920-星际UI拆解与指挥官界面重构.md) | done | partial | 补齐运输全阶段界面、重连后头像交互的人工验收；既有血条断言不在本轮修改范围。 | 2026-09-21 |
| [玩法地图对齐Demo光照](../DevelopmentDocumentation/20260918-玩法地图对齐Demo光照.md) | done | partial | 用户查看实际PIE截图确认明暗；战争机器拥挤按独立诊断决定后续实施范围。 | 2026-09-21 |
| [游戏对象与效果统一缩放至0.2倍 — 实施记录](../DevelopmentDocumentation/20260918-游戏对象与效果统一缩放至0.2倍.md) | done | partial | 继续其余0.2倍真实运行门禁（Mass、Ship/僚机、建造、空运、护盾占领传送）及用户视觉验收 | 2026-09-21 |
| [松树林原地图全资源风格重构 — 实施记录](../DevelopmentDocumentation/20260918-松树林原地图全资源风格重构.md) | done | partial | 等待用户审核原Demo_Map整图；按反馈修订，动态录像、GPU性能及编辑器崩溃根因不冒充已验证。 | 2026-09-21 |
| [Q导弹圆面打击与赠品建筑防卡死 — 技术方案](../DevelopmentDocumentation/20260918-Q导弹圆面打击与赠品建筑防卡死.md) | done | failed | 定位原LogUtils错误的实际调用者；复核工程车辆/玩家清场、受阻改选及边界越程等未覆盖行为。 | 2026-09-21 |
| [松树林与三单位同场景对照 — 试摆记录](../DevelopmentDocumentation/20260917-松树林与三单位同场景对照.md) | done | partial | 在WORK-20260918-002完成原地图全资源适配与整图终验。 | 2026-09-21 |
| [地面机枪飞行弹丸与僚机弹效复用](../DevelopmentDocumentation/20260917-地面机甲弹幕肉鸽与塔防建造.md) | in_progress | partial | 跟踪既有导弹夹具和挂点断言失配；地面弹丸、配色与5Hz专项留待后续安排。 | 2026-09-21 |
| [GuLiStrike 自然地编资源包 — 制作与验收](../DevelopmentDocumentation/20260917-自然地编资源包.md) | done | partial | 用户审核v3工艺及外形参考后，以图集和卡片制作六件Blender代表样板。 | 2026-09-21 |
| [Ship导入与扫荡者战争机器风格重制 — 实施](../DevelopmentDocumentation/20260916-Ship导入与扫荡者战争机器风格重制.md) | done | partial | 继续实战轰炸、僚机死亡、机械姿态和完整性能验收。 | 2026-09-21 |
| [僚机对地轰炸动漫爆炸样板 — 实施与验收](../DevelopmentDocumentation/20260916-僚机对地轰炸动漫爆炸样板.md) | done | partial | 用户先在 UE 评审视觉；随后补齐稳定 GPU 对照、尺寸精测和真实客户端/专用服务器验收。 | 2026-09-21 |
| [指挥官相机稳定巡航 — 技术方案](../DevelopmentDocumentation/20260904-指挥官相机稳定巡航.md) | done | partial | 完成30/60/120 FPS、三臂长、16:9/超宽屏、18.5°坡道、50.5°坑壁、四角与小地图跳转的人工PIE矩阵 | 2026-09-21 |
| [僚机世界空间近距编队与租约恢复 — 技术方案](../DevelopmentDocumentation/20260904-僚机世界空间近距编队与租约恢复.md) | done | partial | 在优化配置复跑完整 GuLiStrike.Wingman：DebugGame 已执行 79 项，仅既有 H4000 ServerValidator 性能预算失败 | 2026-09-21 |
| [指挥官与飞船共享 HUD 及 Ship UI v1 — 技术方案](../DevelopmentDocumentation/20260902-指挥官与飞船共享HUD及Ship UI v1.md) | done | partial | 保留 Commander 全部既有数据、输入、MiniMap、Tooltip 和生命周期行为 | 2026-09-21 |
| [指挥官相机、编队导航与移动射击优化 — 技术方案](../DevelopmentDocumentation/20260901-指挥官相机编队导航与移动射击优化.md) | done | partial | 完成Server target构建；Launcher版UE5.7明确拒绝Server targets are not currently supported from this engine distribution，属于环境限制，需要源码版引擎或支持Server的发行环境 | 2026-09-21 |
| [指挥官小兵表现层两阶段性能优化 — 技术方案](../DevelopmentDocumentation/20260829-指挥官小兵表现层两阶段性能优化.md) | done | partial | 实例到镜头实际距离 100000cm ± 1cm 的精确硬切边界 | 2026-09-21 |
| [指挥官 3C、Soldier 数据化与运行时 GM 调参 — 开发文档](../DevelopmentDocumentation/20260828-指挥官3C与运行时GM调参.md) | done | partial | 在有画面的 PIE 中完成人工体验矩阵：四档相机高度、边角点选、25/50/100 人复杂导航、连续 Q/E 旋转与小地图点击 | 2026-09-21 |
| [GuLiStrike：Mass 双端同步架构草案 — 技术方案](../DevelopmentDocumentation/20260827-Mass双端同步架构草案.md) | done | partial | 协议版本/纪元拒绝门控已实现；仍需制造一次 v0.2/v0.3 不匹配连接并保存明确拒绝日志，完成后才勾选 | 2026-09-21 |
| [数据管线：Excel 配置飞船数值 — 技术方案](../DevelopmentDocumentation/20260821-数据管线Excel配置.md) | done | partial | 无头 JSON→DataTable 导入对 FText/嵌套 FVector 的兼容性 → 降级 CSV 后端（导出脚本双格式输出） | 2026-09-21 |
| [玩家地面机甲与Mass单位立体碰撞 — 技术方案与实施记录](../DevelopmentDocumentation/20260920-玩家地面机甲与Mass单位立体碰撞.md) | verification | partial | 完成Listen/Dedicated真实接触验收并记录仍未覆盖的视觉与网络条件。 | 2026-09-20 |
| [玩家地面机甲开火与Excel升级配置](../DevelopmentDocumentation/20260920-玩家地面机甲开火与升级配置.md) | verification | partial | 用户审核FireReview的枪口、弹道及缓冲后坐；批准后将已验证配置接到正式BP_GroundMech_Light。 | 2026-09-20 |
| [两组机甲资源风格统一参考 — 制作记录](../DevelopmentDocumentation/20260919-两组机甲资源风格统一参考.md) | done | partial | 查看整批UE展示；后续按反馈处理外观，性能和武器玩法另行接入。 | 2026-09-19 |
| [战争机器模型大小适配](../DevelopmentDocumentation/20260919-战争机器模型大小适配.md) | done | partial | — | 2026-09-19 |
| [Mass单位体型与最小净距 — 运行时开发](../DevelopmentDocumentation/20260919-Mass单位体型与最小净距.md) | done | partial | 已按用户指示撤回算法，转入WORK-20260919-002战争机器模型大小适配。 | 2026-09-19 |
| [战争机器Q导弹与通用地面预警 — 实施与验证](../DevelopmentDocumentation/20260917-战争机器Q导弹与通用地面预警.md) | done | partial | 单独处理两项既有回归失败；本次功能与崩溃修复已交付。 | 2026-09-17 |
| [Ship剩余四组件贴图与框线制作 — 实施与审核](../DevelopmentDocumentation/20260917-Ship剩余四组件贴图与框线制作.md) | done | partial | 查看实际交付总览；现有舰体bottom_mid_0缺失和六个组件未配兼容槽位留作独立玩法工作。 | 2026-09-17 |
| [Ship第三批支援组件贴图与框线制作 — 实施与审核](../DevelopmentDocumentation/20260917-Ship第三批支援组件贴图与框线制作.md) | done | partial | 查看实际交付总览；现有舰体bottom_mid_0缺失和六个组件未配兼容槽位留作独立玩法工作。 | 2026-09-17 |
| [Ship第二批三组件贴图与框线制作 — 实施与审核](../DevelopmentDocumentation/20260917-Ship第二批三组件贴图与框线制作.md) | done | partial | 查看实际交付总览；现有舰体bottom_mid_0缺失和六个组件未配兼容槽位留作独立玩法工作。 | 2026-09-17 |
| [Ship三组件风格样板制作 — 实施与审核](../DevelopmentDocumentation/20260917-Ship三组件风格样板制作.md) | done | partial | 查看实际交付总览；现有舰体bottom_mid_0缺失和六个组件未配兼容槽位留作独立玩法工作。 | 2026-09-17 |
| [GPU渲染降耗 — 技术方案与实施](../DevelopmentDocumentation/20260916-GPU渲染降耗.md) | done | partial | 继续归因未下降的ShadowDepths；后续实机游玩关注FXAA远距闪烁和快速转镜头下的Lumen收敛。 | 2026-09-16 |
| [指挥官白模据点占领与建筑体系 — 技术方案](../DevelopmentDocumentation/20260914-指挥官白模据点占领与建筑体系.md) | in_progress | partial | 补齐包围、厂内延后指令及运输边界观察；坡道断言已由9月16日落点修复处理。 | 2026-09-16 |
| [工程车地面动态避障 — 技术方案](../DevelopmentDocumentation/20260915-工程车地面动态避障.md) | verification | partial | 后续确认旧BuildingWorld测试更新范围，补密集狭路及大规模性能观察。 | 2026-09-15 |
| [指挥官10Hz权威循环与移动容量压测 — 技术方案](../DevelopmentDocumentation/20260915-指挥官10Hz权威循环与移动容量压测.md) | done | partial | 后续先解决初始名册分片与姿态带宽预算，再以打包分机、长时多轮条件复测部署容量。 | 2026-09-15 |
| [僚机短激光与炮弹池 — 技术方案](../DevelopmentDocumentation/20260914-僚机短激光与炮弹池.md) | done | partial | 后续另行处理既有失败断言；独立GPU计时与强制扩容未纳入本次实测。 | 2026-09-14 |
| [次级单位武器独立Excel维护 — 技术方案](../DevelopmentDocumentation/20260914-次级单位武器独立Excel维护.md) | verification | partial | 本次归并已交付；前次4项现有回归适配仍等待用户范围确认。 | 2026-09-14 |
| [单位受击白光与摧毁爆炸 — 技术方案](../DevelopmentDocumentation/20260913-单位受击白光与摧毁爆炸.md) | done | partial | — | 2026-09-14 |
| [Soldiers统一单位与全局法术场及矿车调度](../DevelopmentDocumentation/20260913-Soldiers统一单位与全局法术场及矿车调度.md) | done | partial | 单独维护旧网络包预算和Ship快照测试夹具；跟踪场外绕矿簇减速及已有距离场ensure。 | 2026-09-13 |
| [激光采矿与矿车进出厂](../DevelopmentDocumentation/20260913-激光采矿与矿车进出厂.md) | done | partial | 等待矿车受击专项测试范围确认；保留距离场渲染警告供后续定位。 | 2026-09-13 |
| [通用敌方描边与普通PIE僚机跳变修复 — 技术方案](../DevelopmentDocumentation/20260912-通用敌方描边与普通PIE僚机跳变修复.md) | done | partial | 用户直接PIE复核手感；旧Coordinator限制测试迁移需另获授权，公网长期丢包表现未验证。 | 2026-09-12 |
| [僚机玩法门精简 — 技术方案](../DevelopmentDocumentation/20260912-僚机玩法门精简.md) | done | partial | 待用户授权后更新已删除射程/归队门、v7金样和旧Pawn攻击状态的测试预期。 | 2026-09-12 |
| [僚机视觉插值与远端缓冲 — 技术方案](../DevelopmentDocumentation/20260912-僚机视觉插值与远端缓冲.md) | done | partial | 更新旧 `PerMemberAirGroundAttackExecution` 的空/地攻击状态预期后，重跑完整Pawn套件。 | 2026-09-12 |
| [指挥官双点传送技能 — 技术方案](../DevelopmentDocumentation/20260910-指挥官双点传送技能.md) | done | partial | 用户自测最新范围、500米光柱和边缘渐变 | 2026-09-11 |
| [地图资源密度涂绘与导出 — 技术方案与实施记录](../DevelopmentDocumentation/20260910-地图资源密度涂绘与导出.md) | verification | partial | 保存并重启旧 Editor，在隔离验证地图执行人工验收矩阵并记录证据。 | 2026-09-10 |
| [游戏内 GM 分页浮层面板 — 技术方案](../DevelopmentDocumentation/20260910-游戏内GM分页浮层面板.md) | in_progress | partial | 重启源码版 Editor，完成三分辨率、Commander/Ground/Air、Standalone/Listen/客户端的人工 PIE 验收。 | 2026-09-10 |
| [僚机飞行尾焰与拖尾 — 实施记录](../DevelopmentDocumentation/20260910-僚机飞行尾焰与拖尾.md) | done | partial | — | 2026-09-10 |
| [Ship僚机战斗表现、HUD与相机调整 — 技术方案](../DevelopmentDocumentation/20260910-Ship僚机战斗表现HUD与相机调整.md) | verification | partial | 在交互式源码Editor中补做三种目标分辨率、分屏、贴地绕山、Listen/Dedicated/Late Join及10枚齐射GPU/Bounds截图验收。 | 2026-09-10 |
| [Ship 僚机客户端 Pawn 与逐架 StateTree 全面重构 — 技术方案](../DevelopmentDocumentation/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md) | done | partial | 后续另立空战射界循环工作项，使持续目标场景的25个成员均完成至少两轮有效开火。 | 2026-09-09 |
| [指挥官双机甲骨骼与武器挂点 — 技术方案](../DevelopmentDocumentation/20260906-指挥官双机甲骨骼与武器挂点.md) | verification | partial | 完成主体、武器、四足/六足 FK 和 IK 控制，保留辅助结构 | 2026-09-07 |
| [地图战略点标注与数据导出工具 — 技术方案](../DevelopmentDocumentation/20260906-地图战略点标注与数据导出工具.md) | in_progress | partial | M2 完整验收（实现完成，交互矩阵待验收） | 2026-09-07 |
| [指挥官兵种技能、Ship 武器通道与 Roguelike 升级归属 — 技术方案](../DevelopmentDocumentation/20260905-指挥官兵种技能与Roguelike升级归属.md) | draft | partial | P1：稳定绑定、可扩展通道定义、兼容校验、启用/移除与完整 Loadout 版本；代码已覆盖 Army/Wingman，仍待 P3 协议定向验证 | 2026-09-07 |
| [非 Mass 大规模弹道与特效架构 — 技术方案](../DevelopmentDocumentation/20260906-非Mass大规模弹道与特效架构.md) | draft | partial | 用户确认范围假设：仅特效/弹道栈不用 Mass，不移除单位系统 Mass | 2026-09-06 |
| [指挥官 WM01 第二兵种与多 ISM 表现 — 技术方案](../DevelopmentDocumentation/20260904-指挥官WM01第二兵种与多ISM表现.md) | verification | partial | 在最终 WM01 资产和数据上运行获准的聚焦测试（本轮按要求停在测试阶段） | 2026-09-05 |
| [僚机无规则护航盘旋技能重构 — 开发文档](../DevelopmentDocumentation/20260904-僚机无规则护航盘旋技能重构.md) | verification | partial | 向用户说明拟新增测试及文件，取得明确测试许可 | 2026-09-05 |
| [WM01 程序化六足行走动画 — Blender 到 UE 完整管线教程](../DevelopmentDocumentation/20260826-WM01程序化六足行走动画-Blender到UE管线教程.md) | done | partial | — | 2026-09-05 |
| [DIY 飞船（模块化装配 + 飞行中热切换） — 技术方案](../DevelopmentDocumentation/20260820-DIY飞船.md) | in_progress | partial | 用户手动加 7 个 socket（教程见下，坐标已定稿） | 2026-09-05 |
| [飞船世界空间环绕 HUD 与技能准星 — 技术方案](../DevelopmentDocumentation/20260904-飞船世界空间环绕HUD与技能准星.md) | done | partial | — | 2026-09-04 |
| [飞船 GAS 与僚机技能归属 — 开发文档](../DevelopmentDocumentation/20260902-飞船GAS与僚机技能归属.md) | done | partial | — | 2026-09-03 |
| [移动命令自由扩散与静态寻路线 — 技术方案](../DevelopmentDocumentation/20260901-移动命令自由扩散与静态寻路线.md) | done | partial | — | 2026-09-01 |
| [小兵扫射与可扩展技能桥接 — 技术方案](../DevelopmentDocumentation/20260901-小兵扫射与指挥官GAS桥接.md) | done | partial | — | 2026-09-01 |
| [指挥官精确选兵与快捷提示栏 — 技术方案](../DevelopmentDocumentation/20260831-指挥官精确选兵与快捷提示栏.md) | done | partial | — | 2026-09-01 |
| [小兵客户端先行移动拖拽诊断 — 技术方案与证据](../DevelopmentDocumentation/20260831-小兵客户端先行移动拖拽诊断.md) | done | partial | — | 2026-09-01 |
| [公共战局框架与三类角色接入 — 技术方案](../DevelopmentDocumentation/20260831-公共战局框架与三类角色接入.md) | verification | partial | 全部网络验收门通过：最终 NetworkGate 的 ACK P95=138.1ms 达标，但未标记硬跳变 1 次，仍为 FAIL | 2026-08-31 |
| [指挥官 HUD 逻辑接入与批量小兵血条 — 技术方案](../DevelopmentDocumentation/20260829-指挥官HUD逻辑接入与批量小兵血条.md) | done | partial | — | 2026-08-31 |
