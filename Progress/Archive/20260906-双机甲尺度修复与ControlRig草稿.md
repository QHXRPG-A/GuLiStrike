---
schema: guli-progress/v1
id: ARC-20260906-002
work_id: ''
kind: archive
role: root
title: 双机甲尺度修复与 Control Rig 草稿
areas:
- commander
- ui
- data-pipeline
- combat
- assets
status: recorded
verification: partial
created: '2026-09-06'
updated: '2026-09-06'
summary: WM01 关节全局平移 ×1000、全局尺度归一为 1，重建逆绑定矩阵。顶点位置未改。FourF 保留原绑定，通过 NodeMappingContainer 桥接 Rig 别名与原变形骨名
next_action: ''
relations:
  work_items:
  - WORK-20260906-002
status_note: 阶段成果，**未完成交付**。
---

# 双机甲尺度修复与 Control Rig 草稿

- 对应：[需求](../RequirementDocument/20260906-指挥官双机甲骨骼与武器挂点.md) / [开发](../DevelopmentDocumentation/20260906-指挥官双机甲骨骼与武器挂点.md)。
- 用户“允许”仅扩展 WM01 项目副本的蒙皮、尺度/轴心修复权限；商城和战斗/Crowd 保持不动。

## 已保存内容

| 单位 | 资产目录 | 结果 |
|---|---|---|
| FourFRobot | `/Game/GuLiStrike/Robots/FourFRobot/Rig` | `SKM_FourFRobot_Rig`、`SK_FourFRobot_Rig`、`CR_FourFRobot`；21 骨、30 控件 |
| WM01 | `/Game/GuLiStrike/Robots/WM01/Rig` | `SKM_WM01_Rig`、`SK_WM01_Rig`、`CR_WM01`；99 骨、59 控件 |

WM01 关节全局平移 ×1000、全局尺度归一为 1，重建逆绑定矩阵。顶点位置未改。FourF 保留原绑定，通过 NodeMappingContainer 桥接 Rig 别名与原变形骨名。

控制入口：`CTRL_Global`、`CTRL_Body`；WM01 另有 `CTRL_Chassis` 与 `CTRL_Weapon_UpperBody`。`CTRL_Foot_XX_IK` 是全局空间脚目标，`CTRL_Leg_XX_IK` 是默认 1 的 IK 权重，`CTRL_Leg_XX_FK_YY` 是局部 FK。控制值缩放限制为 1。

## 实际验证

- FourF 14 组、WM01 20 组瞬态数值姿势回读后复位；覆盖默认、逐足抬脚/前移/侧移和主体升高。最大骨段长度变化约 `1.5e-5 cm` / `6.6e-10 cm`。WM01 最大脚踝目标残差约 `0.85 cm`。
- FourF 向外超出腿长时被钳制，不伸长机械骨段。WM01 使用隔离 Null 求解链，避免 CCDIK 包含骨盆；保留骨段局部平移，避免求解器末端强制定位引起伸长。
- RigVM 重编译后两个新实例执行 `Forwards Solve` 返回 true。FourF 默认求解位置与参考最大差约 0.00054 cm。
- 保存回读：87632 / 247887 顶点、21 / 99 骨、独立 Skeleton，所有权重 SHA256 及材质引用仍与原源网格相同；最后脏内容包列表为空。
- 证据在 `outputs/commander-rigs/`：`wm01-reference-repair.json`、两个 `*-rig-authoring.json`、`evaluated-pose-readback.json`、`saved-stage-readback.json`。这些是资产/数值回读，不是可视验收或新增自动化测试套件。

## 未完成与阻碍

- **WM01 武器局部重绑仍未执行**。已检查独立武器 StaticMesh，真实装配尚待校准；武器骨零权重问题未解决，不把基座/安装点当枪口。
- 未创建 `FX_Muzzle_Basic_01`、`FX_AimTarget` 或 WM01 双导弹 Socket，未回填开火坐标。
- 预览网格映射、液压杆贴合、关节翻折、四种截图、人工摆姿与保存后重开均未验收。Rig 元数据为 `Draft_RequiresWeaponBindingSocketsAndVisualQA`，不是成品。
- 旧草稿对象重建期间编辑器崩溃，已重启源码 UE5.7.4。当前“恢复包”窗口阶段阻挡可视工作；Windows 操作报 `GetCursorPos failed ... 0x80070005`，截图黑屏。已请求用户解锁桌面并进入可见 UE 主界面，没有在 UI 恢复或覆盖任何包。

## 范围与续接

新增/更新只涉及本需求 Rig 副本、`Scripts/build_commander_rigs.py`、两个 `Scripts/Blender/*wm01*` 诊断脚本、原副本脚本说明及 Progress 记录。未改原生代码、插件、商城、战斗和 Crowd，无需 C++ 构建。

恢复桌面后核对编辑器恢复状态，继续武器几何/绑定、Socket 校准及原需求的可视验收；不能以本次数值回读代替这些门槛。
