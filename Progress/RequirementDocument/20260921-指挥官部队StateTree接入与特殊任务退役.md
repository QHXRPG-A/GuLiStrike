---
schema: guli-progress/v1
id: REQ-20260921-005
work_id: WORK-20260921-005
kind: requirement
role: root
title: 指挥官部队StateTree接入与特殊任务退役
areas: [commander, resources, building]
categories: [gameplay]
status: approved
verification: not_applicable
created: '2026-09-21'
updated: '2026-09-21'
summary: 全部指挥官兵种使用Actor或Mass StateTree，替代特殊任务目录与执行器，并由树接管采矿、建造和据点推进的内部阶段。
next_action: 玩家在已交付的LVL_CommanderMassPrototype确认运行效果和命令语义。
relations:
  development: DEV-20260921-005
status_note: 用户已批准实施；随后明确允许迁移MiningVehiclePawn、ConstructionWorkComponent、ArmyAdvanceSubsystem流程控制，并允许必要的网络接口调整；动作算法、结算规则、推进分组预算与同步频率保持不变。
---

# 指挥官部队StateTree接入与特殊任务退役

## 已确认范围

- 保留通用命令队列、权限、任务面板、版本与安全取消语义。
- Soldiers新增必填StateTreeAsset；扫荡者与战争机器使用ST_CommanderMass，矿车使用ST_CommanderMiner，建造车使用ST_CommanderBuilder。
- 退役特殊任务Excel、目录、执行器、对应导出产物和DataTable。旧网络编号可留作兼容标识，不保留旧调度器。
- StateTree直接驱动去矿点、采矿、回厂、卸货，去工地、施工，以及选据点、推进、等待占领等阶段；底层保留动作执行、资源结算、路径和编队能力。
- 已批准Mass初始与增援实体接入树，以及调参迁移保留运行实例。其他超出上述三处流程迁移的底层修改须另行确认。
- 网络接口允许为接入适当调整；既有同步频率、重试、回执、预测与插值行为保持。10Hz任务调度和5Hz摘要保持。

## 玩家验收

对应Map：`/Game/Maps/LVL_CommanderMassPrototype`。

- [ ] 四种兵种运行正确的树，权威端负责行为，客户端保持表现与UI。
- [ ] 树中可观察采矿、建造、据点推进的内部阶段，原流程调度不再同时执行。
- [ ] 追加、替换、停止、安全退出、换路不断旧路与一次性出生推进语义保留。
- [ ] 增援、死亡、重复注册和调速不会丢失实例或重授已消费行为。
- [x] Excel与资产引用有效，重新导出不会恢复旧特殊任务表。

[开发记录](../DevelopmentDocumentation/20260921-指挥官部队StateTree接入与特殊任务退役.md)
