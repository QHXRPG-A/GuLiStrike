# 待验收与验证边界

> 自动生成，请勿手改。运行 `progress_docs.py build` 刷新。

| 工作项 | 阶段 | 验证 | 下一步 | 更新 |
|---|---|---|---|---|
| [指挥官白模据点占领与建筑体系 — 技术方案](../DevelopmentDocumentation/20260914-指挥官白模据点占领与建筑体系.md) | verification | partial | 补齐占领、自动进攻与人口回收边界验收，处理旧建筑测试合同和600单位性能问题。 | 2026-09-14 |
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
| [指挥官相机稳定巡航 — 技术方案](../DevelopmentDocumentation/20260904-指挥官相机稳定巡航.md) | verification | partial | 完成30/60/120 FPS、三臂长、16:9/超宽屏、18.5°坡道、50.5°坑壁、四角与小地图跳转的人工PIE矩阵 | 2026-09-04 |
| [僚机世界空间近距编队与租约恢复 — 技术方案](../DevelopmentDocumentation/20260904-僚机世界空间近距编队与租约恢复.md) | in_progress | partial | 在优化配置复跑完整 GuLiStrike.Wingman：DebugGame 已执行 79 项，仅既有 H4000 ServerValidator 性能预算失败 | 2026-09-04 |
| [指挥官与飞船共享 HUD 及 Ship UI v1 — 技术方案](../DevelopmentDocumentation/20260902-指挥官与飞船共享HUD及Ship UI v1.md) | in_progress | partial | 保留 Commander 全部既有数据、输入、MiniMap、Tooltip 和生命周期行为 | 2026-09-04 |
| [飞船 GAS 与僚机技能归属 — 开发文档](../DevelopmentDocumentation/20260902-飞船GAS与僚机技能归属.md) | done | partial | — | 2026-09-03 |
| [移动命令自由扩散与静态寻路线 — 技术方案](../DevelopmentDocumentation/20260901-移动命令自由扩散与静态寻路线.md) | done | partial | — | 2026-09-01 |
| [指挥官相机、编队导航与移动射击优化 — 技术方案](../DevelopmentDocumentation/20260901-指挥官相机编队导航与移动射击优化.md) | in_progress | partial | 完成Server target构建；Launcher版UE5.7明确拒绝Server targets are not currently supported from this engine distribution，属于环境限制，需要源码版引擎或支持Server的发行环境 | 2026-09-01 |
| [小兵扫射与可扩展技能桥接 — 技术方案](../DevelopmentDocumentation/20260901-小兵扫射与指挥官GAS桥接.md) | done | partial | — | 2026-09-01 |
| [指挥官精确选兵与快捷提示栏 — 技术方案](../DevelopmentDocumentation/20260831-指挥官精确选兵与快捷提示栏.md) | done | partial | — | 2026-09-01 |
| [小兵客户端先行移动拖拽诊断 — 技术方案与证据](../DevelopmentDocumentation/20260831-小兵客户端先行移动拖拽诊断.md) | done | partial | — | 2026-09-01 |
| [公共战局框架与三类角色接入 — 技术方案](../DevelopmentDocumentation/20260831-公共战局框架与三类角色接入.md) | verification | partial | 全部网络验收门通过：最终 NetworkGate 的 ACK P95=138.1ms 达标，但未标记硬跳变 1 次，仍为 FAIL | 2026-08-31 |
| [指挥官 HUD 逻辑接入与批量小兵血条 — 技术方案](../DevelopmentDocumentation/20260829-指挥官HUD逻辑接入与批量小兵血条.md) | done | partial | — | 2026-08-31 |
| [指挥官小兵表现层两阶段性能优化 — 技术方案](../DevelopmentDocumentation/20260829-指挥官小兵表现层两阶段性能优化.md) | verification | partial | 实例到镜头实际距离 100000cm ± 1cm 的精确硬切边界 | 2026-08-30 |
| [指挥官 3C、Soldier 数据化与运行时 GM 调参 — 开发文档](../DevelopmentDocumentation/20260828-指挥官3C与运行时GM调参.md) | verification | partial | 在有画面的 PIE 中完成人工体验矩阵：四档相机高度、边角点选、25/50/100 人复杂导航、连续 Q/E 旋转与小地图点击 | 2026-08-28 |
| [GuLiStrike：Mass 双端同步架构草案 — 技术方案](../DevelopmentDocumentation/20260827-Mass双端同步架构草案.md) | verification | partial | 协议版本/纪元拒绝门控已实现；仍需制造一次 v0.2/v0.3 不匹配连接并保存明确拒绝日志，完成后才勾选 | 2026-08-28 |
| [数据管线：Excel 配置飞船数值 — 技术方案](../DevelopmentDocumentation/20260821-数据管线Excel配置.md) | in_progress | partial | 无头 JSON→DataTable 导入对 FText/嵌套 FVector 的兼容性 → 降级 CSV 后端（导出脚本双格式输出） | 2026-08-25 |
