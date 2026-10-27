---
schema: guli-progress/v1
id: ARC-20261027-007
work_id: WORK-20261027-002
kind: archive
role: root
title: GroundMech功能目录与组件职责整理交付
areas: [combat, network, input]
categories: [gameplay]
status: recorded
verification: partial
created: '2026-10-27'
updated: '2026-10-27'
summary: Ground源码按功能归档并提取闪现/镜头组件；原生构建加载、配置与原地图9实体核对完成，玩法与协议保持，待玩家回归。
next_action: 在LVL_CommanderMassPrototype选择Ground回归既有操控、武器、喷气、闪现及控制恢复。
relations:
  work_items: [WORK-20261027-002]
status_note: 本轮只做静态、构建/加载和场景数据核对；不启动PIE、不执行原自动化用例、不做截图读图测试，玩家未登记通过。
---

# 2026-10-27：GroundMech按功能组织与职责分离

用户指出GroundMech中各功能平铺并混合，选择“目录与职责一起整理”，随后明确要求实施。以本轮33文件哈希及已有未提交改动为基线迁移31文件，不覆盖工作区修改。

## 实现

根目录保留Character和导航，其余按Data、Movement/Collision、Weapons、Animation、Abilities/Shared、RocketJump、Blink/Presentation、Camera、Network/Private、Tests归档。目前47个源码文件，Character头由224行降为164行；新增文件用于职责分离。

- Blink组件持有阶段、冷却、GAS句柄、恢复及表现缓存；Character保留原蓝图API和委托。GA与Cue通过组件接口工作，移除其对Character的friend访问。
- Camera组件接管视图状态、Look/Zoom、FOV、镜头保持与恢复，继续控制Character创建的CameraBoom/Camera。两个组件固定名Blink与MechCameraController，无独立Tick或复制，由原位置手动驱动。
- 拆分共享属性/燃料效果、两项GameplayAbility及Blink冷却GE；配置只保留配置，运行状态及输入类型归所属功能。
- Hybrid使用组件快照、延迟恢复和重置接口；输入v6、运动姿态v6、私有状态v11及序列化字段/顺序保持。UE类型名、资源路径、既有默认子对象名及模块依赖保持，无Content迁移和数值变更。
- 更新实际include消费者，包括编辑器工具、动画图、HUD、指挥官及工程单位。不保留旧路径转发头。

## 验证

静态include、生成头、接口和差异检查通过。20个仅迁移文件在还原include后与本轮基线哈希完全一致，覆盖物理、碰撞、喷气、武器、动画和原有测试；没有新增测试框架或用例。

用户本轮明确“现在编译并重启编辑器”后，使用源码引擎`D:/UnrealEngine-5.7`构建GuLiStrikeEditor Win64 Development，一次通过，50.40秒、退出0。Engine/Project BuildId均为`d4757e21-eaef-492e-b079-c8c0056c493d`，Editor PID40496冷启动加载。

生产Ground蓝图CDO含两个新组件，Tick/复制关闭。6项原蓝图入口/委托、10个相关原生类、8组材质映射均读取成功；只读配置查询asset_mutations=false，配置指纹仍为`32eba6f0a72452bd0dde37990c2226ea`。加载脚本按当前Python接口读取replicates字段；未将受保护的Blueprint Status字段作为验证条件。

## 场景与边界

最后复用`/Game/Maps/LVL_CommanderMassPrototype`已保存的Ground入口和机动区，9个相关实体、BlockAll碰撞和生产Ground类读回，不需要修改场景。入口GroundMobility_Entry=(-2800,50000,1149.04)。完成后恢复原弹射卡地图LVL_GroundMechRicochetReview，地图/内容脏包为空。

没有启动PIE、执行自动化用例、性能测试或截图读图测试；编译和实体数据不代替玩法/视觉回归。玩家在原地图选择Ground，验证移动/转向/肩瞄/缩放、开火、喷气、32米闪现/2秒CD、镜头恢复及中断/控制恢复。

完整位置映射见[源码导航](../../Source/GuLiStrike/Gameplay/GroundMech/README.md)，证据与步骤见[本轮交付](../../Artifacts/GroundMechRefactor_20261027/DELIVERY.md)。旧闪现开发和归档保持原事实。
