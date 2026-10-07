# 先驱号委托 PIE 验收

本报告为五倍步态减速前的功能验收。用户后续反馈腿部过快，四向播放速率已调为 0.2，移动目标复用半径改为 1 m；新参数已保存并在其现有 PIE 中更新。[后续调参与验证范围](../../../../Progress/DevelopmentDocumentation/20261003-先驱号步态减速与移动点击精度.md)记录当前状态，不将下文通过写成新观感批准。

2026-10-03，按用户“这次你帮我验收”的明确委托执行。**本地权威 PIE 核心功能 13 项通过**，运行中发现的四个问题已修复并回归。最新 Editor 编译与重载通过，PIE 已结束，`/Game/Maps/LVL_CommanderMassPrototype` 已保存，烘焙验证有效，未保存资源包为零。

本报告记录助手实际执行的功能验收。美术沿用用户批准的 B-v1；未替代 A/B 审核，冻结源文件未变。[结构化结果](Reports/qa_acceptance_summary.json)可由 `Scripts/Pioneer/summarize_acceptance.py` 按原始证据重新汇总。

## 实测结果

| 项目 | 结果与证据 |
|---|---|
| 身份、体型、属性 | ID 1 / DefaultSoldier / 先驱号；玩法体宽 625 cm、避障半径 312.5 cm、100 生命、0 防御。[实测](Reports/qa_v2_single_readback.json)、[正式表回读](Reports/ue_runtime_readback.json) |
| 单台 Q | 通过正常选择入口选中单位，向真实 Controller 发送 Q 按下/松开；生成五个 ID 5 扫荡者，初生待机。[结果](Reports/qa_v2_single_readback.json) |
| 冷却、累积 | 冷却期间拒绝且不新增；30 秒后同一台再次生成五个并保留前批。[冷却](Reports/qa_v2_cooldown_readback.json)、[累积](Reports/qa_v2_accumulation_readback.json) |
| 混选、独立冷却 | 两台先驱号各五个；混选扫荡者返回 NoSkill。只给其中一台施法，另一台仍可用。[混选](Reports/qa_v2_multi_readback.json)、[独立冷却](Reports/qa_v2_independent_readback.json) |
| 相同请求重发 | 从服务器只读回执复制完整原 Q 请求，重发相同 ID 和内容，返回相同成功结果；扫荡者仍为 ID 8–12，人口和冷却均未变化。[最终补测](Reports/qa_v5_exact_duplicate_call.json) |
| 受阻整批失败 | 最新构建中，保存的四堵墙使召唤零新增、零冷却；只移除 PIE 世界中的墙后立即生成五个。[受阻](Reports/qa_v4_blocked_readback.json)、[解除](Reports/qa_v4_unblocked_readback.json) |
| 最终 Q 回归 | 碰撞修复后两台 Q 从五个增至十五个，新增十个全部初生待机，生产人口与预占不增加。[结果](Reports/qa_v4_multi_readback.json) |
| 存续、控制、人口 | 累积 35 个召唤兵时，生产人口 101 恰等于存活本方非召唤兵；预占为零。施法者死亡后 35 个仍存活，选中 ID 9 后继续移动。[人口](Reports/qa_v2_overview_readback.json)、[死亡后控制](Reports/qa_v2_final.json) |
| 实际移动速度 | 先驱号、重防号、扫荡者最高水平速度分别为 **1440 / 720 / 720 cm/s**。绕行和避让会减速；先驱号全程 79 个移动样本中位数约 1160.6 cm/s，不将瞬时避让速度视为基础配置。[采样汇总](Reports/qa_acceptance_summary.json) |
| 双枪、伤害、散布 | 五轮十个唯一 ShotId，每轮左右各一颗；轮间隔 0.9951–1.0075 秒，每发配置 10 伤害，目标 300.5→240.5，六次命中共 60。十发最大离轴 **0.946°**，符合总锥角 2°；弹丸与同源枪口事件起点差 **0 cm**。[记录](Reports/qa_v2_shooting_early.json) |
| 射程、召唤兵武器 | 先驱号对 56 m 目标开火，70.04 m 目标保持满血且无先驱号攻击记录。扫荡者受控靠近后以 20 m 单枪开火，每发 10 伤害。[射程外](Reports/qa_v2_wait_readback.json)、[扫荡者战斗](Reports/qa_v2_overview_readback.json) |
| VAT 与死亡 | 实际 Mass 为 StaticMesh ISM，59 个自定义槽，骨骼网格组件始终为零。采样覆盖待机、前后左右及死亡帧段；最新静态网格残骸实际 VAT 帧 190.3147。[死亡实例](Reports/qa_v4_death_isolation.json) |
| 编译、资源、保存 | 正式资源回读通过；最新 `GuLiStrikeEditor Win64 Development` 构建 17 个动作、36.20 秒、Succeeded，重开加载后完成重发补测。七部署点、四墙已保存，启动烘焙有效。[构建](Reports/qa_exact_dedup_build.log)、[最终状态](Reports/qa_editor_final_state.json) |

