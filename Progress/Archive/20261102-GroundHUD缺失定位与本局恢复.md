---
schema: guli-progress/v1
id: ARC-20261102-004
work_id: WORK-20261102-001
kind: archive
role: root
title: GroundHUD缺失定位与本局恢复
areas: [ui, presentation, combat, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-11-02'
updated: '2026-11-02'
summary: 当前Ground PIE角色正确但Widget为0；定位到遗漏Pawn接管通知，源码已修复，本局经正式HUD入口恢复。
next_action: 待用户允许中断其当前PIE后编译重启，验证新开局自动创建。
relations:
  work_items: [WORK-20261102-001]
status_note: 本局恢复不是新源码已生效；未停止用户PIE，未编译本次修复，未读图验收。
---

# 2026-11-02：Ground HUD未创建，已定位并恢复当前实例

用户反馈“游骑兵UI好像并没有实装，目前看来时没有的，PIE已开”。保持其会话不间断，在 `/Game/Maps/UEDPIE_0_LVL_CommanderMassPrototype` 做结构化读取：Ground席位、本地控制器、正式4001角色、CanPresent均正确，GuLiCommanderHUD Actor存在，但 `WidgetLibrary.get_all_widgets_of_class` 返回0个Ground HUD，ObjectIterator也只有类默认对象。

## 根因和修复

Hybrid先应用席位，再由Ground Adapter生成并Possess角色。旧HUD只在BeginPlay与输入模式发生变化时检查创建条件；前一次检查时Pawn尚未就绪，后续输入模式未变化又提前返回，导致实际接管机甲后没有再创建控件。此前PIE启动检查只确认HUD Actor，未确认Ground Widget，不能代表UI已出现。

四个源码文件已完成修改与静态检查：

- `GuLiCommanderHUD.h/.cpp`：BeginPlay绑定OnPossessedPawnChanged，EndPlay移除；接管事件独立刷新HUD创建/移除，再同步已有控件的数据宿主。
- `GuLiGroundHUDWidget.h/.cpp`：提取RefreshPawnBinding，换Pawn时即时解绑/绑定并清理命中、准星和模态状态，隐藏期间也处理宿主变化。普通数据刷新仍为0.2秒，无新增逐帧扫描。

## 当前实例恢复

在原会话通过既有 `ClientSetHUD(GuLiCommanderHUD)` 正式入口重新创建HUD，没有重新Possess、换席位或修改玩法状态。随后读取到1个Ground HUD，owner为当前本地控制器，in_viewport=true，SelfHitTestInvisible，opacity=1。其31个文本控件包含1000/1000生命、100%燃料、武器Lv.1、射速4.0/秒，以及LMB/RMB/Space/Shift键位。

这证明现有布局和绑定能在Pawn就绪后创建，不证明新的接管事件代码已加载。已请求用户确认是否可中断其正在玩的PIE以重新构建并重启；本记录阶段未执行新构建。没有通过截图或目检验证布局效果。

证据位于 [MissingHUD-20261102](../../Data/GroundHUDValidation/MissingHUD-20261102/before.json)：[创建条件](../../Data/GroundHUDValidation/MissingHUD-20261102/creation-conditions.json)、[正式入口重建](../../Data/GroundHUDValidation/MissingHUD-20261102/current-session-rebind.json)、[控件与文字](../../Data/GroundHUDValidation/MissingHUD-20261102/widget-readback.json)、[源码检查](../../Data/GroundHUDValidation/MissingHUD-20261102/static-review.json)。当前状态见[开发记录](../DevelopmentDocumentation/20261102-游骑兵HUD正式接入.md)。
