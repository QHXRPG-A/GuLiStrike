---
schema: guli-progress/v1
id: ARC-20261010-006
work_id: ''
kind: archive
role: root
title: 车辆CharacterMovement与飞船僚机移动开销拆分
areas: [performance, combat, wingman, presentation]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-10-10'
updated: '2026-10-10'
summary: 剩余飞行物与表现管线记录；旧独立投射路径描述已清理。
next_action: 若继续优化，先细分车辆与Ship组件类、僚机固定步和远端呈现计时，再按相同场景确认收益；三项计划整版FPS矩阵继续暂缓。
relations:
  work_items: [WORK-20261009-006, WORK-20261009-008, WORK-20261009-007]
  related: [ARC-20261010-003]
status_note: 用户要求分析CharacterMovement并区分有无飞船及僚机。仅做现有Trace和源码分析，当前只读探测PIE已结束；未重启、编译或修改生产代码资源地图。现有Trace无法单独量化Ship角色与僚机纯运动算法，未将不同场景作为收益对照。
---

# 车辆 CharacterMovement 与飞船、僚机移动开销拆分

复用已有两个30秒采样窗口，增加按完整引擎帧和World分支的移动Scope导出。完整报告、可复现脚本、CSV/Trace证据索引见[移动开销报告](../../outputs/performance/20261010-movement-cost-analysis/report.md)。本次是分析交付，没有实施新的优化。

## 主要结论

- 当前无飞船场景有16采矿、18建造车辆，每World34个CharacterMovement，服务器和两个客户端合计每帧102次。902完整帧平均2.088ms、P95为2.663ms；服务器0.840ms，两客户端分支0.648/0.599ms，约占GT33.176ms的6.3%。客户端Actor Tick关闭不等于运动组件停止。原全窗含边界Scope的2.091ms与完整帧口径略有差异。
- 当前CharacterMovement扣除已记录子项后的内部成本约1.516ms，另有约0.575ms子项，代表帧可见采矿机械模型、采集器、炮塔和挂载的级联变换。未标记内部查询不能全部归因到FindFloor或Sweep。
- 旧飞船场景确有`BP_CombatAvatarFly01_C`及`BP_GroundMech_Light_C`；34车辆加两个玩家角色在三World共108次CharacterMovement，464完整帧平均1.279ms、P95为1.509ms。现有Scope未记录组件类/Owner，无法拆出Ship本人的单独ms；CSV `Ticks/*`是数量，不能当耗时或按比例分摊。
- 僚机为APawn＋自定义UPawnMovementComponent，Pawn/运动组件Tick关闭，Owner由Relay调用子系统按30Hz固定步推进，最多4步/帧；Remote只插值并关闭碰撞。旧两客户端各25个Pawn是表现副本计数。相关Relay平均0.810ms/P95为1.075ms，PresentationActor为0.462/0.591ms，尾迹Niagara GT为0.381/0.450ms；这些包含同步与呈现，不能冒充纯飞行算法耗时。组件子项不得再次加到父项。
- Owner固定步后插值、Remote先写Actor后写平滑PresentationRoot会触及模型及尾迹挂载，属于后续可评估的变换合并入口。现有数据没有证明可删多少，也未实施。当前空Relay仅0.0173ms，不能替代有僚机场景验收。

## 验证与边界

源码确认车辆/Ship的CharacterMovement继承关系、僚机固定步路径、QueryOnly Owner碰撞与Remote无碰撞、引擎CSV Tick计数语义。离线分析脚本成功导出当前902帧和旧464帧，两窗均每引擎帧4个World Tick，逐World运动次数与场景实体及CSV一致。两个客户端Trace无身份元数据，保持A/B分支，不冒认Client1/2。

当前只读桥探测零PIE World，因此未重启游戏或执行新运行验证。两次视口、工厂数、车辆运动和负载不同；不声称优化收益、回退或玩家验收通过。仅增加诊断产物，构建、资源和地图交付不适用。原三项计划整版FPS对照继续按用户要求暂缓。

Progress索引构建与检查结果另见本次交付记录；旧归档正文保留。
