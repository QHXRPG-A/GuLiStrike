---
schema: guli-progress/v1
id: DEV-20261027-002
work_id: WORK-20261027-002
kind: development
role: root
title: GroundMech功能目录与职责整理 — 实施记录
areas: [combat, network, input]
categories: [gameplay]
status: verification
verification: partial
created: '2026-10-27'
updated: '2026-10-27'
summary: 完成31文件迁移和闪现/镜头职责提取，47个源码按功能组织；源码版编译加载、配置与原地图9实体核对完成，待玩家回归。
next_action: 在LVL_CommanderMassPrototype选择Ground，玩家回归移动/瞄准/开火/喷气/闪现和控制恢复；本轮未运行PIE或读图测试。
relations:
  requirement: REQ-20261027-002
status_note: 代码静态检查通过，本轮获准源码版Editor构建退出0并冷启动加载；原地图9实体已核对、原弹射卡地图恢复，待玩家验证效果。
---

# GroundMech功能目录与职责整理 — 实施记录

## 实现与所有权

目录规则见[源码导航](../../Source/GuLiStrike/Gameplay/GroundMech/README.md)。仍使用GuLiStrike模块，GuLiStrikeEditor/GuLiStrikeAnimGraph依赖方向保持；不新增Build.cs依赖或include搜索路径。

| 单元 | 所有权与接口 |
|---|---|
| Character | 默认子对象装配、输入、执行顺序及兼容API；闪现查询/请求转发 |
| Blink组件 | 阶段、冷却、GAS句柄、恢复、资源缓存和Cue管理；只读GetState及快照/控制接口 |
| Camera组件 | 镜距、Look/Zoom、构图混合、FOV与阻尼；控制原CameraBoom/Camera |
| Movement | 保持CMC、落点/瞬移与物理积分实现；碰撞服务迁入Collision |
| Network | 保留序列化字段、顺序和版本，改用组件应用闪现状态 |
| GAS/状态 | 属性与燃料、RocketJump、Blink及冷却分开；状态类型归所属功能 |

两个组件默认子对象名为Blink与MechCameraController，关闭独立Tick与复制，仍由原调用点手动推进。原Character的RequestBlink、阶段/冷却/姿态查询、OnBlinkStateChanged保持。Cue通过组件和只读网格访问器工作，移除其与GA对Character的friend访问。既有类型名、资源路径及默认子对象名保留。

## 任务

- [x] 记录33文件哈希与已有工作区状态。
- [x] 迁移31个源码文件并更新实际include消费者。
- [x] 拆分输入/运行状态、共享属性、两项能力和冷却GE。
- [x] 提取闪现及镜头状态和实现，接通Character/Hybrid/Cue接口。
- [x] 初次静态include、生成头、friend与网络边界检查。
- [x] 完成本轮可选构建/加载记录。
- [x] 最后确认原地图Ground入口和相关实体数据，交付玩家回归入口。
- [ ] 玩家功能和效果回归。

## 证据与验证边界

基线、最终迁移表和静态读回保存在`Artifacts/GroundMechRefactor_20261027/`。代码从33个文件拆为47个文件，Character头由224行降为164行；新增文件用于职责分离，并非新增玩法或测试。

输入v6、运动姿态v6、私有状态v11的载荷和原始字段顺序保持；角色位置和物理仍由Movement管理，配置数值与指纹保持。共享碰撞服务仍可由工程单位和指挥官表现消费，同步更新其include。

用户明确答复“现在编译并重启编辑器”后执行本轮构建。源码版D:/UnrealEngine-5.7，GuLiStrikeEditor Win64 Development一次通过，50.40秒、退出0；引擎/项目BuildId均为d4757e21-eaef-492e-b079-c8c0056c493d。新Editor PID40496，原弹射卡地图恢复。原Ground蓝图CDO含Blink与MechCameraController，两者Tick和复制关闭；6项原蓝图API、10个原生类及8组表现材质映射可读取。配置指纹仍为32eba6f0a72452bd0dde37990c2226ea，配置读取asset_mutations=false。

最后复用`/Game/Maps/LVL_CommanderMassPrototype`的GroundMobility_Entry=(-2800,50000,1149.04)及原机动区，9个实体和碰撞/生产类引用读回；无场景配置变更，复用已保存地图。完成后恢复LVL_GroundMechRicochetReview，地图/内容脏包均为空。没有启动PIE、自动化用例或截图读图测试。

20个仅迁移文件在还原include路径后与基线哈希一致，覆盖物理、碰撞、喷气、武器、动画和原测试。源文件导航、完整迁移表及构建/加载/场景证据见[本轮交付](../../Artifacts/GroundMechRefactor_20261027/DELIVERY.md)，事实归档见[ARC-20261027-007](../Archive/20261027-GroundMech功能目录与组件职责整理交付.md)。
