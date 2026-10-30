# GroundMech代码导航

玩家Ground席位的实现。运行代码仍属于GuLiStrike模块；UE类名、数据ID、资源路径和网络载荷不因目录变化而迁移。

## 从哪里开始

| 位置 | 职责 |
|---|---|
| GuLiGroundMechCharacter.h/.cpp | 原生装配、输入入口、执行协调和兼容蓝图API |
| Data/ | Excel/DataTable配置加载与指纹 |
| Movement/ | CMC、空中移动、转向状态；Collision/为Mass接触服务 |
| Weapons/ | 开火、升级、武器动画、瞄准状态类型 |
| Rogue/ | Ground个人肉鸽构筑、卡池路由与Hybrid能力桥接；普攻不可变载荷在Weapons/GuLiGroundAttackSnapshot.h |
| Animation/ | 身体动画实例、Aim/Grounding骨骼控制 |
| Abilities/Shared/ | 共享属性集、燃料GE |
| Abilities/RocketJump/ | 喷气组件和GA |
| Abilities/Blink/ | 闪现组件、GA、冷却GE、状态和标签；Presentation/为Cue与材质映射定义 |
| Camera/ | 自由/肩瞄构图、Look/Zoom、闪现镜头与恢复 |
| Network/ | Hybrid输入类型、适配、升级；Private/只存内部编解码 |
| Tests/ | 原有开火与Mass碰撞测试，未新增用例 |

## 状态与执行顺序

- Character创建Blink和MechCameraController默认子对象；既有CameraBoom、Camera、ASC等名称保持。两个新组件都不自行Tick或复制。
- Hybrid推进顺序保持：喷气输入 → 闪现 → 转向 → 移动 → 瞄准 → 骨骼 → 开火 → 闪现表现。渲染帧保持表现更新后更新镜头。
- 闪现组件唯一持有BlinkState、GAS句柄、冷却、恢复标记和表现缓存；查询只返回const状态。Character原RequestBlink、阶段/冷却/姿态查询及OnBlinkStateChanged委托保持。
- Hybrid适配通过ApplySnapshot/PrepareRestore/CompletePendingRestore/ResetControl完成恢复与重置。Input v6、运动姿态v6、私有状态v11及归档字段顺序保持；配置数值和指纹不改。
- Camera组件持有本地视图状态，控制Character原相机组件。技能只提交一次位移抵消请求，恢复或晚加入不重放历史镜头。
- Cue目标仍是Character；通过GetBlinkComponent读取资源和状态，通过GetSeparateWeaponMesh读取独立武器，避免暴露Character私有字段。

## 资源和维护

正式资产仍在`/Game/GuLiStrike/FX/GroundMech/Blink`；技能ID3、VFX ID56/57不改。参数由GuLiStrikeMech.xlsx技能表/Blink与Camera维护，VFX软路径由GuLiStrikeVfx.xlsx/Effects维护。
当前维护脚本位于`Scripts/GroundMech/Blink`；没有为目录迁移重制或重导入资产。

## 旧位置迁移表

所有旧文件原先位于GroundMech根目录，内部输入头原先位于Private/。此表记录源码位置，不为旧路径保留转发头。

