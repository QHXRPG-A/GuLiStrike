# 命中批量资源制作经验

参照实现：`Source/GuLiStrike/Gameplay/CombatEffects/GuLiImpactBatchPresentation.*`、`GuLiCombatEffectAuthoringLibrary.*`、`Scripts/Vfx/build_impact_batch_candidates.py`。这是当前管线经验，改动前读实际代码和认证报告，不能用本文替代运行检查。

1. 从当前完整/简化/最简项目资源独立派生，保留启用 emitter、Renderer、材质、随机范围、时序和基础 Scale。先记录组件输入及依赖模块；递归处理动态输入和生产/跟随事件。
2. Global Data Channel 按客户端 World 发布。同 World 多视图不会重复写入；F 发布，读取器 F+1 读取上一帧。消费系统驻留时使用无限、自管理 EmitterState，原单次 SpawnBurst 停用，按事件条件生成原数量粒子。
3. 每个事件输入 Position、Rotation、Scale、Tint、Seed、Lifetime、Slot、Generation、Group。读入独立粒子属性，组件世界变换保持中性；原 Owner/System 变换、缩放、系统年龄和随机依赖改为逐事件属性。出生与实际 F+1 对齐，寿命不能从 F 的排队时间扣除。
4. Sprite、Mesh 和 Ribbon 都检查完整变换，含嵌套函数和事件接收器。Ribbon 身份使用 Slot+Generation 的独立 Niagara ID，禁止把所有事件接到同一 Ribbon，也不能覆盖 Niagara 自身的 PersistentID。
5. Follower 初始化必须先于依赖它的 Spawn 模块。尚未收到生产事件时使用 Slot=-1、Generation=0 的待初始化状态；先接收父粒子的事件数据，再按父身份、出生和变换求值。不能在事件载荷到达前套普通出生退出门。
6. 生命周期先计算事件 Age 和有效代次，保留 ParticleState 的正常完成逻辑；在 ParticleState 之后追加退出门，禁止后面的模块重新把失效粒子设为 Alive。重用槽位只杀对应旧代次。
7. Idle 停止模拟；唤醒不能消费上次发布的旧记录。Reset/Epoch 改变使旧身份失效；各 World 拥有独立槽池、组件、修订与 Channel 发布上下文。
8. 元数据版本先为未认证值。保存、编译零错误、读回图和实际 CPU VM 数据，确认新事件/F+1/Follower/Ribbon/空帧唤醒/复用，再写受支持版本。通过后才能更新正式 Effects 软引用；兼容失败使用同档单次资源。

枪口额外需要出生变换与当前跟随变换：姿态键包括完整来源、武器槽、左右枪口和历史时间；不能用全组共享组件变换代替。保留后坐力及重防号倍率只应用一次。使用独立 Muzzle Channel 和输入版本，不改变已交付命中资源。

## 本轮枪口适配补充

原枪口的六个 CPU emitter 使用 LocalSpace。保留局部空间的物理模拟与原模块，在 Renderer 前生成每槽世界位置、速度、朝向、尺寸及 Ribbon 宽度；共享组件保持单位变换。直接把 emitter 切成世界模拟会改变移动时的火花和拖尾行为，不能只看图编译成功就采用。

Follower 接收生产事件后要立即写入事件身份和第一份世界渲染属性，避免第一帧还使用共享组件原点。Renderer 的 PreviousPosition 等历史输入同样需要有效初值。读回 VM 的局部 ParticlePosition 与世界渲染位置时分别解释，不能把合法局部坐标当作错误原点。

随机种子及组件/系统年龄输入递归检查所有动态输入；原 ParticleState 后的代次退出门、Ribbon Slot/Generation、WM01倍率只应用一次，均用实际粒子输入验证。每客户端 World 拥有独立 Channel 发布和承载组件；旧命中 Channel 不修改。
