---
schema: guli-progress/v1
id: ARC-20261126-002
work_id: ''
kind: archive
role: root
title: 巡天 HUD 对齐 ground-reticle 试玩版
areas: [ship, ui, combat, presentation]
categories: [art, gameplay]
status: recorded
verification: partial
created: '2026-11-26'
updated: '2026-11-26'
summary: 巡天使用由试玩版绘制指令生成的原生仪表图集和窄体数字，地面射界与空域改用各自的深度测试材质。
completion: done
next_action: ''
relations:
  work_items: [WORK-20261126-001]
status_note: 仪表对齐已归档。材质、资源和站位已读回，玩家视觉与手感未单独签收。
---

# 巡天 HUD 对齐 ground-reticle 试玩版

原生 HUD 从发布的离线 Demo 提取双弧刻度、矩形雷达、准星、炮口圆圈和武器图标共 27 个图块。Slate 按同一套 1920 × 1080 参考坐标排布，用每帧的飞船与目标数据绘制文字、弧形填充、炮数、敌我标记和目标距离。相机初始肩位、鼠标灵敏度和深俯视时的仪表移动按试玩版参数接入。[原生 HUD 绘制](../../Source/GuLiStrike/Gameplay/Ship/UI/GuLiXuntianHUDWidget.cpp)

舰腹射界按每个炮口的真实参数取并集，地面橙色覆盖表示至少一座炮可射。斜纹标出盲区，中央大红叉及就近可射点角标提供方向反馈。空中使用有刻齿的水平线、两道随舰移动的等高环、敌舰的高度虚线和可用炮组标记。[场景组件](../../Source/GuLiStrike/Gameplay/Ship/UI/GuLiXuntianSceneGuide.cpp)

## 资源清单

| 项目内目录 | 来源及用途 |
|---|---|
| `ArtSource/UI/Ship/XuntianHUD_20261125/Native/` | 离线 Demo 的 Canvas 绘制函数与其使用的 HUDuiPRO 图形生成 4 倍透明图集，另存地面及天空材质的 HLSL 源码 |
| `/Game/GuLiStrike/Ship/UI/Xuntian/` | 图集、项目 Rajdhani SemiBold 字面与字体、地表覆盖材质及空域刻度材质。中文名称使用项目已有的 `F_GroundHUD` |

## 构建与读回

- 源码版 UE 5.7 完成标准 `GuLiStrikeEditor Win64 Development` 构建。正式模块文件为 `UnrealEditor-GuLiStrike.dll` 和 `UnrealEditor-GuLiStrikeEditor.dll`。
- 编辑器载入试飞地图并读回 2048 × 2048 图集、两种字体、两个场景材质、六个站位和三个炮模。两个材质均为双面、半透明、自发光着色并启用深度测试。两者的 D3D12 材质编译统计均已读回。
- 对局计时及双方比分仍待正式比赛数据。玩家可在 **无双·巡天 → 巡天 HUD** 中验收空中、高低和地表目标的表现。[试玩与操作](../DevelopmentDocumentation/20261126-巡天飞船HUD与3C.md)
