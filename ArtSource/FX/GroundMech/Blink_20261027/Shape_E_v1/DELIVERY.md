# Ground闪现：单体能量替身与铺屏镜头

当前实际位移与视觉尾迹均为32米，CD2秒；起步/隐藏表现/恢复为0.02/0.22/0.3秒。角色只检查落点并瞬移一次，中段不扫掠。真实机甲只隐藏，原材质不替换；一个复用机甲网格和姿态的绿色能量替身沿路径移动，没有两端云团或闪现Loop动画。

## 本轮镜头

| 状态 | 闪现镜距 | 侧偏 | 额外抬视线 |
|---|---:|---:|---:|
| Free | 500 cm | 160 cm | +11° |
| Aim | 350 cm | 110 cm | +4° |

在0.08秒内随FOV一起进入，0.25秒平滑恢复。普通Free镜距1084.4236cm、侧偏338.2424cm、FOV90°保持。闪现期间CameraBoom锚点留在起点；仅闪现构图临时收近，到达后才开始阻尼追赶。调整意图是放大近端线束、增加左侧和屏幕下沿覆盖；铺屏视觉效果待玩家确认。

Camera字段经Excel源表和导出器生成，不在运行代码中写死。当前配置指纹`32eba6f0a72452bd0dde37990c2226ea`，200/600清单一致。

## 技术事实

- 源码版`D:/UnrealEngine-5.7`，GuLiStrikeEditor Win64 Development最终构建退出0，34.11秒；引擎/项目BuildId均为`d4757e21-eaef-492e-b079-c8c0056c493d`。当前Editor PID33800，最新原生代码已加载。
- [四表和配置读回](data_readback.json)：技能ID3、VFX ID56/57，96条独立Ribbon、24点/条、128片碎片；源商城资产只读。能量替身只复制实际可见的独立武器，复用LeaderPose，不运行独立动画实例。
- [本轮镜头数值轨迹](camera_framing_numeric.json)：100帧、活动阶段锚点变化小于0.01cm，位移3200.00001cm；镜距1084.4236→500cm，FOV90→105°存在15个中间采样；结束恢复全部普通镜头参数与真实模型可见性。`image_capture=false`。
- [此前32米运行合同](warmup_contract_32m.json)和已有Niagara运行数据记录实际单次位移、CD拒绝、原材质保持及96个Ribbon ID；这些技术事实不能替代视觉验收。
- 31张源表验证、相关Python语法、接口引用与差异检查通过。本轮未新建测试框架或自动化测试文件，未做性能基准、晚加入或多客户端完整验收。
- 用户最新AGENTS.md禁止助手读图测试；本轮未截图、读取运行截图或进行视口目检。历史参考与早期截帧仅保留来源记录。

## 玩家入口

地图`/Game/Maps/LVL_CommanderMassPrototype`已保存，选择Ground进入。按Shift即可在真实出生位置观察；当前未保持PIE运行，角色选择提示已恢复。

1. 在普通肩后视角按Shift：观察FOV逐步拉开、镜头收近，线束是否更多进入左侧和下沿。到达后镜头追赶并平滑回到原构图。
2. 对照右键Aim，再松开右键，检查两种构图都能正常恢复。遮挡仍由原SpringArm避障处理，实际距离可能短于表值。
3. 观察是否只有一个移动的机甲能量形体、32米线束及自然消散；真实机甲显现后保持原材质。确认无侧闪/后闪和Loop姿态摆动。
4. 需要固定地形时，到`GroundMobility_Entry=(-2800,50000,1149.04)`；中段阻挡物`GroundBlink_TransitWall=(-2840.81,49401.39,1252.9)`，用来区分中段穿越与落点占用检查。它已从默认出生路线移走。

[场景读回](scene_readback.json)记录9个相关实体、BlockAll碰撞、生产Ground类及正式资源引用。场景数据通过；当前玩家视觉、手感、联机和中断全组合验收仍待反馈。

正式资源：`/Game/GuLiStrike/FX/GroundMech/Blink/NS_GroundMech_Blink`、`DA_GroundMech_BlinkVfx`和该目录Materials。日常调数入口为`data/Excel/GuLiStrikeMech.xlsx`技能表/Blink与Camera；VFX软路径在GuLiStrikeVfx.xlsx/Effects。
