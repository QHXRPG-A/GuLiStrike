---
schema: guli-progress/v1
id: ARC-20261103-001
work_id: ''
kind: archive
role: root
title: Ground弹道、爆燃、折射边界与HUD修订
areas: [combat, network, ui, vfx, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-11-03'
updated: '2026-11-03'
summary: 火红弹道、1秒爆燃、首次命中点50米折射与图标HUD正式接入；修复状态同步吞爆燃及半径超标，构建与结构化功能检查通过。
next_action: 玩家在LVL_CommanderMassPrototype核验视觉、完整联机恢复及性能。
relations:
  work_items: [WORK-20261028-005, WORK-20261028-006, WORK-20261102-001]
status_note: 沿用明确的Editor/Coordinator构建、结束PIE及重载验证授权；不做助手截图或读图，未将技术检查当作玩家视觉通过。
---

# 2026-11-03：Ground弹道、爆燃、折射边界与HUD修订

已更新正式C++、Excel、DataTable、StringTable、Niagara及制作脚本，保留玩法/表现职责、5Hz加事件同步和正面单击选牌。

- 灼烧材质修正`along`声明顺序，随后按用户“弹道需要改成火红色”改红焰主体和克制橙核；原弹道运动与附着灼烧保持。
- 爆燃每2秒结算，表现0.5秒到5米外缘、0.5秒淡出。运行发现状态片段先于提交消费事件，分离持续状态同步与瞬时回执；身份独立于附着槽位，避免到期重播。专用最终写入模块与方向归一化修复半径超标。
- 折射限定首次命中点50米固定球，GroundRogue升级版本3，Editor/Coordinator及两份Manifest一致；保留次数成长、无遮挡、无重复和×0.8。
- 三卡说明用取得后等级；HUD角标用已确认等级。强化三列、五行后滚动，删标题/名字/+N；精简战斗字样，属性与喷气采用矢量图标，保留键位/CD/原动画。

授权构建成功、BuildId一致；7张表及StringTable回读，原地图32个相关实体保存，200/600清单验证292/692对象和169规则。真实开火验证四次2秒爆燃均呈现、邻机体积相交/友军/范围外/护盾；真实折射两级及六跳伤害匹配。Niagara数值采样0.5秒500厘米、1秒无粒子；HUD三角标2/1/1、重复无效确认不增级、隐藏停刷、约5Hz。精确49.9/50/50.1米及15/16图标/三画幅是原生结构化检查，不冒充全部真实靶位或视觉通过。

资源、脚本、失败排查过程、最终证据、正式Map坐标和玩家操作见[交付说明](../../Artifacts/GroundRevision_20261103/DELIVERY.md)。本轮没有外部资源迁移，复用原有灼烧材质，商城原件不改。完整CPU/GPU性能、真实多客户端恢复、宿主结算途中死亡及玩家视觉未登记通过。
