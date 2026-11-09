---
schema: guli-progress/v1
id: ARC-20261102-008
work_id: WORK-20261102-001
kind: archive
role: root
title: GroundHUD多宽高比与强化摘要截图修正
areas: [ui, presentation, combat, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-11-02'
updated: '2026-11-02'
summary: 第二轮截图修正宽高比锚点、图标、地图裁剪和卡牌布局；真实单击选择验证并修复摘要映射，编辑器原生导表避免强引用对象重建。
next_action: 保留一比一目标，补齐战斗快捷键、完整战斗和性能验证，等待玩家最终视觉决定。
relations:
  work_items: [WORK-20261102-001]
status_note: 三种比例实机截图及两卡权威获得通过；玩家最终视觉未确认。当前PIE保留战斗，原临时设置恢复且无脏包。
---

# Ground HUD 多宽高比与强化摘要截图修正

用户明确要求助手截图校验，沿用同一HUD任务的结束PIE、构建及重启许可。没有扩大为全项目视觉验收，也未修改玩法平衡或卡池。

## 修正与证据

- 视口扩展设计面、四周锚点和中央区域分开处理，解决21:9边框漂浮；近16:9、16:10、21:9实际查看战斗、地图、选牌。HUDuiPRO静态翼形/网格/刻度复用，技能/强化符号改为Slate静态缓存，取消正文白色粗描边。
- 地图增强高程与轮廓表达，UMG层设置ClipToBoundsAlways修复缩放越界；最后实际滚轮、拖动、定位自身、选择据点、Esc关闭均有画面响应。
- Ground卡牌整体上移并同步命中中心，保留现有插画/六层视差和正面单击流程；共享Director保存回读0错误/0警告，Commander默认DirectSelection=false。
- 实点折射发现HUD摘要误显灼烧：权威构筑正确，显示层把不透明ID `06.01` 当英文名称。改为读取卡牌定义的标题文字键；重编译后实点折射、灼烧各一次，计数与图标名称正确，退出后overlay/capture/target/ticking全部关闭。
- 前次“原地填充已避免工厂重建”判断不完整：FillDataTableFromJSONString内部仍调用FactoryCreateText。新增GuLiDataTableImportLibrary临时表校验后复制行到同一对象；仅公共文字/HUD使用，拒绝PIE内导入。非法JSON原表未改，315+34行保存回读、PIE后重导和重启回读通过。

本轮新增符号曾引发TArray自引用追加断言，已先复制首点后追加；两次C++编译错误亦修正。失败与成功日志均保存，不追写前次归档为无故障。

## 构建与运行边界

源码引擎`D:/UnrealEngine-5.7`，获准目标`GuLiStrikeEditor Win64 Development`。最终强化映射14步/22.63秒、字体4步/9.96秒，退出码均0；最终引擎/项目BuildId均为`ec746d14-0746-471d-9ee9-2dfd468f881d`，未重复核验未构建插件。

最终编辑器PID18976，`/Game/Maps/LVL_CommanderMassPrototype` Ground新开局自动创建HUD，当前PIE含折射/灼烧各一次，留在战斗。18个GroundHUD/GroundRogue编辑器实体只读回读，地图/内容无脏包；恢复Canvas调试、席位提示和下次PIE原3840×2037尺寸。场景布局未改。

0.2秒普通刷新与事件合并合同未改；卡牌退出资源清理已回读。CPU/GPU、5Hz计时与完整战斗矩阵未执行。战斗内M/F4电脑控制短按不可靠，动作注入证据不等同物理键盘通过。既有弹道材质错误未扩展修复。地形内容、建筑/道路层次及框架材质发光仍不同于参考，用户没有确认一比一通过。

详细截图、前后对照、宽高比及未测项见[第二轮报告](../../ArtSource/UI/GroundMech/HUD_Reference_20261102/VisualRefinement_20261102.md)；[证据哈希](../../data/GroundHUDValidation/Refinement20261102/manifest.json)、[构建标识](../../data/GroundHUDValidation/Refinement20261102/build-ids-final.json)、[交付状态](../../data/GroundHUDValidation/Refinement20261102/handoff-state.json)。[前次归档](20261102-GroundHUD截图核验与可读性修复.md)保留原阶段事实。