| 原位置 | 当前位置/拆出内容 |
|---|---|
| `AnimNode_GuLiGroundMechAim.cpp` | `Animation/AnimNode_GuLiGroundMechAim.cpp` |
| `AnimNode_GuLiGroundMechAim.h` | `Animation/AnimNode_GuLiGroundMechAim.h` |
| `AnimNode_GuLiGroundMechGrounding.cpp` | `Animation/AnimNode_GuLiGroundMechGrounding.cpp` |
| `AnimNode_GuLiGroundMechGrounding.h` | `Animation/AnimNode_GuLiGroundMechGrounding.h` |
| `GuLiGroundMassContactSubsystem.cpp` | `Movement/Collision/GuLiGroundMassContactSubsystem.cpp` |
| `GuLiGroundMassContactSubsystem.h` | `Movement/Collision/GuLiGroundMassContactSubsystem.h` |
| `GuLiGroundMechAbilities.cpp` | `Abilities/Shared/GuLiGroundMechAttributes.cpp`, `Abilities/RocketJump/GuLiGA_RocketJump.cpp`, `Abilities/Blink/GuLiGA_Blink.cpp`, `Abilities/Blink/GuLiGE_BlinkCooldown.cpp` |
| `GuLiGroundMechAbilities.h` | `Abilities/Shared/GuLiGroundMechAttributes.h`, `Abilities/RocketJump/GuLiGA_RocketJump.h`, `Abilities/Blink/GuLiGA_Blink.h`, `Abilities/Blink/GuLiGE_BlinkCooldown.h` |
| `GuLiGroundMechAnimInstance.cpp` | `Animation/GuLiGroundMechAnimInstance.cpp` |
| `GuLiGroundMechAnimInstance.h` | `Animation/GuLiGroundMechAnimInstance.h` |
| `GuLiGroundMechBlink.cpp` | `Abilities/Blink/GuLiGroundMechBlinkComponent.cpp`, `Abilities/Blink/GuLiGroundMechBlinkTags.cpp` |
| `GuLiGroundMechBlinkCue.cpp` | `Abilities/Blink/Presentation/GuLiGroundMechBlinkCue.cpp` |
| `GuLiGroundMechBlinkCue.h` | `Abilities/Blink/Presentation/GuLiGroundMechBlinkCue.h`, `Abilities/Blink/Presentation/GuLiGroundMechBlinkVfxDefinition.h` |
| `GuLiGroundMechBlinkTags.h` | `Abilities/Blink/GuLiGroundMechBlinkTags.h` |
| `GuLiGroundMechCamera.cpp` | `Camera/GuLiGroundMechCameraComponent.cpp` |
| `GuLiGroundMechConfiguration.cpp` | `Data/GuLiGroundMechConfiguration.cpp` |
| `GuLiGroundMechConfiguration.h` | `Data/GuLiGroundMechConfiguration.h`, `Abilities/Blink/GuLiGroundMechBlinkTypes.h`, `Movement/GuLiGroundMechTurnTypes.h`, `Weapons/GuLiGroundMechWeaponAimTypes.h` |
| `GuLiGroundMechFireTests.cpp` | `Tests/GuLiGroundMechFireTests.cpp` |
| `GuLiGroundMechMassCollisionTests.cpp` | `Tests/GuLiGroundMechMassCollisionTests.cpp` |
| `GuLiGroundMechMovementComponent.cpp` | `Movement/GuLiGroundMechMovementComponent.cpp` |
| `GuLiGroundMechMovementComponent.h` | `Movement/GuLiGroundMechMovementComponent.h` |
| `GuLiGroundMechRocketComponent.cpp` | `Abilities/RocketJump/GuLiGroundMechRocketComponent.cpp` |
| `GuLiGroundMechRocketComponent.h` | `Abilities/RocketJump/GuLiGroundMechRocketComponent.h` |
| `GuLiGroundMechWeaponAnimInstance.cpp` | `Weapons/GuLiGroundMechWeaponAnimInstance.cpp` |
| `GuLiGroundMechWeaponAnimInstance.h` | `Weapons/GuLiGroundMechWeaponAnimInstance.h` |
| `GuLiGroundMechWeaponComponent.cpp` | `Weapons/GuLiGroundMechWeaponComponent.cpp` |
| `GuLiGroundMechWeaponComponent.h` | `Weapons/GuLiGroundMechWeaponComponent.h` |
| `GuLiHybridGroundAdapter.cpp` | `Network/GuLiHybridGroundAdapter.cpp` |
| `GuLiHybridGroundUpgrade.cpp` | `Network/GuLiHybridGroundUpgrade.cpp` |
| `GuLiHybridGroundUpgrade.h` | `Network/GuLiHybridGroundUpgrade.h` |
| `Private/GuLiHybridGroundInput.h` | `Network/Private/GuLiHybridGroundInput.h` |

Character保留原位置；输入类型从Character.h迁至Network/GuLiHybridGroundInputTypes.h。

交付记录：`Artifacts/GroundMechRefactor_20261027/DELIVERY.md`。
