# GroundMech功能目录与职责整理

本轮仅整理代码职责与位置，保留当前玩法、数值、UE类型名、资源路径及Hybrid网络合同。[源码导航](../../Source/GuLiStrike/Gameplay/GroundMech/README.md)包含完整的功能目录和旧位置迁移表。

## 已交付

- 原33个源码文件约7021行，以[哈希和工作区状态](baseline.json)为基线，迁移31文件；目前47个源码按功能归档，根目录只有Character头/实现及README。
- 新增手动驱动的Blink和MechCameraController组件，分别持有闪现及镜头状态。Character原请求、查询和蓝图委托保持，Look/Zoom转发，推进顺序不变。
- GAS定义拆为共享属性/燃料效果、RocketJump能力、Blink能力及冷却GE；配置头不再混放运行状态。输入编解码只引用输入类型，Cue和GA通过组件接口访问技能，移除其对Character的friend。
- Hybrid通过只读状态、ApplySnapshot、PrepareRestore、CompletePendingRestore和ResetControl操作闪现；输入v6、运动姿态v6、私有状态v11及序列化字段顺序保持。
- [20个保留实现的哈希核对](preserved_implementations.json)全部通过：还原include路径后与迁移前一致，包含移动、碰撞、喷气、武器、动画和原测试。不新增测试框架或用例。

## 构建和加载

用户明确批准本轮编译并重启。使用`D:/UnrealEngine-5.7`构建GuLiStrikeEditor Win64 Development，一次通过，50.40秒、退出0；BuildId为`d4757e21-eaef-492e-b079-c8c0056c493d`，与源码引擎一致。新Editor PID40496。

[加载读回](loaded_readback.json)确认生产Ground蓝图含两个新组件，Tick和复制关闭；原6项蓝图入口/委托、10个相关原生类及8组表现材质映射可读取。原相机、ASC、武器、喷气等子对象名称保持。未移动Content资源或修改源表。

[只读配置核对](configuration_readback.json)无资产变更，指纹仍为`32eba6f0a72452bd0dde37990c2226ea`。静态接口/include/生成头检查通过；相关UE模块的C++和UHT均已编译。

## 玩家回归入口

地图`/Game/Maps/LVL_CommanderMassPrototype`，选择Ground。复用已保存的GroundMobility_Entry=(-2800,50000,1149.04)及原机动区；[场景读回](scene_readback.json)确认9个实体、碰撞与生产Ground类。无需新增场景配置。

1. W/A/S/D、转向、鼠标观察、右键肩瞄和滚轮缩放应保持原行为。
2. 开火、升级和喷气应保持原流程，检查枪口与动画同步。
3. Shift保持32米有效落点瞬移、2秒CD、单体能量替身及原镜头进入/恢复过程。
4. 核验空中释放、打断、死亡、控制撤销及恢复后的技能/可见性状态。

本轮未启动PIE、执行自动化测试或截图读图测试；没有登记玩家效果通过。最终编辑器恢复到原`/Game/GuLiStrike/Cards/GroundMech/Ricochet/Maps/LVL_GroundMechRicochetReview`，地图/内容脏包为空。

机器证据：[迁移表](migration.json)、[静态报告](static_readback.json)、[构建记录](build_readback.json)、构建日志（2026-10-28已清理，保留构建摘要）。
