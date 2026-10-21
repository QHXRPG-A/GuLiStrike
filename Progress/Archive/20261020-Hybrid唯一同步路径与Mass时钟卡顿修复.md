---
schema: guli-progress/v1
id: ARC-20261020-001
work_id: WORK-20261018-001
kind: archive
role: root
title: Hybrid唯一同步路径与Mass时钟卡顿修复
areas: [network, commander, performance]
categories: [gameplay, performance]
status: recorded
verification: partial
created: '2026-10-20'
updated: '2026-10-20'
summary: 删除旧同步路径并编译；离线复现和修复快照屏障占住逻辑时钟、累计时间丢失。600单位移动写入774→113ms，本地665个Mass最新168ms。完整Hybrid8.12ms，窗口消息长停顿触发原因及玩家反馈仍待确认。
next_action: 核对玩家操作反馈和窗口停顿触发条件，继续处理距离5ms的3.12ms差距。
relations:
  work_items: [WORK-20261018-001]
status_note: 用户取消18窗口、45分钟及新增故障PIE矩阵，异常边界改为离线脚本；本次保留所有失败窗口，不登记全部卡顿已消除。
---

# Hybrid唯一同步路径与Mass时钟卡顿修复

延续[同步替换](../DevelopmentDocumentation/20261019-Hybrid同步正式替换验收.md)和[资源快照接管](../DevelopmentDocumentation/20261019-资源模拟端快照接管.md)。本归档记录阶段事实，未宣称5ms优化完成或玩家已验收。

## 实施与构建

- 删除Core及游戏层ReducedWork、IncrementalDelivery配置和旧分支，周期资格、版本缓存、实体集合、增量交付、订阅和环形结果缓存为唯一实现；保留注册、恢复和公共查询所需接口。
- 默认mask31、身份`hybrid-sync-unique-v2`；64/128退役并兼容忽略告警。Slate共享顶点和后台并行关闭，协议32/交接3、Manifest24及普通玩法入口保持本轮既定版本。
- 用户实玩报告Mass操作卡顿后，确认资源快照完成结果可占住模拟时钟500ms，累计时间一次交给四步限幅器又被截断。修正有序消费后的废弃屏障释放；累计时间以最多三步批次保留，包括已有小数步。真实成员暂停不累计生产或冷却补推进。
- 沿用用户已授权的`D:/UnrealEngine-5.7`、GuLiStrikeEditor Win64 Development及必要重编；01至04编译退出0。引擎/项目BuildId均为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。最新Core DLL SHA256为`82a85c80582f41b5d46ad775e77ff515f3657f63dedcad161a59a822168b290e`；游戏DLL为`6681ad9d97d61807d44a9b33a953d093305aac3d6c08ddd81d9a6026ce167287`。

## 验证证据

证据根目录`D:/UE5.7/PerformanceCaptures/20261019-HybridDirectReplacement-v1`；旧版`frozen-old-sync`和当前`frozen-unique-final`保留源码、差异、产物、BuildId及配置。当前源码并无运行时旧路径，回退依靠冻结构建。

- `Scripts/Network/check_resource_snapshot_offline.py`调用实际生产核心及内存传输。四个请求断点、纪元隔离、损坏/不完整副本、连续接管、出生死亡/采矿状态及状态先于回调通过。相同当前状态增加256笔已完成事务仍为1046字节；520次采集传8版、发送上限2版；备用保留2份完整副本。
- `offline-clock-before`复现20步丢失失败，`offline-clock-after-02`通过20步及前后半步保留、未消费屏障释放和全套核心契约。未增加真实断网PIE矩阵。
- 600单位场景实际本地Mass300，移动写入均值774.01→113.38ms、P95 1228.27→128.64ms。故障旧/新构建仅推进约27～29Hz，原低CPU数字不能登记同频优化收益。
- 满载窗口本地Mass165/135/365，矿车与工程车16/16，据点49、建筑37/37/39，全窗单位组成保持。`response-high-01`移动写入139ms；`response-high-02`受长停顿影响为623ms、P95 1343ms，保留失败结果。
- 最新`response-window-message-01`原生30秒诊断：位移写入168.095ms、P95 240.136ms；观察位移179.856ms；两笔最终回执143.879/68.847ms。30.030秒1837tick，首秒补前置欠时94tick，之后约60.08Hz。全窗频率超过严格1%上边界，分析失败标记没有放宽。
- 完整Hybrid按嵌套并集计8.1184ms/帧、P95 30.9808ms、峰值65.7906ms；6.5993ms/逻辑步、403.789ms CPU/墙钟秒。完整游戏线程20.105ms、P95 42.748ms、峰值81.861ms。客户端Slate上界1.4215ms，完整PIE Slate4.7948ms；未改Slate实现。
- 主要项目自耗时：StateTree0.855、收包0.737、对象族0.603、快照采集0.547、群体推进0.435ms/帧。逻辑批次含子项5.476ms，不能再次与子项相加。
- 原生队列未出现连续增长，末尾待处理/活动/保留回执均0；所有自建编辑器、逻辑端、Coordinator正常退出0，两客户端加载身份已核对。
- 采样分析版本5保留历史报告，兼容缺少时钟证据的旧文件并明确记为不可验收；12项分析回归通过。文档索引已构建，全量检查仍有4处既有外部报告链接缺失及19条既有维护警告，本次没有新增结构错误。

## 边界与玩家入口

仍有独立的5.6～6.0秒主线程停顿，已定位到Windows消息泵及嵌套编辑器重绘，具体触发消息未确认。开启引擎原有逐消息Trace的最新窗未复现；不能据此认定该问题已修复，也不能用好窗口替代受影响窗口。

测试Map为`/Game/Maps/LVL_CommanderMassPrototype`，原地图保留，采样前既有导航准备保存的版本另存于各窗口`map-after-editor-save.umap`，脚本退出后恢复该轮共同输入。使用现有本地PIE入口开启两个客户端和Coordinator；玩家框选本地Mass、连续下达移动/停止与换向，观察是否仍有延迟或整批停走。测试脚本高负载入口使用开发金手指补齐前置，真实移动仍走正式权威逻辑；计时窗内无Python或截图。

当前距离5ms为约3.12ms，尚无干净的冻结旧构建同频A/B，不声明精确优化倍数。全部通过后再做最终截帧的要求仍保留；本次Trace是用户卡顿反馈的故障诊断，玩家最终效果待反馈。
