---
schema: guli-progress/v1
id: REQ-20260912-005
work_id: WORK-20260912-005
kind: requirement
role: root
title: ShipComponent正式部件蓝图
areas:
- ship
- assets
categories:
- art
- gameplay
status: approved
verification: not_applicable
created: '2026-09-12'
updated: '2026-09-12'
summary: 按ship第一版组件系统文档建立全部ShipComponent部件蓝图，优先使用骨骼炮台，未分配槽位留空并清理旧占位蓝图。
next_action: ''
relations:
  development: DEV-20260912-005
status_note: 用户明确要求Rigged全部炮台与其余部件建立组件蓝图，并授权删除旧占位组件；沿用此前Socket完整保留约束。
---

# ShipComponent 正式部件蓝图

## 范围

- `/Game/Assets/Ships/ShipComponent/Rigged`中的7个骨骼炮台各建部件蓝图；根目录6种其余模型各建部件蓝图。
- 按`D:\学习文档\ship第一版组件系统.md`的7组方案配置兼容槽位和说明；Thor导弹舱另建二级版本，共14个蓝图。
- 没有分配Socket的部件，其兼容槽位数组留空。保留全部现有舰体／武器Socket。
- 替换飞船部件目录，清理旧4个占位部件及默认装配引用。
- 记录文档的依赖、互斥和火力定位；本次不扩展升级系统、索敌系统、炮管俯仰或特殊部件能力。

## 验收

- 14个蓝图引用正确模型，编译并保存；重启读取后配置一致。
- 7个未分配槽位的部件保持空数组；文档指定但不存在的名称不自动映射或改写舰体Socket。
- 旧4个蓝图不再存在，飞船目录切换为新蓝图，活动资产及源码不再引用旧占位。
- 现有Socket、网格、材质及Skeleton保持完整。

见[开发记录](../DevelopmentDocumentation/20260912-ShipComponent正式部件蓝图.md)。
