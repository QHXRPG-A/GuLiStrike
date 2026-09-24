---
schema: guli-progress/v1
id: ARC-20260921-011
work_id: ''
kind: archive
role: root
title: 指挥官StateTree迁移与场景交付
areas: [commander, resources, building]
categories: [gameplay]
status: recorded
verification: partial
created: '2026-09-21'
updated: '2026-09-21'
summary: Commander Actor/Mass接入三棵共享StateTree，退役特殊任务目录及源表，内部业务阶段迁入树；原生构建、资产和原型地图保存读回完成，运行效果待玩家。
next_action: 玩家在LVL_CommanderMassPrototype验证自动业务、命令队列语义和单位生命周期。
relations:
  work_items: [WORK-20260921-005]
status_note: 用户已批准内部阶段迁移、必要网络接口适配及编译和资产交付；未启动PIE、自动化或性能测试。
---

# 2026-09-21：指挥官StateTree迁移与场景交付

用户批准为所有指挥官单位接入StateTree，并进一步允许迁移矿车、建造组件与据点推进Subsystem的内部流程控制。保留移动、寻路、避让、资源结算和推进分组预算；网络接口允许必要适配，10Hz任务、5Hz摘要及原同步参数保持。Mass接线范围为生成时加入树数据、调参迁移保留数据和运行句柄。

最终使用3棵共享资产：扫荡者与战争机器绑定ST_CommanderMass，电磁矿车绑定ST_CommanderMiner，建造车绑定ST_CommanderBuilder。Actor由组件执行，Mass采用原生Schema、ExecutionContext与实例存储，每单位独立实例；Processor按原任务节拍批量执行。任务Subsystem保留命令队列、版本、异步寻路批次和摘要，树通过版本化桥接请求调用原能力。采矿、施工和据点推进的阶段选择已进入树。

删除GuLiStrikeSpecialTasks源表、生成行结构、JSON/CSV、manifest条目、专用脚本、Catalog与Executor；旧DataTable在资产引用为空后删除。网络Special、SpecialTaskId及编号兼容保留。Soldiers新增StateTreeAsset，原13列不变。5条旧执行器错误提示仍使用原文本ID，仅修改GameTexts来源介绍并导入；玩家显示文本不变。标准导出仍为18张表。

用户明确回复“允许编译并完成资产和场景交付”后，源码版UE5.7的GuLiStrikeEditor Win64 Development成功构建。修正Mass fragment复制约束、Move标识歧义及未导出的编辑器接口调用。引擎、项目与本次构建插件BuildId一致：`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。三棵树均编译保存成功，Actor/Mass Schema正确，四行Soldiers绑定回读一致。

现有`/Game/Maps/LVL_CommanderMassPrototype`已保存，未改动原42个Actor变换或资源布局。在`GuLiStrike/Review/CommanderStateTree`中布置Entry、MiningFactory、Construction、StrongholdAdvance四个EditorOnly说明点，对应正常的指挥官入口、自动采矿、B建造和出生推进。保存后再次回读4个说明点及无地图脏包；正常初始配置预计500名Mass士兵与4辆工程车，共504个独立实例，尚未运行计数验证。

静态核对、编译和可运行场景配置交付完成；未新增或执行自动化测试，未启动PIE，未宣称采矿循环、队列中断或网络运行效果通过。开发状态保持待验证。玩家入口与具体实体、资产和证据见[开发记录](../DevelopmentDocumentation/20260921-指挥官部队StateTree接入与特殊任务退役.md)，[需求验收清单](../RequirementDocument/20260921-指挥官部队StateTree接入与特殊任务退役.md)保留运行项未勾选。

交付收尾读回：编辑器无游戏World、无地图或内容脏包。原型地图沿用既有Git忽略规则，本地文件已保存，并保留场景作者脚本复现4个说明点；未改动版本管理规则。
