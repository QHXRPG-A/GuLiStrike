# 先驱号验收入口

最新调整：用户反馈腿部过快后，四向运动 VAT 已降至原播放速率 20%，玩法移速仍为 1440 cm/s；普通移动目标复用半径从 25 m 改为 1 m。正式配置和资产已保存，用户随后开启的 PIE 会话保持运行；实际客户端只读采样约为原频率 0.2。新观感与实际一米点击边界尚无用户确认，见[本轮实施记录](../../../../Progress/DevelopmentDocumentation/20261003-先驱号步态减速与移动点击精度.md)。

前轮验收状态：用户委托“这次你帮我验收”后，本地权威 PIE 核心功能 13 项通过。地图启动、基础速度、召唤初生状态和墙内生成问题修复，编译重载与回归通过；该轮 PIE 已结束并保存场景。完整结果见[委托验收报告](PIE_ACCEPTANCE_20261003.md)。旧证据不代替最新步态观感确认。

地图：`/Game/Maps/LVL_CommanderMassPrototype`。复查时选择指挥官席位。该图核心功能已代验；本次未做容量满载、多客户端或性能测试。

运行模型为 `SM_Pioneer_VAT`，使用静态网格 ISM 和 GPU 顶点动画，不创建骨骼网格组件或 AnimInstance。骨骼变换已烘焙为纹理，CPU 从同源数据采样枪口；骨骼网格与动画副本仅用于兼容交付，不属于 Mass 运行引用。依赖核对见 `Reports/mass_vat_reference_check.json`。

## 场景布置

场景已由 `Scripts/Pioneer/prepare_acceptance_scene.py` 保存，最终回读见 `Reports/qa_final_saved_scene.json`。Outliner 搜索 `PioneerQA_`。七个单兵部署点替代此原型图默认 500 人开局；其他地图默认路径保留。GameMode 和 Controller 分别为 `GuLiCommanderGameMode`、`GuLiCommanderPlayerController`。所有部署点初生待机；既有工厂、赠兵仍会产生其他普通单位。进入后可使用战术档镜头和小地图定位兵群，再切近景检查。

| 部署标识 | XY（cm） | 单位与用途 |
|---|---|---|
| PioneerQA_Single | 0, 65000 | 红方先驱号；双枪、单台 Q、射程 |
| PioneerQA_Multi_A / Multi_B | -2300, 65000 / 67000 | 红方两台先驱号；混选与独立冷却 |
| PioneerQA_SizeReference_WM01 | 0, 63000 | 红方重防号；体型对照，关闭自动开火 |
| PioneerQA_Blocked | -45000, 60000 | 红方先驱号；周围阻挡，完整五个或零个 |
| PioneerQA_Target_InRange | 5600, 65000 | 蓝方重防号被动目标；起点 56 m |
| PioneerQA_Target_OutOfRange | 6900, 63800 | 蓝方重防号被动目标；距 Single 约 70.04 m |

受阻区四堵厚墙使用 QueryOnly、WorldDynamic、Movable，仅阻挡 Pawn，覆盖三层候选外圈，不参与导航。测试只移除了 PIE 副本，保存地图仍有四堵墙。墙用于原地 Q 的直接碰撞拒绝，不据此判断导航烘焙；共享导航代理半径保持 150 cm，先驱号运行避障半径 312.5 cm 单独校准。

## 操作与预期

1. 选中 Single，查看名称、头像、100 生命、0 防御。对照重防号，检查先驱号 6.25 m 配置体宽及相同已审比例。
2. 攻击射程内目标，观察左右主机枪同轮发射，各 1 发/s、各 10 伤害；散布最大偏离瞄准轴 1°。其余外观枪械不增加基础攻击数。
3. 在同一路线分别移动先驱号和重防号，基础速度应为 1440 与 720 cm/s。检查移动、停止、四向步态与枪口瞄准；近中远三档镜头检查色块、法线和 LOD 切换。
4. 使用保持位置攻击或明确攻击指令，分别检查射程内、外目标。移动造成距离变化时，以单位当前位置到目标的距离为准；生产场景不以两台目标彼此间距判断 60 m。
5. Single 按 Q，应立即生成五个扫荡者，初生待机并自动索敌。冷却期间再次 Q 不新增；30 s 后再次 Q 可累积至十个。
6. 同时选中 Multi_A、Multi_B 按 Q，应各生成五个、分别进入冷却。只给其中一台施法时另一台不进入冷却。混选扫荡者时，不由扫荡者再次召唤。
7. 对 Blocked 原地按 Q，应零新增且不进入冷却。解除足够空间后可再次施法；本批应全部五个，不能残留一至四个。
8. 框选新扫荡者并移动、战斗，观察血条与小地图。召唤前后生产人口及预占不增加；先驱号死亡后召唤兵继续保留。
9. 检查原生产入口仍生产第一兵种先驱号，扫荡者不会出现在生产或开局类型列表中。

完全相同请求重发已有真实服务器入口运行证据：`Reports/qa_v5_exact_duplicate_call.json`。10000 实体容量满载、提交中途故障注入、多客户端协议 23 联机及性能/打包未执行，不标为通过。容量保护与提交回滚仅做源码审查；资源预算、截图即时 FPS 不替代实战性能测量。

## 已有证据

- B-v1 用户批准及冻结 SHA256：`../approval_B_v1_20261003.json`。
- 44 骨骼、刚性权重、七动画和导出回读：`Reports/fbx_readback.json`。
指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。
- 骨骼版材质、参考姿态、状态覆盖材质：`Reports/ue_art_finalization.json`。
- Editor 构建、BuildId 和新类型加载：`Reports/editor_build.json`、`Reports/editor_reload.json`。
- 正式运行定义、源像素、六张表、技能与头像磁盘重载：`Reports/ue_runtime_readback.json`。
- 当前地图保存、七部署点与四阻挡物：`Reports/qa_final_saved_scene.json`，停止 PIE 与烘焙状态：`Reports/qa_editor_final_state.json`。
- 本次核心功能汇总：`Reports/qa_acceptance_summary.json`；最新编译：`Reports/qa_exact_dedup_build.log`。

编译、CPU/GPU 源像素、正式表、场景实体及委托 PIE 效果分别记录。旧 `acceptance_scene_readback.json` 的“入口就绪”遗漏启动守卫，本次已通过真实启动发现并修复，当前结论以委托报告为准。美术 A/B 用户批准记录继续保留。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。

