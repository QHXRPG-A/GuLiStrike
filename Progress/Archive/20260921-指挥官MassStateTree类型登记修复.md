---
schema: guli-progress/v1
id: ARC-20260921-012
work_id: ''
kind: archive
role: root
title: 指挥官MassStateTree类型登记修复
areas: [commander]
categories: [gameplay]
status: recorded
verification: partial
created: '2026-09-21'
updated: '2026-09-21'
summary: 玩家反馈首次分配Mass树实例时GuLiUnitTaskSubsystem类型信息缺失；补齐三个项目Subsystem的每World Mass类型登记，源码Editor增量构建成功，运行复查待玩家。
next_action: 玩家重新进入Commander原型地图，确认TypeInfo ensure消除并观察Mass出生推进。
relations:
  work_items: [WORK-20260921-005]
status_note: 此记录补充此前静态与资产交付后的运行问题，不覆盖原归档；助手未启动PIE或自动化。
---

# 2026-09-21：指挥官Mass StateTree类型登记修复

在[迁移与场景交付](20260921-指挥官StateTree迁移与场景交付.md)后，玩家提供`Failed to find type information for GuLiUnitTaskSubsystem`的handled ensure。异常发生于`UMassStateTreeSubsystem::AllocateInstanceData`为编译树创建动态Processor、解析Subsystem依赖时。UE5.7的模板查询声明不会登记普通WorldSubsystem的运行时类型信息；项目接线遗漏了这一步。树资产此前可编译，不能证明这条运行路径已通过。

局部修复仅修改`GuLiCommanderMassStateTreeProcessor.cpp`：在ConfigureQueries阶段登记任务Subsystem、据点推进Subsystem和单位数据Subsystem的原生Mass类型信息，覆盖节点的全部项目依赖；已登记则跳过，避免重复覆盖ensure。保留主线程限制，不改引擎、底层业务、树资产或同步参数。

沿用此前“允许编译并完成资产和场景交付”的同范围授权，确认编辑器无游戏World和脏包后正常退出。源码引擎路径`D:/UnrealEngine-5.7`，目标`GuLiStrikeEditor Win64 Development`，增量构建退出0，实际重新编译1个Processor源文件并链接项目模块。引擎与项目BuildId一致，为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。随后重新打开既有Commander原型地图，复用原验证入口。

[诊断与原始证据](../../Artifacts/CommanderStateTree/20260921/subsystem-traits-fix/diagnosis.json)、[构建记录](../../Artifacts/CommanderStateTree/20260921/subsystem-traits-fix/build.json)、[开发记录与玩家复查步骤](../DevelopmentDocumentation/20260921-指挥官部队StateTree接入与特殊任务退役.md)。助手未运行PIE或自动化，也未以编辑器加载成功代替运行修复验收；状态保持待玩家复查。

收尾加载补记：上述重新打开地图的两次尝试均在编辑器第4帧遇到独立的D3D12 GPU队列超时，场景回读请求未能执行。因此本轮没有取得新代码加载后的场景回读或运行成功证据。已结束本任务启动的两个无响应进程，未改变渲染配置；此前保存的原型地图和树资产保留。[加载结果](../../Artifacts/CommanderStateTree/20260921/subsystem-traits-fix/editor-load-result.json)。
