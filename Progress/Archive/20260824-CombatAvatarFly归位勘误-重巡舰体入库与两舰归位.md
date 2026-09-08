---
schema: guli-progress/v1
id: ARC-20260824-001
work_id: ''
kind: archive
role: root
title: 2026-08-24 解决了：CombatAvatarFly 归位勘误——重巡舰体从 Blender 入库，无畏舰/重巡分驻 01/02 文件夹
areas:
- commander
- ship
- data-pipeline
- assets
status: recorded
verification: not_run
created: '2026-08-24'
updated: '2026-09-05'
summary: 2026-08-24 解决了：CombatAvatarFly 归位勘误——重巡舰体从 Blender 入库，无畏舰/重巡分驻 01/02 文件夹
next_action: ''
relations:
  work_items:
  - WORK-20260820-001
status_note: ''
---

# 2026-08-24 解决了：CombatAvatarFly 归位勘误——重巡舰体从 Blender 入库，无畏舰/重巡分驻 01/02 文件夹

- 对应开发文档：无（资产整理 + 配置勘误，附属于 [DIY 飞船](../DevelopmentDocumentation/20260820-DIY飞船.md)）
- 变更类型：内容资产 | 蓝图 | Blender
- **勘误**：本日早前归档 CombatAvatarFly两舰资产归位与01主控3C设置（原件已并入[总归档](./20260825-飞船3C与相机避障总归档-0824至0825.md)） 把 fly-01（space_mouse_fbx 战机）/fly-02（监视无人机）误当成了目标舰。用户澄清：两艘"无组件飞船"= **无畏舰裸舰体 SM_Dreadnought_Hull** + **尚在 Blender 未导出的重巡舰体 scifi_heavy_cruiser_maelstrom.001**。本次已全部回退并按正确对象重做。

## 变更清单

| 文件/资产 | 变更 |
|---|---|
| `/Game/Assets/Arma/CombatAvatarFly-01\|02/` 下的 fly 资产（16 个） | **全部回退**至 `/Game/Assets/Arma/fly-01/`（平铺）与 `fly-02/sci_fi_surveillance_drone/{Materials,StaticMeshes,Textures}/` |
| `D:\UE5.7\Blenders\ShipComponent.blend` 中 `scifi_heavy_cruiser_maelstrom.001` | 按流水线三件套处理（取消隐藏→轴心归 ORIGIN_CENTER_OF_MASS→移世界原点），导出 FBX 后恢复隐藏，**.blend 已保存** |
| `D:\UE5.7\Blenders\exports\SM_Maelstrom_Hull.fbx`（新） | 483KB / 12226 三角形 |
| `/Game/Assets/Arma/CombatAvatarFly-02/StaticMeshes/SM_Maelstrom_Hull`（新导入） | legacy FbxFactory 纯导入（不带材质贴图）；尺寸 37127×75133×20772 = Blender ×100 轴向不变；绑原包材质 ScifiHeavyCruiserMaelstromGrey；碰撞 CTF_USE_COMPLEX_AS_SIMPLE（对齐 Dreadnought 舰体配置） |
| `/Game/Assets/Arma/CombatAvatarFly-01/` | **无畏舰全套迁入**：SM_Dreadnought_Hull（自 ShipComponent）+ 原始整船网格 + 2 材质 + 7 贴图，按 {StaticMeshes, Materials, Textures} 分层 |
| `/Game/Assets/Arma/CombatAvatarFly-02/` | **重巡全套迁入**：SM_Maelstrom_Hull（新）+ 原始整船网格（自 Ships 包）+ 2 材质 + 8 贴图 |
| `/Game/Assets/Ships/` | 只剩 ShipComponent/（14 个 SM_SC_* 组件）；两舰资源包目录已清空 |
| `/Game/GuLiStrike/Ship/BP_CombatAvatarFly01` | 舰体 space_mouse_fbx → **SM_Dreadnought_Hull**；Hull Mesh relative_rotation yaw **-90°**（长轴 Y→+X 艏向）；relative_location (-14162, 0, -929)（质心轴心的包围盒居中补偿）；SpringArm target_arm_length 2400 → **120000**、socket_offset Z+12000（900m 级巨舰追尾镜头） |

## 做了什么

1. **比例验证先行**：比对 SM_SC_Autocannon（Blender 10.83×21.51×5.85 vs UE 1083×2151×585）确认流水线为 ×100、轴向不变，随后才导出。
2. **Blender 导出**：三件套 + 默认 FBX 设置（apply_unit_scale、global_scale=1），命名对齐 SM_Dreadnought_Hull。
3. **目录重组**：编辑器级 rename 全程引用安全——Dreadnought 舰体材质引用、14 个组件的 Blue/Weapons 材质引用均自动跟随新路径（抽查回读确认）。
4. **主控舰体换血**：BP_CombatAvatarFly01 换 SM_Dreadnought_Hull，yaw -90 把艏从 +Y 转到 +X（截图确认尖头朝前），相机臂按舰长 900m 重配。

## 验证

- 视口截图（模拟玩家相机位姿 -110m/48m/-18°）经视觉模型复核：从舰尾后方看、艏朝前、占画面 40-50%、居中、灰蓝金属材质正常。
- PIE 日志：`Game class is 'BP_ShipGameMode_C'`，无 LogLoad Warning、无 LoadErrors。
- 两文件夹资产计数：01 = 11（2 网格 + 2 材质 + 7 贴图），02 = 12（2 网格 + 2 材质 + 8 贴图）。

## 遗留问题

- 两舰体均无 socket/部件：极速仍走 0.5 倍下限（600），900m 舰 6m/s 偏慢，飞行手感调校待 socket/引擎件接入后在 Excel Tuning 表加专属行。
- Blender 中重巡还有 14 个散件对象（.029~.1243 等）未按 SM_SC_* 命名入库——它们是组件候选，待下批拆件。
- Python API 坑追加：`unreal.Rotator(a,b,c)` 构造参数顺序**不是** (pitch,yaw,roll)（实测 -90 落到 pitch），必须用 `.pitch/.yaw/.roll` 字段赋值。
