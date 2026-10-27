# Ground 闪现 3C — 制作交付 B_v1

2026-10-27。A_v1两张参考已由用户明确通过，并要求同步落实镜头及至少三倍线条。补充要求是“接受闪现时立即转向闪现方向”和“恢复需要过程”。本轮已完成代码、资源、数据与原地图入口交付；**玩家动态视觉验收待进行**。

## 当前行为与数据

| 项目 | 当前值 / 行为 |
|---|---|
| 方向 | 请求接受后立即把Actor及Blink.Yaw设为锁定镜头水平朝向；均为转向后的前冲，拒绝请求不转向 |
| 距离 / CD | 1200cm / 2秒，从接受请求开始 |
| Begin / Loop / End | 0.02 / 0.8 / 0.3秒；固定步仍限制实际阶段采样粒度 |
| 镜头 | Begin/Loop持续抵消实际扫掠位移；FOV90→105；进入End才开始FOV0.25秒归正、位置约0.45秒达到95%阻尼收敛 |
| 姿态 | 双臂后摆145°、躯干前倾30°；下沉上限60cm、目标膝内角110°，按真实骨长与两脚位置求共同髋部下沉 |
| 全身材质 | 原8个材质槽各有项目副本，Begin能量化、24Hz断层闪烁、8cm表面偏移、Emission=8 |
| 恢复 | 材质和姿态以同一0.3秒五次平滑曲线恢复；碎片采用分散阈值逐步填回；权重0时准确回到原材质图，再归还原接口 |
| 线束 | Trails / TrailsDense2 / TrailsDense3 三个独立发射器，各670/s、一个RibbonRenderer和15模块；合计2010/s，速度继承0 |

原材质图、队色覆盖、描边WPO和参数保留在副本中，运行时复制原接口的Uniform参数。恢复终点无需用估算底色模拟原效果，结束时也不会把低权重的绿色材质直接切成另一种输出。正常End收回；死亡、外部传送、撤销控制及销毁按原规则立即清理。

## 资源与调用

| 项目内路径 | 来源与用途 |
|---|---|
| `/Game/GuLiStrike/FX/GroundMech/Blink/NS_GroundMech_Blink` | 原NS_GreenCrystalTrail项目副本扩成三个独立流，原包只读 |
| `/Game/GuLiStrike/FX/GroundMech/Blink/Materials/*_BlinkPhase` | 复制生产A4B2的8个原材质图，加入相位发光、缺口和WPO，并保留精确原图终点 |
| `/Game/GuLiStrike/FX/GroundMech/Blink/DA_GroundMech_BlinkVfx` | VFX ID57 / GroundMechBlinkBody，保存8组Source→Phase软引用 |
| `/Game/GuLiStrike/FX/GroundMech/Blink/M_GroundMech_BlinkPhase` | 单独材质原型与作者探针，正式机体通过上述8个变体绑定 |
| `/Game/GuLiStrike/Data` | 定向导入Skills、Mobility、Camera、Effects四表；VFX ID56仍指向拖尾系统 |
| `/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations/ABP_GroundMech_A4B2`、`BP_GroundMech_Light` | 已按新原生布局编译并保存，零错误/零警告 |

GA/Hybrid仍负责原玩法阶段和冷却。Cue在Begin接管材质，Loop才启动Niagara，End自然停发射并渐变材质；尾迹与材质均结束后回收。取消/回收路径逐槽比较当前接口，避免旧Cue覆盖外部新材质。逻辑节点和专服不加载表现资源。

半蹲由现有Grounding节点消费本地动画快照，不新增网络字段或动画蓝图接线；保持两段骨长和FK脚/趾位置。瞬间朝向写入原Actor旋转和Blink.Yaw，沿既有运动/私有状态同步。相机首帧另外处理跳过短前摇的情况：本地新闪现仍抵消从Start到首次观察End的位移；快照恢复/晚加入不回补历史偏移。

## 维护入口

1. `configure_blink_3c_source.py`通过excelize-cli维护两张Excel；技能新增材质、半蹲、抖动与线束参数，Camera新增FOV参数，VFX表只保存稳定ID/定义软路径。
2. `export_data_from_excel.py`生成JSON/头文件并校验，禁止手改生成物。
3. `author_blink_assets.py`绝对设置三条独立流；`author_blink_material_variants.py`制作原图变体并在新类可用时绑定定义。仅重建项目生成副本，原材质与商城源系统不改。
4. 取得许可构建并重启后，`import_blink_data.py`先绑定定义、导入四表，再只读配置并更新200/600清单。
5. 最后`prepare_blink_scene.py`保存原地图入口并读取相关实体；不会启动游玩。

## 实际技术记录

- 用户明确同意本轮`GuLiStrikeEditor Win64 Development`构建与编辑器重启。使用`D:/UnrealEngine-5.7`：首次62.67秒成功，首帧相机分支同范围修复后最终9.25秒成功，退出码均0。引擎/项目BuildId均`d4757e21-eaef-492e-b079-c8c0056c493d`，最终编辑器PID38716。
- 31表导出校验通过；Native/UHT、两张Blueprint、3流Niagara和8个正式材质变体编译通过。Niagara与Blueprint零错误/零警告；8材质诊断CompileErrors为空。原生日志中的既有插件依赖/弃用警告保留，未扩展修理范围。
- 新类、8绑定、三条流各670/s及速度继承0已从重启后的编辑器读回。配置指纹`de8f7f7a2f4b88f694feba1e4922c4a7`；200清单哈希`1043b90c531f3fc6a20c0c591d457863aba29e55797036b6b18a5f17661da0d4`，600清单哈希`729555c56bce32eb5bb5964f97bd7115e6f93216ca0ba70fce39c55b5a4b1e55`，均使用新指纹。
- 对应地图`/Game/Maps/LVL_CommanderMassPrototype`保存成功，8实体回读；7个碰撞实体BlockAll。入口TargetPoint为`GroundMobility_Entry=(-2800,50000,1149.04)`，增加`GuLi.Review.GroundBlink3C`标记。入口不是重生点，正式阵营出生规则保持。
- 本轮未启动PIE/Standalone或联机测试，没有截图/读图测试、性能测量或新增测试文件。3组独立流是结构事实，实际视觉密度、姿态与手感仍由玩家核验。

证据：`native_build.json`及两份log、`niagara_readback.json`、`material_variants_readback.json`、`blueprint_readback.json`、`data_readback.json`、`loaded_readback.json`、`scene_readback.json`。

## 玩家入口

在当前地图选择Ground进入PIE。可先在正式出生位置的空旷处按Shift，无需走到验证区；需要坡面/碰撞对照时再使用上述既有区域。

1. 让机体与镜头朝向不同，按Shift：接受时机体立即转向本次方向，以半蹲前倾/后摆姿态前冲；冷却中按Shift应拒绝，机体不因拒绝而转向。
2. 观察FOV快速扩大、摄像机在位移中保持，End开始才追赶；结束后FOV与位置都有归正过程。
3. 检查全机甲含枪、腿、脚的绿色断层/抖动，以及三组密集长线；检查0.3秒收回期间碎片、姿态和原材质逐步复原，原队色与描边完整恢复。
4. 再按既有验收范围检查CD、遇障、空中、打断、暂停恢复及联机去重。当前未登记这些运行项目通过。
