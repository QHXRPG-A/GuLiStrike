---
schema: guli-progress/v1
id: ARC-20261102-007
work_id: WORK-20261102-001
kind: archive
role: root
title: GroundHUD截图核验与可读性修复
areas: [ui, presentation, combat, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-11-02'
updated: '2026-11-02'
summary: 用户明确授权截图校验后发现并修复文字遮挡、边框覆盖和卡牌露角，三次增量构建加载成功；仍未达到参考图一比一。
next_action: 继续细化参考差异，真实键盘、完整战斗与性能另行验收。
relations:
  work_items: [WORK-20261102-001]
status_note: 本轮有实际助手读图证据，但不是用户最终视觉通过；未提交候选或修改玩法数值。
---

# Ground HUD 截图核验与可读性修复

用户明确指令“你来进行截图校验”覆盖本 HUD 任务原有默认不读图边界。按三张用户参考实际检查战斗、地图与选牌，保留修复前后截图及源码哈希；未改全项目规则。

## 修正

- `GuLiGroundHUDLayout/Widget/Visuals`：生命数值按内容排布，喷气文字与分段条分离，公共边框移到内容下方并收窄，准星加粗。
- `GuLiGroundMapWidget`：普通标记在5Hz数据提交/缩放变化时聚合，调整地形填充与等高线；不增加持续捕获或逐帧Actor遍历。
- `GuLiGroundCardFace`、`GuLiRogueCardPresentation`：保留重建前文案，文字面移到插画前方；统一扩大原六层图面，富文本单行适配，透明混合保留发光，厚度背板内缩避免悬停露角。
- Controller模态内由UMG接管M/I，避免模态重复切换；既有事件同步、0.2秒数据刷新和Ground单击提交规则保留。

本轮没有新增美术图像、导表或修改二进制资产；复用既有HUDuiPRO静态资源、图集、卡牌插画与原567×567高度场。

## 验证与交付

沿用用户已明确同意的“结束PIE，编译重启”。每次退出前确认无脏包；源码引擎 `D:/UnrealEngine-5.7` 构建 `GuLiStrikeEditor Win64 Development`，三次分别10步/11.86秒、7步/11.55秒、4步/9.82秒，均Succeeded且退出码0。引擎、项目及7个插件BuildId一致，最终编辑器PID35416。

`/Game/Maps/LVL_CommanderMassPrototype` Ground新开局自动创建1个HUD、175个子控件；当前正式4001/A4B2 Pawn、两端online/ready。近16:9与最大化约1.88:1均有三张画面证据。实际地图标记点击、模态内F4、Esc关闭、卡牌悬停已观察；未单击提交候选。战斗M/F4动作映射及注入路由通过，但电脑控制脉冲未可靠触发真实键盘路线，不记为通过。

最终保留战斗PIE。临时隐藏的Canvas调试显示、原席位选择提示和下一次PIE的3826×1984设置均恢复回读，脏地图/内容为空。原测试场景已存在，本轮没有新增保存需求。

## 尚未达标

助手判断当前共用框架/卡框的层次、发光、标题与按钮比例、图标造型及地图立体感仍有明显参考差异，一比一还原未通过。真实地图地貌与参考的概念场景不同；不以贴图替代实际地形。护甲和二次确认按钮按已确认规则不采用。完整战斗、Buff/冷却状态、多种实际宽高比及性能没有由截图结果替代。原场景弹道材质错误未在本轮扩展修复。

证据：[实机核验报告与图片](../../ArtSource/UI/GroundMech/HUD_Reference_20261102/VisualAudit_20261102.md)、[哈希清单](../../data/GroundHUDValidation/Visual20261102/manifest.json)、[BuildId](../../data/GroundHUDValidation/Visual20261102/build-ids.json)、[最终交付状态](../../data/GroundHUDValidation/Visual20261102/handoff-state.json)。
