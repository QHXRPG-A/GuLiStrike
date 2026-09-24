---
schema: guli-progress/v1
id: ARC-20260921-013
work_id: ''
kind: archive
role: root
title: 指挥官StateTree条件属性分类修复
areas: [commander]
categories: [gameplay]
status: recorded
verification: partial
created: '2026-09-21'
updated: '2026-09-21'
summary: 修复Actor和Mass条件参数误用Context分类导致的无效类型显示；源码Editor构建成功，8个字段反射用途正确，三棵既有树编译保存且图结构不变。
next_action: 玩家检查ST_CommanderMass条件显示，并在Commander原型地图继续运行验收。
relations:
  work_items: [WORK-20260921-005]
status_note: 编辑器已正常启动并打开Mass树；已保存场景和兵种绑定回读通过。未启动PIE或自动化，运行效果待玩家确认。
---

# 2026-09-21：指挥官StateTree条件属性分类修复

玩家截图显示`RejectUnreachableStronghold`的`Required`、`Forbidden`、`Phase`、`Result`均标为“上下文 / 无效类型”。UE5.7 StateTree将`Category="Context"`作为上下文绑定语义，只接受对象引用或结构体；原条件节点把整数、枚举也放入该分类，导致详情面板报错。此前资产编译成功不足以证明属性分类正确。本问题与[Mass Subsystem类型登记修复](20260921-指挥官MassStateTree类型登记修复.md)独立，前项修复继续保留。

在`GuLiCommanderStateTreeNodes.h`中，将Actor与Mass两个条件结构体的8个字段改为`Category="Condition"`，不改名称、类型、默认值和判断逻辑。在编辑器资产命令中增加`gs.Commander.RefreshStateTrees`，使用引擎原生反射核对属性用途，编译并保存现有树，保存前比较编辑数据哈希，避免重新生成图。

沿用用户“允许编译并完成资产和场景交付”的同范围授权，确认无游戏World和脏包后正常关闭编辑器。引擎路径为`D:/UnrealEngine-5.7`，目标`GuLiStrikeEditor Win64 Development`，2026-09-21 19:18增量构建退出0；项目与引擎BuildId一致，为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。本次编辑器正常启动并加载新模块，未修改渲染配置或RHI启动参数；不能据此认定前次GPU超时原因已被修复。

原生反射回读确认8个字段均为普通Parameter。`ST_CommanderMass`、`ST_CommanderMiner`、`ST_CommanderBuilder`全部编译成功、可运行、已保存，编译前后编辑图哈希不变。已打开Mass树供玩家检查。原型地图`/Game/Maps/LVL_CommanderMassPrototype`的4个StateTreeReview说明点均为EditorOnly，Soldiers四行仍绑定正确资产，地图及内容无脏包。本次仅刷新相关树资产，没有新增场景对象。

本轮没有改动业务算法、网络接口、10Hz任务调度、5Hz摘要或其他同步参数，也未启动PIE或自动化。属性用途和资产交付已核对，单位运行效果及首次分配实例的ensure消除仍待玩家验证。

[诊断记录](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/diagnosis.json)、[玩家截图](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/reported-invalid-types.png)、[构建结果](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/build.json)、[资产刷新](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/asset-refresh.json)、[场景及引用回读](../../Artifacts/CommanderStateTree/20260921/condition-property-fix/editor-readback.json)、[开发文档](../DevelopmentDocumentation/20260921-指挥官部队StateTree接入与特殊任务退役.md)。
