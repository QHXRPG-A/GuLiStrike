# 当前工作

> 自动生成，请勿手改。运行 `progress_docs.py build` 刷新。

## 待确认需求

| 工作项 | 模块 | 任务 | 下一步 | 更新 |
|---|---|---|---|---|
| [指挥官兵种技能、Ship 武器通道与 Roguelike 升级归属 — 技术方案](../DevelopmentDocumentation/20260905-指挥官兵种技能与Roguelike升级归属.md) | commander, network, ship, ui, wingman | 9/18 (50%) | P1：稳定绑定、可扩展通道定义、兼容校验、启用/移除与完整 Loadout 版本；代码已覆盖 Army/Wingman，仍待 P3 协议定向验证 | 2026-09-07 |
| [非 Mass 大规模弹道与特效架构 — 技术方案](../DevelopmentDocumentation/20260906-非Mass大规模弹道与特效架构.md) | combat, commander, network, vfx, wingman | 0/17 (0%) | 用户确认范围假设：仅特效/弹道栈不用 Mass，不移除单位系统 Mass | 2026-09-06 |
| [据点混凝土巨构模型 — 技术方案](../DevelopmentDocumentation/20260905-据点混凝土巨构模型.md) | assets, building, combat, commander, vfx | 9/9 (100%) | — | 2026-09-05 |
| [基地建造玩法探索草案 v0.1](../RequirementDocument/20260831-基地建造玩法探索.md) | assets, building, combat, commander, ship | — | 玩家能说明选址和建筑选择的原因、放弃的其他投入，以及队友的作用 | 2026-08-31 |
| [GuLiStrike：5v5 大战场玩法策划草案](../RequirementDocument/20260826-5v5大战场玩法草案.md) | building, commander, network, ship, wingman | — | 操作者能够说明自己至少一次如何制造了可被指挥官兑现的突破 | 2026-08-26 |

## 规划中

暂无。

## 实施中

| 工作项 | 模块 | 任务 | 下一步 | 更新 |
|---|---|---|---|---|
| [地图战略点标注与数据导出工具 — 技术方案](../DevelopmentDocumentation/20260906-地图战略点标注与数据导出工具.md) | assets, building, commander, data-pipeline, map-authoring | 15/22 (68%) | M2 完整验收（实现完成，交互矩阵待验收） | 2026-09-07 |
| [DIY 飞船（模块化装配 + 飞行中热切换） — 技术方案](../DevelopmentDocumentation/20260820-DIY飞船.md) | assets, network, ship, ui | 28/29 (97%) | 用户手动加 7 个 socket（教程见下，坐标已定稿） | 2026-09-05 |
| [僚机世界空间近距编队与租约恢复 — 技术方案](../DevelopmentDocumentation/20260904-僚机世界空间近距编队与租约恢复.md) | network, ship, wingman | 8/10 (80%) | 在优化配置复跑完整 GuLiStrike.Wingman：DebugGame 已执行 79 项，仅既有 H4000 ServerValidator 性能预算失败 | 2026-09-04 |
| [指挥官与飞船共享 HUD 及 Ship UI v1 — 技术方案](../DevelopmentDocumentation/20260902-指挥官与飞船共享HUD及Ship UI v1.md) | assets, commander, network, ship, ui | 20/25 (80%) | 保留 Commander 全部既有数据、输入、MiniMap、Tooltip 和生命周期行为 | 2026-09-04 |
| [指挥官相机、编队导航与移动射击优化 — 技术方案](../DevelopmentDocumentation/20260901-指挥官相机编队导航与移动射击优化.md) | building, combat, commander, ui | 12/16 (75%) | 完成Server target构建；Launcher版UE5.7明确拒绝Server targets are not currently supported from this engine distribution，属于环境限制，需要源码版引擎或支持Server的发行环境 | 2026-09-01 |
| [数据管线：Excel 配置飞船数值 — 技术方案](../DevelopmentDocumentation/20260821-数据管线Excel配置.md) | assets, data-pipeline, ship | 15/15 (100%) | 无头 JSON→DataTable 导入对 FText/嵌套 FVector 的兼容性 → 降级 CSV 后端（导出脚本双格式输出） | 2026-08-25 |

## 待验收

| 工作项 | 模块 | 任务 | 下一步 | 更新 |
|---|---|---|---|---|
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
| [Ship 僚机客户端 Pawn 与逐架 StateTree 全面重构 — 技术方案](../DevelopmentDocumentation/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md) | ai, combat, network, ship, wingman | 8/8 (100%) | 后续另立空战射界循环工作项，使持续目标场景的25个成员均完成至少两轮有效开火。 | 2026-09-09 |
| [Ship 僚机空地统一匈牙利自动选敌 — 技术方案](../DevelopmentDocumentation/20260908-Ship僚机空地统一匈牙利自动选敌.md) | combat, network, ship, wingman | 8/8 (100%) | — | 2026-09-09 |
| [Ship 空中部队原型关卡与三倍航速 — 技术方案与验证](../DevelopmentDocumentation/20260908-Ship空中部队原型关卡与三倍航速.md) | data-pipeline, level, navigation, ship, wingman | 13/13 (100%) | — | 2026-09-08 |
| [Ship 僚机三维往返缠斗与随机转向 — 技术方案与验证](../DevelopmentDocumentation/20260907-Ship僚机三维往返缠斗与随机转向.md) | combat, commander, network, ship, wingman | — | — | 2026-09-08 |
| [Ship僚机对地轰炸与对空盘旋攻击 — 技术方案](../DevelopmentDocumentation/20260907-Ship僚机对地轰炸与对空盘旋攻击.md) | commander, network, ship, ui, wingman | 11/11 (100%) | — | 2026-09-07 |
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
