---
schema: guli-progress/v1
id: ARC-20261016-029
work_id: ''
kind: archive
role: root
title: 战斗与Commander冷重加入通过及F当前版本续采
areas: [network, commander, combat]
categories: [gameplay]
status: recorded
verification: partial
created: '2026-10-16'
updated: '2026-10-16'
summary: R129保留28条G通过及全部证据哈希；战斗四项和Commander正常退出、冷重加入通过，Ground离席仍超过15秒。r143只调整准备脚本并冻结复用原生构建，R130开始当前版本F独立采集。
next_action: 完成R130原有F独立项目，集中修复剩余正常离席及准备问题，冻结后完整复跑G→F。
relations:
  work_items: [WORK-20261011-002]
  related: [ARC-20261016-028]
status_note: 当前没有完整同基线G/F通过；历史独立F18项、玩家验收和性能结论分别记录。
---

R129原目录为`D35-GF-R129/gameplay_merged/Hybrid-3v3-n600-r1-81827a7a`。[审计](../../TestResults/HybridFullAcceptance/D35-Implementation/r142-r129-collection-audit.json)确认28条G断言通过且关联证据哈希一致，G01/G02/G04/G05/G07五组完整通过。Mass战斗、重防号、战斗事件对应、机枪速度四项均通过；正式采矿前等到实际500飞行物后，采矿运输通过；孤立合法位置准备后的真实传送落地及取消也通过。没有把位置、资源或低血量准备当作伤害、死亡、生产、供电的通过证据。

Commander正常离席取得回执，重启原固定席位逻辑端后冷重加入通过，新端接收19831446字节控制应用数据、控制中断0、编解码失败0。这是r142目录流式发送的运行证据。随后Ground正常离席仍触发原15秒期限，版本3失败；依赖阶段立即停止，Ground/Air后续及F未执行。清理无强制结束。正常离席整体尚未修复，不能由Commander这一个分支扩大为全职责重连通过。

三处准备问题原样保留。第二次人口准备的25个候选中，22个HP10、3个HP100，最后两次射击瞄向健康目标；45秒内未及时读到所需8空位，退出准备时真实死亡及人口最终释放已读回，仍记准备跳过，见[诊断](../../TestResults/HybridFullAcceptance/D35-Implementation/r142-r129-population-preparation-diagnostic.json)。编队在短准备预算下重复等待全军归属，未进入后续原300秒正式等待；首次卡牌取消入口在报价到达前关闭，后续正式提交通过。卡牌入口与前一传送恢复间的竞争仍是待验证假设。

r143仅修改`Scripts/Network/integrated_commander.py`：编队去掉重复的准备归属等待，保留原正式等待并显式检查六端没有待交接；卡牌等待可下令、施法已结束且连续两个不同原生发布；人口准备从已有25个上限内优先使用完整低血量缓冲，避免混入健康前排挡枪。没有改变HP、清单、5/15/45/300秒预算或通过条件。[静态检查](../../TestResults/HybridFullAcceptance/D35-Implementation/r143-static-review.json)及[冻结核对](../../TestResults/HybridFullAcceptance/D35-Implementation/r143-build-baseline.json)确认963输入中只有这一份Python变更，原生源码和产物与r142一致，八份BuildId仍为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。本增量没有执行新编译，复用先前已授权并成功的源码UE5.7三个目标构建。

新冻结身份为`e1683161e7daefead378a1ae14ec5cd806d086bffaef8243175e673c8198fdb9`，gold-v6 SHA256仍为`19e54feef8fc7a11cbca08851b3740f4e796613d18a7be24abe532aac7fc1747`。R130通过既有`fault_merged --collect-only`采集所有原F分支，目录`D35-F-R130/Hybrid-3v3-n600-r1-4dc5910b`、run_id=`4dc5910b-d0d2-46c1-84b0-620e817093a6`。六端、单Commander渲染、600＋500、原Map及协议31／清单24／交接2保持。[运行亲和性读回](../../TestResults/HybridFullAcceptance/D35-Implementation/r143-r130-runtime-affinity-readback.json)确认服务15、六客户端67108848匹配当前所属PID；不据此给出性能结论。归档时F仍在执行，未登记整场通过。

Ground离席的[重传诊断](../../TestResults/HybridFullAcceptance/D35-Implementation/r143-r129-retransmit-diagnostic.json)确认源Ground到重加入Commander的13.03秒采样中发送约4.39MB交接数据、重传0；不能据此修改重传参数。[目录摘要诊断](../../TestResults/HybridFullAcceptance/D35-Implementation/r143-r129-directory-hash-diagnostic.json)另发现旧端在保留历史超过默认4MiB Writer后发布SHA256空串，属于待修的诊断摘要溢出。原集成G/F断言不消费这个字段，不能由此撤销真实玩法证据；`bootstrap`分支使用它，后续应修正摘要实现并保留历史，不用空摘要比较制造通过。

Map仍为`/Game/Maps/LVL_CommanderMassPrototype`，入口`HybridNetworkReview`，复用已保存且已读取相关实体的场景；本次没有地图或美术修改。玩家反馈及性能测试继续单列。全部原始运行目录与首次失败保留，独立收集不替代最终完整同基线G→F。
