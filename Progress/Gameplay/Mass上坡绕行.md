---
schema: guli-progress/v1
id: GAMEPLAY-COMMANDER-UPHILL
work_id: ''
kind: gameplay
role: root
title: Mass上坡绕行
areas: [commander, navigation]
status: current
verification: partial
created: '2026-10-01'
updated: '2026-10-01'
summary: 可达高处目标沿导航绕过陡坡，持续停滞时以完整个人路径恢复；Editor编译通过，新PIE仍暴露部分共享路径末端的停滞处理遗漏。
next_action: 补齐部分共享路径末端的恢复与失败反馈，并核对西北高处导航连通性。
relations:
  development: DEV-20261001-004
status_note: 2026-10-01静态核对及源码版Editor编译通过。用户随后自行开启PIE，新目标仅有部分路径，135零位移约168秒仍Normal且停滞计时为0；末端处理有遗漏，运行验收未通过。
---

# Mass上坡绕行

对应 `/Game/Maps/LVL_CommanderMassPrototype`。玩家下达高处移动令后，单位保持固定落点，并沿CommanderSoldier的可通行路线接近。绕行允许暂时远离终点；不能仅因进入目标附近的圆形范围就直顶陡坡。末段直达需当前脚下到落点的导航射线无阻挡，且末端确实位于该导航走廊，避免仅看XY把不同高度表面混在一起。

正常仍使用共享路线。通道末端无法直达实际落点，或持续不进展时，以现有世界帧预算补个人完整路径：1秒无进度先回共享中心路线，再持续1秒则进入个人路径恢复。进度按当前拐点和过渡点计，零位移或避让速度为零也会触发；2秒持续无进度可重新查询个人路径。无完整路线时保留明确失败，部分路径末端不会算作抵达高处。

接近拐点时限制前向速度并保留侧向避让。S停止、远目标改令、接敌停留、任务身份、固定落点和导航坡度上限保持现有语义。当前源码与复验标记见[开发记录](../DevelopmentDocumentation/20261001-Mass陡坡绕行与停滞恢复.md)；Editor编译及BuildId核对通过。编译后新位置回读确认部分路径末端仍存在长期Normal/零停滞计时的遗漏，场景保存及修复后的玩家效果仍待确认。
