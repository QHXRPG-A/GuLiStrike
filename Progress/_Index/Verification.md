# 待验收与验证边界

> 自动生成，请勿手改。运行 `progress_docs.py build` 刷新。

| 工作项 | 阶段 | 验证 | 下一步 | 更新 |
|---|---|---|---|---|
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
