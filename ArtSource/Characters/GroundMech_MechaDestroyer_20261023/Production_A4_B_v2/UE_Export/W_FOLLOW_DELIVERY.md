> 2026-10-26 本会话已按用户指令完成归档。以下保留当时版本及审核事实，最终方向/时长与清理说明见[完成归档](../../../../../Progress/Archive/20261026-地面机甲镜头方向闪现与会话完成归档.md)；不补记未执行的视觉验收。

# Ground：W 跟随镜头、转身加速与移动降速

2026-10-24，依据用户明确批准方案实施。源码Editor编译、重启和数据导入完成；地图与入口已保存，BP和ABP已打开。未启动PIE、未做运行姿势或图像核验，实际操控与视觉由用户验收。

## 当前操作

- 按住W：机体持续转向镜头水平朝向，边移动边转身。W+A/D保持面朝镜头并斜向移动，Shift不改变朝向规则。
- 松开W：停止镜头朝向跟随，单独A/D/S沿用现有行为。右键仍可带动身体转向并进入越肩；W+右键时身体以镜头水平角为准。
- W的身体朝向独立于枪口瞄准点，不受近处障碍物或相机横向偏移影响；按W本身不切换越肩构图。
- 地面、空中/喷气与UI优先级保持。失去控制、停止前进、UI接管、重生会清理新增状态。

## 本轮数值

唯一维护源：[GuLiStrikeMech.xlsx](../../../../../Data/Excel/GuLiStrikeMech.xlsx)。

| 字段 | 原值 | 当前值 |
|---|---:|---:|
| Mobility/Default/WalkSpeed | 720 | 216 cm/s |
| Mobility/Default/SprintSpeed | 1440 | 432 cm/s |
| Mobility/Default/Acceleration | 2880 | 864 cm/s² |
| Mobility/Default/Braking | 2880 | 864 cm/s² |
| Mobility/Default/MovingTurnRate | 540 | 1080°/s |
| Gait/TurnLeft、TurnRight/BasePlayRate | 1 | 2 |

转身蒙太奇与根旋转曲线共用2倍倍率，单步时长约由2.067秒变为1.033秒，单步角度与15°/5°阈值保持。移动动画仍由实际速度、标定速度与0.75～1.35步幅范围计算，不额外乘0.3；原动画标定速度没有降低。Idle、JumpStart、JumpLoop、Shoot倍率和技能表未改。

## 技术交付

- 新增独立`bFaceCamera`及量化`CameraYaw`。优先W镜头朝向，其次右键/语义瞄准，最后保持机体朝向。现有语义输入签名不变，新增状态默认关闭。
- 相机越肩开关只由右键控制，控制恢复仍使用原`bFaceAim`判断右键状态。输入版本3、Ground状态版本7、移动快照及共享控制版本3，200/600网络清单已刷新。
- [Excel修改及回读](w_follow_tuning_changes_20261024.json)、[标准导入报告](w_follow_import_20261024.json)：七项数值正确；无额外曲线重烘焙。
- [源码构建](w_follow_build_20261024.log)、[BuildId](w_follow_build_ids_20261024.json)：用户本轮方案明确授权编译重启，`D:/UnrealEngine-5.7`，`GuLiStrikeEditor Win64 Development`退出码0，引擎/项目BuildId一致。
- [冷加载检查](w_follow_cold_load_20261024.json)：重启后、导入新数值前直接读取55节点及关键接线，未重建图，保留原已保存资产；此时配置指纹仍为上一版，属预期。
- [最终配置与场景回读](w_follow_final_delivery_20261024.json)：新配置指纹`fec3967f727c6450742b593c38d155df`与两份清单一致，Ground状态版本7，保留8个验证实体并确认地图保存。

## 用户验收

地图`/Game/Maps/LVL_CommanderMassPrototype`、GuLi Local PIE `MainRole=Ground`。入口仍为`BP_GroundMech_Light`、`ABP_GroundMech_A4B2`。复用`Review/GroundMobility_20261024`验证区，中心约(-4000,50000,905)，定位点`GroundMobility_Entry`。

1. 松开右键，把镜头绕到机体侧面再按W：身体应转向屏幕当前朝向，保持普通构图。
2. 持续W并移动鼠标，再组合A/D和Shift：身体持续朝镜头，移动方向按镜头计算。
3. 松开W继续转镜，单独按A/D/S：不因这些输入新增身体朝向跟随。
4. W+右键及原地右键左右转身：镜头模式仅响应右键，转身与动画同步加快。
5. 普通/Shift前后左右、斜向移动及起停：实际速度降低，步态频率随之调整；继续检查坡面、台阶、跳跃和开火。

本轮技术检查不代表玩家效果通过。此前IK和场景说明见[首版记录](GROUND_MOBILITY_DELIVERY.md)，其中旧移速与版本号以本页为准。
