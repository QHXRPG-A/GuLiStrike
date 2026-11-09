---
schema: guli-progress/v1
id: ARC-20261102-005
work_id: WORK-20261102-001
kind: archive
role: root
title: GroundHUD自动创建修复加载与开局检查
areas: [ui, presentation, combat, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-11-02'
updated: '2026-11-02'
summary: 用户批准结束PIE并编译重启；接管事件修复已加载，新开局自动创建唯一Ground HUD，原生布局和真实文字回读通过。
next_action: 在已开启的Ground PIE中验收布局、动画和交互，另行记录完整功能与性能结果。
relations:
  work_items: [WORK-20261102-001]
status_note: 开局自动创建的定向检查通过；没有手动重建HUD，没有助手读图，未实测完整性能。
---

# 2026-11-02：Ground HUD自动创建修复已编译加载

承接[缺失定位与原局恢复](20261102-GroundHUD缺失定位与本局恢复.md)。用户明确同意“现在结束PIE，编译重启”；此次授权包含加载修复并检查Ground开局自动创建。

## 加载与源码边界

正常结束原PIE，逻辑端/协调端退出码均0、没有强制终止，确认无脏地图或内容后关闭编辑器。源码版 `D:/UnrealEngine-5.7` 构建 `GuLiStrikeEditor Win64 Development`：生命周期修复21步骤、47.76秒；追加只读原生诊断6步骤、29.85秒。两次均Succeeded/退出码0，引擎、项目及7个插件BuildId一致。

- `GuLiCommanderHUD.h/.cpp`：订阅和解绑OnPossessedPawnChanged，Pawn接管后立即创建/移除相应界面。
- `GuLiGroundHUDWidget.h/.cpp`：已有控件立即切换Pawn数据宿主，清理旧命中/模态状态；普通0.2秒刷新及显示帧动画未改，没有新增逐帧对象扫描。
- `GuLiLocalPIESubsystem.cpp`：既有只读会话状态增加Ground HUD的类别、入视口、可见标志、owner和原生宽高；仅查询时读取，不加入运行时轮询。

## 新开局定向结果

编辑器PID6440，地图 `/Game/Maps/LVL_CommanderMassPrototype`，红队Ground，正式 `/Game/GuLiStrike/GroundMech/BP_GroundMech_Light`。房间 `ED413CC64490871B42ACDE8ED96C8E70`；正常启动PIE，无ClientSetHUD、无手动创建控件。

早期读数为Pawn尚未接管、Ground控件0个；接管后自动出现唯一 `GuLiGroundHUDWidget_0`，所属HUD仍为 `GuLiCommanderHUD_0`。入视口、可见标志与owner均正确，opacity=1，原生逻辑尺寸2080.64×1080；31个文字控件包含1000/1000生命、100%燃料及LMB/RMB/Space/Shift键位。两成员online/ready、600单位，网络error为空。

检查中Python传递FGeometry得到0×0，而原生C++直接读取同一实例为非零；场景UI原生绘制计数也持续增加。因此Python几何读数不能单独作为未显示的结论，最终采用新增的原生标量字段。这不代表视觉验收通过。

原席位选择设置已恢复，未保存临时设置。当前PIE保持开启供用户验证。此次没有新增地图对象或更改美术资源，沿用既有已保存入口和护盾/击杀靶；完整战斗交互、动画观感、多宽高比与性能仍待验收。

证据：[构建与源哈希](../../Data/GroundHUDValidation/MissingHUD-20261102/build-result.json)、[接管前](../../Data/GroundHUDValidation/MissingHUD-20261102/final-startup-before-possession.json)、[接管后原生读取](../../Data/GroundHUDValidation/MissingHUD-20261102/final-startup-native.json)、[验证结论](../../Data/GroundHUDValidation/MissingHUD-20261102/verification-result.json)。玩家步骤见[交付说明](../../ArtSource/UI/GroundMech/HUD_v1_20261101/Integration/README.md)。