默认解锁、免费召唤与 30 秒配置经正式技能资产和源码核对；没有单独隔离经济收入做扣费计量。正常生产与开局使用 ID 1，ID 5 为仅召唤兵。既有工厂、赠兵会增加普通单位，因此按 ID 5 差额和非召唤人口判断 Q，不以世界总实体增量代替召唤数量。

## 修复与回归

1. **原型地图无法启动。** 资源烘焙校验器只接受默认 500 人布局，拒绝七个实际部署点。作者工具现按运行时相同尺寸与间距公式校验部署点；没有部署点的地图保留默认路径。重新烘焙并实际启动成功：49 区域、240 簇、6240 节点，七个初始兵通过。[烘焙](Reports/qa_bake_result.json)
2. **各兵种共享 1440 cm/s。** 全局移动调节误当作所有兵种的基础速度。现以各兵种表速度为基准，再应用既有全局和肉鸽倍率，实测 1440/720/720。
3. **召唤兵自动推进。** 默认任务树初始化含自动推进。批量召唤改为初生停止状态，手动移动正常恢复任务，自动索敌继续工作。
4. **墙内候选被接受。** 原通道阻挡检测未拒绝实测已与墙重叠的候选。现收集实际形状重叠，再核对组件对 Pawn 的阻挡响应；预检查和提交使用同一函数。最终保存墙体验证零或五个，失败不扣冷却。

另修正验收工具：旧 `qa_v2_duplicate_q.json` 遗漏原 Q 的地面命中上下文，实际证明“同 ID、不同内容”拒绝。旧记录保留；缓存成功结论以最新完整请求重发为准。

前篇 `ARC-20261003-007` 的编译、资源回读事实仍有效；“入口就绪”只基于静态实体核对，未覆盖实际 PIE 启动守卫。本次真实启动暴露并修复遗漏，以本报告和新归档为当前状态。

## 运行画面与资产边界

下列图来自真实 PIE 世界，未用展示模型替代单位，也未手工改写运行 VAT 姿态。

- [真实近景](ReviewImages/PIE_Pioneer_Live_Hero.png)：暖白、珊瑚红、天蓝背环与头部凹面、浅黄头球、三档明暗与结构线。
- [名称、头像、100 HP、14.4 m/s、0 防御](ReviewImages/PIE_Pioneer_UI_Q.png)。
- [战术镜头](ReviewImages/PIE_Pioneer_Tactical.png)：画面采样离地约 307.1 m，300 m 为目标档位。
- [实际总览](ReviewImages/PIE_Pioneer_Overview.png)：按战场范围动态计算，本次高度约 6144.1 m。
- [真实死亡 VAT 网格](ReviewImages/PIE_Pioneer_Death_VAT_Only.png)：仅隔离其他模型和特效，保留实际残骸帧；完整死亡截图被爆炸遮挡，未作模型形态依据。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。

B-v1 Blender SHA256 再核对为 `3b330e2d2e84bb464b904aa2bb77e2043e79489c226c8d39815c8fe11f19a887`，未变。44 通道、311 帧、七片段为离线数据，兼容骨架与动画不属于 Mass 依赖。原动作接地偏差、落地约 59.3 m 源位移、重防号 12.5 m 配置宽与约 13.98 m 网格宽的差异继续留档。

## 未执行范围

- 10000 实体容量满载与提交中途故障注入；容量保护、原子提交、回滚仅做源码审查。
- 独立服务端与多客户端协议 23 联机；本次为本地权威 Standalone PIE。
- 实战帧率、GPU 性能基准和打包；截图即时 FPS 不作性能验收。

这些边界没有标为通过。核心功能验收完成，扩展范围需另行记录测试规模和结果。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。
