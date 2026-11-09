---
schema: guli-progress/v1
id: ARC-20261103-002
work_id: ''
kind: archive
role: root
title: Ground完整爆燃、图标管线与HUD循环
areas: [ui, combat, presentation, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-11-03'
updated: '2026-11-03'
summary: 保留附着灼烧的完整三发射器，八张生图图标由共享数据表接入，移除I详情并加入两处原包局部循环。
next_action: 玩家在LVL_CommanderMassPrototype核验火焰形态、新图标、循环接缝与三画幅可读性。
relations:
  work_items: [WORK-20261102-001, WORK-20261028-006]
status_note: 授权构建和冷启动完成，原地图32实体保存回读；连续四次两秒爆燃、约5Hz刷新、隐藏停更、60MiB动效驻留通过结构化检查。未截图读图，完整恢复组合、压力测量及视觉待验收。
---

# Ground 完整爆燃、图标管线与 HUD 循环

爆燃重新从正式 `NS_GroundBurningStatus` 派生，保留 Dots、Smoke001 和 Trails，包括 Ribbon 材质、分组与原火焰运动。独立包络控制半秒扩张、半秒淡出和粒子结束，解除复制对象的私有继承引用后保存成功；不更改2秒玩法周期、伤害或火红弹道。

八次内置 image_gen 生成透明图标。新增 `GuLiStrikeIcons.xlsx / Icons`；Cards、Skills、Buffs、HUD 引用 IconId，客户端准备并缓存纹理，专服不加载。移除被替换的矢量分支、Ground I绑定、按钮和详情模态。保留真实等级、键位、CD、M地图和F4正面单击。

Circle_Elements_04、Circle_Elements_Bars_02 原动画各48帧96方形，4秒循环、帧插值、0.25秒首尾淡化、0.18透明度。材质时钟驱动两个角部弧，地图/选牌/隐藏暂停并恢复相位；无需原包媒体播放。

## 资源清单

| 最终项目内目录 | 来源 | 内容与用途 | 边界 |
|---|---|---|---|
| `ArtSource/UI/GroundMech/HUD_Revision_20261103/Icons` | 内置image_gen；原路径记录在generated_files.json | 八张透明原图、独立提示词和SHA256 | 无助手读图验收 |
| `/Game/GuLiStrike/GroundMech/UI/Icons` | 上述生成PNG | 统一UI纹理，MaxTextureSize256 | 当前实际驻留156方形，源图保留 |
| `/Game/GuLiStrike/GroundMech/UI/Motion` | HUDuiPRO两个Circle原序列 | 两张局部弧图集和透明UI循环材质 | 原包保留，未加载完整媒体序列 |
| `/Game/GuLiStrike/FX/GroundMech/Burning` | 正式NS_GroundBurningStatus | 三发射器爆燃独立副本 | 继续引用原火焰材质；基础灼烧与玩法不变 |
| `/Game/GuLiStrike/Data` | 项目Excel标准导出 | Icons新增及四张业务表IconId引用 | Cards牌面ArtworkId保持独立 |

## 验证与玩家入口

沿用用户本轮明确授权，构建 `D:/UnrealEngine-5.7` 的 `GuLiStrikeEditor Win64 Development` 与 Coordinator Release。Editor 首次局部变量遮蔽错误已修复，重编译与Coordinator退出码0；项目/引擎BuildId均为 `ec746d14-0746-471d-9ee9-2dfd468f881d`。

冷启动真实开火四次爆燃均相隔120tick，4接纳/0抑制/0拒绝，三发射器均有粒子且最终清空。普通刷新约4.99Hz；选牌隐藏4秒刷新、短动画计数及循环相位不变。M输入、真实F4、卡牌确认、喷气释放、闪现CD及就绪检查通过。新旧动效纹理驻留60MiB；退出后旧HUD销毁，新局没有旧回执重播。

`/Game/Maps/LVL_CommanderMassPrototype` 已保存并回读32个相关实体、200/600清单。Ground入口 `GroundHUD_v1_PlayerEntry`；5米验证靶为 `GroundBuff_FiveMeter_Host`，坐标约 `(-13950,51150,1075)`。F4先取灼烧再取爆燃，射击Host，检查半秒扩张到5米与半秒消散；查看新图标并用M/F4核对循环暂停恢复。

完整证据、失败记录及未覆盖组合见[交付记录](../../Artifacts/GroundHUDRevision_20261103/DELIVERY.md)；原图、提示词、来源帧和哈希见[资源说明](../../ArtSource/UI/GroundMech/HUD_Revision_20261103/README.md)。图标实际造型、火焰观感、循环接缝及三种画幅文字可读性由玩家验收；本轮没有执行助手视觉测试。
