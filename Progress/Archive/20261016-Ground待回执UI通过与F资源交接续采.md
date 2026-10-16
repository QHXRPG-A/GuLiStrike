---
schema: guli-progress/v1
id: ARC-20261016-024
work_id: ''
kind: archive
role: root
title: Ground待回执UI通过与F资源交接续采
areas: [network, commander, combat]
categories: [gameplay]
status: recorded
verification: partial
created: '2026-10-16'
updated: '2026-10-16'
summary: R119业务幂等与Commander/Air UI、R120 Ground UI、R121资源能力/待回执/新生对象交接均独立通过且哈希一致；跨基线F18项补采齐备。r136退出绘制生命周期收尾编译通过，R122开始冻结基线完整G→F。
next_action: 完成R122同基线完整G→F，按原断言、阶段和哈希判定；保留首次失败，玩家与性能单列。
relations:
  work_items: [WORK-20261011-002]
  related: [ARC-20261016-023]
status_note: F18项目录覆盖跨不同基线，不能登记完整F/G通过；R119 Ground重启后退出的根因未确认。r136离席修改的效果待完整复测。
---

R119目录`D35-F-R119-r135-IdempotenceUIRegression/Hybrid-3v3-n600-r1-6ee8e011`通过业务幂等与Commander/Air待回执UI两项F必需断言。[审计](../../TestResults/HybridFullAcceptance/D35-Implementation/r135-r119-fault-collection-audit.json)哈希一致，8次原请求回放得到8次重复结果，完整数值和不可变决定哈希不变。两个职责均有原请求、重绑定后同事件、最终实际回执、一次完成与等待解除。后续切到Ground时，新PID47108已完成私有恢复但在进入Ground UI前退出，剩余依赖停止，不判定Ground UI失败。末次状态无原生失败字符串，导航尚在准备；[恢复与中断记录](../../TestResults/HybridFullAcceptance/D35-Implementation/r135-r119-bootstrap-and-interruption.json)保留根因未确认，不推断为GPU错误。实际加入端最大恢复约113755021展开字节、峰值暂存13801372字节，完成7.22秒，房间未记15秒期限失败；这证明展开总量大于64MiB，不等于已经复现单一来源大于64MiB的历史场景。

按用户继续先采集、准备金手指和短预算跳过的要求，R120直接以Ground作为初始唯一渲染职责，目录`D35-F-R120-r135-GroundUIPendingCollection/Hybrid-3v3-n600-r1-5889ed3d`。Ground原放置事件跨真实HUD/订阅重绑定后得到最终回执，只完成一次且等待解除；[审计](../../TestResults/HybridFullAcceptance/D35-Implementation/r135-r120-fault-collection-audit.json)哈希一致，阶段通过且forced_cleanup为空。它是Ground UI专项证据，不代替R119未完成的长房间渲染职责切换。

库存前置复用原`fixture_position`，把矿车摆到其已接受任务实际预约的工作位：从本地模拟器读回正生命、真实租约token、任务版本和原生移动goal，不生成租约或货物。准备动作后继续原任务，提取/交付/加工仍要求真实提交。仅修改两个Python准备文件，语法及定向差异检查通过，原生源码和模块保持r135，未另行声称编译；[r136p准备基线](../../TestResults/HybridFullAcceptance/D35-Implementation/r136p-build-baseline.json)身份`d9783e3a9f395ca39d65f7e02ed28cb19a7b9470fec65a3d3955b896b493e218`。

R121目录`D35-F-R121-r136p-RemainingResourceWorkCollection/Hybrid-3v3-n600-r1-51b254ed`先用金手指摆位，原生Ground实际射击释放4个人口空位，存活靶子解锁/归位完成；101/102及生产、采矿、交付准备随后完成。F资源组阶段通过能力、待回执工作及新生对象交接三项：[审计](../../TestResults/HybridFullAcceptance/D35-Implementation/r136p-r121-fault-collection-audit.json)哈希一致，438个对象整组迁移，115个能力承载对象具有匹配冻结/导入摘要，26个实际生产新生对象保持原稳定生命；一个原Processing事件在交接后完成且决定不变。原拥有者PID63628正常离席，随后固定职责重入完成，阶段passed、forced_cleanup为空、未记成员期限失败。额外production专项通过不替代完整G。

[F独立证据目录](../../TestResults/HybridFullAcceptance/D35-Implementation/r136-independent-F-evidence-catalog.json)现覆盖原18项，分别来源于r134、r135和r136p，明确`same_baseline=false`、`formal_gate=false`。这些原样结果不能拼接授予最终场通过。所有原断言、600＋500负载、六客户端最多一渲染端及5/15/180/300秒条件保留。

补采完成后，r136仅对正式`RequestLeave`的本地绘制生命周期收尾：成功送出退出请求才暂停该World视口的世界绘制，GameThread、Slate、网络交接和实际退出回执继续；保存原标记，Stop/Start/Deinitialize及Fail恢复。它针对历史R111渲染端离席15秒失败作具体修正，是否解决仍待同条件复测，不把R119进程退出根因归于绘制。没有调整协调协议、并发/字节容量、负载或期限，没有修改活跃玩法绘制、数值或玩家资产。源码UE5.7正式入口已核对，[静态检查](../../TestResults/HybridFullAcceptance/D35-Implementation/r136-static-review.json)与定向diff通过；沿用已有Editor Win64 Development授权，[构建退出码](../../TestResults/HybridFullAcceptance/D35-Implementation/r136-native-build-status.json)为0，4个动作19.46秒。协调服务/Core源码与产物未变，复用r135成功构建和6000/6000核心验收证据，不重复声称本轮重新构建。

[r136最终待验基线](../../TestResults/HybridFullAcceptance/D35-Implementation/r136-build-baseline.json)身份`58fd0bf0ed8a297417a35e4c88e28e00f0f4c9bbdbca5ca5ee4f174416a69380`，963项，八份BuildId一致`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。gold-v6仍为SHA256 `19e54feef8fc7a11cbca08851b3740f4e796613d18a7be24abe532aac7fc1747`，协议31／清单24／交接2。新目录`D35-GF-R122-r136-FrozenFull`已使用正式`run_integrated_suite.py --through fault`开始完整G→F；尚无最终通过结论。Map `/Game/Maps/LVL_CommanderMassPrototype`及既有保存和相关实体核验沿用，本增量不改地图或美术，玩家与性能验收单列。
