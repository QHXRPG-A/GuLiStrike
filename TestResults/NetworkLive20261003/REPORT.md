# 当前 PIE 全场单位延迟诊断

当前直接瓶颈是服务器侧状态与姿态发送预算不足。两个客户端在本机回环网络下持续出现大量姿态待发，客户端应用队列为空。源码和实测高度指向“120Hz 网络预算计时与约35Hz实际帧率不匹配”，同时地面弹丸快照占据过半下行；预算计时原因尚未进行单变量 A/B 复测。

## 采集场景与证据

- 用户明确要求对已开启的 PIE 调试分析，并反馈全战场单位延迟。
- Windows UE5.7 Editor Development，PID11520，同一进程专服加两个客户端；地图 `/Game/Maps/LVL_CommanderMassPrototype`。
- 网络窗口为2026-10-03 23:00:13–23:00:33（Asia/Shanghai），20.0128秒；窗口内客户端名册518–522条，战斗自然继续。
- CPU主线程导出窗口28.6190秒，确认913831条导出事件仅属于 GameThread，采样覆盖当前战斗。该窗口与网络窗口部分重叠，分别统计。
- [诊断汇总](diagnosis.json)、[网络原始采样](live.nprof)、[网络载荷汇总](network_profile_summary.json)、[状态流日志](stream_and_profiler.log)、[状态流统计](stream_summary.json)、[运行配置读数](runtime_settings.log)、[CPU统计](cpu_summary.json)、[CPU原始trace](live.utrace)、[主线程事件](events-project.csv)。

## 已测得的瓶颈

| 指标 | 客户端连接1 | 客户端连接2 |
|---|---:|---:|
| Socket下行，十进制KB/s | 71.405 | 71.408 |
| 地面弹丸快照 | 38.122KB/s，53.39% | 38.122KB/s，53.39% |
| 单位姿态RPC | 13.824KB/s，19.36% | 13.840KB/s，19.38% |
| 可靠战斗状态 | 7.565KB/s，10.59% | 7.565KB/s，10.59% |
| 开火提示 | 5.918KB/s，8.29% | 5.918KB/s，8.29% |
| 单位事实状态批次 | 0.529KB/s，0.74% | 0.511KB/s，0.72% |
| 待发姿态ID数，平均／P95／最高 | 349／501／517 | 354／506／510 |
| 每次采样最老姿态等待，平均／P95／最高 | 0.629／1.292／1.702秒 | 0.584／1.116／1.405秒 |
| 每次采样最老事实等待，平均／P95／最高 | 0.712／1.007／1.015秒 | 0.696／1.007／1.046秒 |
| 预算推迟新增次数 | 34.65次/秒 | 34.70次/秒 |
| ACK窗口推迟新增次数 | 0 | 0 |

RPC比例的分母为实际Socket字节。RPC和属性是Socket容器内的载荷，不能与Socket字节重复相加；Socket口径不含IP/UDP及以太网头。两条服务器下行连接分别为127.0.0.1:62327和:62328，客户端上行在同一地址表中的127.0.0.1:17777汇总。

客户端整个窗口 `pendingBatches=0`、`ready=1`；可靠在途批次最高1，未达到4批窗口上限。由此确认当前主要阻塞发生在服务器发送前，ACK窗口和客户端事实应用没有形成对应积压。

等待统计是每次采样时最老待发ID的等待，不等于所有单位的固定延迟，也不是网络RTT。合并待发姿态会保留最新样本、保留原等待起点。日志中的生命周期 `maxSampleGapMs≈51秒` 未作为本窗口延迟使用。

## 预算计时原因与源码证据

运行配置读数确认：四条连接 `CurrentNetSpeed=250000`，三个驱动 `MaxClientRate=MaxInternetClientRate=250000`、`MaxNetTickRate=120`、`NetServerMaxTickRate=30`。`t.MaxFPS=0`，拥塞控制关闭。GameNetworkManager的7000是另一字段，本轮连接实际速率仍为250000，不能把它误认为本轮连接限速。

UE [NetConnection.cpp](D:/UnrealEngine-5.7/Engine/Source/Runtime/Engine/Private/NetConnection.cpp) 的连接Tick用 `MaxNetTickRate` 决定 `DesiredTickRate`，并按 `CurrentNetSpeed × min(实际DeltaTime, 1/DesiredTickRate)` 补充每帧预算。编辑器当前 PIE 不限帧路径与实际不足120Hz的运行组合，会压低每秒实际补充额度。

CPU事件测得平均34.7322个引擎帧/秒、平均帧28.818ms、P95帧34.846ms。按120Hz预算上限计算，`250000 × 34.7322 / 120 = 72358.7 B/s`，与实测每客约71406 B/s高度吻合。需要仅调整节拍或计时条件的同负载 A/B，确认并量化修复效果；本轮没有改变这些运行值。

项目 [GuLiCommanderStateDispatch.cpp](../../Source/GuLiStrike/Commander/Framework/GuLiCommanderStateDispatch.cpp) 的 `FConnectionBudget::Available()` 要求 `IsNetReady()`，再按 `QueuedBits + SendBuffer` 取信用，状态和姿态严格受此信用控制。

同时 [GuLiCombatEffectReplicationComponent.cpp](../../Source/GuLiStrike/Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.cpp) 的短时特效使用 `ForceSend`，允许最多8KiB突发债务，地面弹丸快照按完整活跃集合广播给两个连接。本轮地面弹丸快照、可靠战斗状态与开火提示共占约72.3%下行。状态/姿态必须等信用恢复，缺少跨这两条发布路径的保底份额，因此当前有效预算下出现持续饥饿。

## CPU结论和修复顺序

主线程真实事件测得：姿态捕获平均0.146ms/次，姿态编码0.00774ms/块，解码0.00489ms/块。捕获、发送、接收和ACK四个互不嵌套范围合计约0.166ms/引擎帧；编码和解码已嵌套在发送/接收中，不能再相加。当前编解码CPU不支持“它造成秒级全场延迟”的归因。编辑器和双客户端共享线程、渲染成本会影响帧率，该结果不是部署服务器性能结论。

建议按以下顺序实施，并用同人口、同开火负载复测：

1. 统一网络节拍与实际预算计时，验证120Hz信用截断；不要先扩大已配置的250KB/s上限。
2. 为单位事实、指令结果与姿态保留连接额度，限制短时特效借债，避免跨Actor发布顺序使姿态饥饿。
3. 精简地面弹丸快照的重复信息，做每连接距离/相关性调度；保留服务器扫掠碰撞和伤害权威，不改变双枪数值。

本轮只有诊断，没有代码/配置修复、编译或编辑器重载。采样结束后已还原 `guli.Commander.StateStreamDiagnostics=0`，停止本轮trace和nprof；三份PIE World仍在运行且未暂停。未改变输入、视角、人口、单位状态或质量设置。CSV的STARTFILE命令未启动采集，未将缺失CSV作为证据；CPU结论来自已完成并核对线程的trace。未进行分机、丢包、延迟注入或容量扩展测试。

采集过程中一次把 `applied` 批次序号误读为人数，已在对话更正；本报告人数只取窗口中的 `roster` 字段。
