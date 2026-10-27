---
schema: guli-progress/v1
id: REQ-20261027-002
work_id: WORK-20261027-002
kind: requirement
role: root
title: GroundMech功能目录与职责整理
areas: [combat, network, input]
categories: [gameplay]
status: approved
verification: not_applicable
created: '2026-10-27'
updated: '2026-10-27'
summary: 按功能组织Ground源码并提取闪现和镜头组件，保留现有玩法、蓝图接口、资源路径及网络协议。
next_action: 玩家在LVL_CommanderMassPrototype选择Ground回归既有操控、技能及恢复；源码与加载交付已完成。
relations:
  development: DEV-20261027-002
status_note: 用户选择目录与职责一起整理，并明确要求实施已确认方案；不扩大为全面组件化或新UE模块拆分。
---

# GroundMech功能目录与职责整理

当前33个源码文件约7021行，除一个Private输入头外均平铺。Character同时持有闪现、镜头、输入和控制状态；Abilities混合多个技能，配置头混合运行状态。用户要求按功能目录整理，并提取闪现与镜头职责。

## 已确认要求

- 根目录保留Character及导航；分Data、Movement/Collision、Weapons、Animation、Abilities/Shared、Abilities/RocketJump、Abilities/Blink/Presentation、Camera、Network/Private、Tests。
- 新增手动驱动的Blink和Camera控制组件。原Character的蓝图入口、委托及查询保持；两个组件不独立Tick、不新增复制。
- 拆分GAS定义及运行状态类型。Hybrid通过闪现组件接口应用快照、延迟恢复、重置控制；Cue目标仍为原Character。
- 更新全部实际include消费者，不保留旧路径转发头。现有未提交修改、UE类型名和默认子对象名保留。
- 不迁移Content资源、不调整Excel/配置指纹、不改物理和输入算法、不改变网络字段及版本、不拆新UE模块。

## 验收

- [x] 源码位置明确，Character移除闪现/镜头运行状态，两个新组件拥有对应状态。
- [x] 旧include清理、生成头及接口检查通过，原有蓝图与类型身份保留。
- [x] 本轮获准编译后核对构建与加载；未获准时准确标记最新代码尚未加载。
- [x] 复用LVL_CommanderMassPrototype的Ground入口和机动区，完成相关实体/引用确认。
- [ ] 玩家回归移动、转向、瞄准、开火、喷气、闪现及控制恢复。技术检查不代替效果验收。

本轮不自动启动PIE，不新增测试框架或用例，不执行截图读图测试。
