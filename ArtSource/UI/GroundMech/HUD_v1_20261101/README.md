# 游骑兵 HUD v1 — 概念与正式接入

**会话状态：已完成（2026-11-03，按用户收尾指令）。** 正式资源与最终结构化证据保留；旧过程日志、截图和一次性回执已按清单清理，正文中的历史链接只记录当时验证。未执行的视觉或完整性能验收未改记为通过。


日期：2026-11-01  
生成方式：内置 imagegen，三次调用，三张原生输出。  
阶段：三张概念图保留。2026-11-02 正式入口的原生 UMG/Slate、5Hz＋事件数据层与正面直选已完成源码检查、获准编译及重启加载；配置表、图集和地图保存回读完成。运行效果、性能与玩家视觉待验收。

## 三张图

| 图稿 | 文件 | 原生尺寸 | 用途 |
|---|---|---|---|
| 战斗 HUD | [01_Battle_HUD.png](01_Battle_HUD.png) | 1672×941 | 第三人称外围仪表、双准星、血量、喷气、技能、Buff、肉鸽摘要、立体小地图 |
| 平面大地图 | [02_Tactical_Map.png](02_Tactical_Map.png) | 1672×941 | 北向固定的俯视地图、标记、地点信息与图例 |
| 肉鸽选牌 | [03_Rogue_Selection.png](03_Rogue_Selection.png) | 1672×941 | 两张合法候选卡；正式交互已改为正面单击提交，概念图的确认提示不再适用 |

![战斗 HUD](01_Battle_HUD.png)

![平面大地图](02_Tactical_Map.png)

![肉鸽选牌](03_Rogue_Selection.png)

## 已确认的设计要求

- 第三人称＋驾驶舱包围仪表。战斗中央保持通透，外围使用 HUDuiPRO 的弧线、切角框、细刻度造型。
- 深青底 #021716 / #032E30；青蓝线条 #0D9099 / #6AA4BE；暖橙强调 #EE9D58；主文字 #FEE4D9。卡牌黄色关键词 #FFD84A 保留功能色例外。
- 小地图为倾斜立体等高线、跟随镜头朝向、带北向指针；大地图垂直俯视、北方固定朝上。
- 左键射击、右键过肩瞄准、Space 喷气跳跃、Shift 闪现、F4 肉鸽。M 地图 / Esc 关闭为本次概念图约定。
- 普通命中为浅色 X，击杀为暖橙强化 X。静态战斗图用于讨论一个示意状态，不能验证动效时序。
- Buff 显示图标、时间和层数；属性与已获强化常驻摘要，详情另行展开。
- Ground 独立于 WM01。模型表与正式 BP 已核对为 ModelId=4001、A4B2；使用当前源模型图作为背景身份参考。
- 选牌图使用现有“折射子弹”“灼烧子弹”原图作为生成输入；不生成新卡牌玩法。首获折射文案数值5–8来自现有表及N-1叠加规则，灼烧描述为8秒。
- 卡池存在灼烧爆裂前置，当前合法候选可能为1–2张；这张图采用两卡居中排布，后续控件支持1–3张。

上面列的是设计要求和输入依据，不代表助手已对生成图逐项作视觉验收。图中战场、属性数字和Buff状态用于布局示意；真实地图、命中回执与玩法状态仍需正式UI接入。

## 完整生成提示词与输入顺序

1. [战斗 HUD 提示词](prompts/01_battle_hud.txt)：HUDuiPRO完整驾驶舱、能量条、Topo_3、Target_28、当前A4B2源模型图。
2. [大地图提示词](prompts/02_tactical_map.txt)：第一张生成HUD、HUDuiPRO Topo_3。
3. [肉鸽选牌提示词](prompts/03_rogue_selection.txt)：第一张生成HUD、现有折射子弹Master、现有灼烧子弹Master。

[参考素材清单](reference_manifest.json)保存实际输入路径、角色和SHA-256。  
[生成与技术检查清单](generation_manifest.json)保存每张图的原生输出来源、提示词路径、输入顺序、尺寸、格式和哈希。

原始生成文件同时保留在 imagegen 默认目录；项目内三张文件为逐字节副本。没有使用程序重画、裁切、缩放或合成生成结果。

## HUDuiPRO盘点与后续复用

只读盘点共505个UE对象：Textures 83、Animations 405、Media 12、Materials 4、Source 1。81组独立组件各有静态源图及五对象媒体链；405个组件对象路径已在编辑器注册表中找到。没有现成Widget Blueprint或示例关卡。

完整来源：
- [HUDuiPRO资源入口](../../HUDuiPRO/README.md)
- [81组组件索引](../../HUDuiPRO/Components20261030/Catalog.md)

| 界面用途 | 复用方向 |
|---|---|
| 四周框架 | Cockpit_2、Panel_1、Display_01_content_2 |
| 血条、喷气 | Energy_bar_1/2/4、Progress_Bar_Vertical_01/02/03的造型；真实值用动态填充 |
| 双准星 | Target与Central_point简化图形；真实瞄准方向及枪口射线各自驱动 |
| 小地图、大地图 | Topo_3 / Earth_Map_Topography_2的线条形式；内容由实际关卡地形提供 |
| 属性、技能、Buff | Statistics_Lines、Box、Polygon等容器；图标和数据层分离 |
| 肉鸽 | 保留既有插画输入及公共文本、关键词功能色，设计外层界面 |

素材中的数字、文字、条形变化和地形内容是烘焙内容。独立媒体序列约12.08 GiB；正式HUD宜按用途选择静态素材与参数动画，再按需使用少量媒体动效。

## 2026-11-01 概念图检查

只核对PNG容器完整性、实际尺寸、RGB格式、原始输出与项目副本SHA-256一致，以及提示词和参考文件存在。原生尺寸为1672×941，接近16:9；1920×1080是布局设计基准，并非实际生成分辨率。

没有进行结果读图测试、UE播放、PIE、编译、性能测试或打包；未修改UE资产、Excel或代码。视觉、文字正确性、遮挡、三图一致性及卡图保真程度由用户确认。


## 2026-11-02 正式接入

- [八图标透明图集](04_Runtime_Icons.png)：1774×887 RGBA，四列两行；[实际提示词](prompts/04_runtime_icons.txt)。原始生成结果直接导入为 UI 纹理，未程序重画或切图；控件以 UV 区域选取图标。透明通道范围0–255，PNG完整性已检查，画面由用户验收。
- [接入说明](Integration/README.md)、[静态检查](Integration/static-review.json)、[图集保存回读](Integration/art-readback.json)、[选牌蓝图保存回读](Integration/cards-readback.json)、[正式角色绑定检查](Integration/formal-source-readback.json)、[地图实体回读](Integration/scene-readback.json)。
- 常驻界面复用 HUDuiPRO 静态 Box_1；使用真实填充、文字、技能遮罩、双准星和缓存地形。未启用预录媒体动画。显示帧动画包括生命条/冷却遮罩插值、地图转向与位置插值、命中/击杀 X 淡出及原有卡牌入退场/悬停视差。
- 数据源为 `data/Excel/GuLiStrikeGroundHUD.xlsx`（27行表现配置）与公共 GameTexts（38条新增/维护文案），均通过 excelize 写入、项目现有导出管线生成；不修改平衡值或卡池。
- 正式选牌：候选始终正面，单击提交，服务器成功后反馈退出；没有翻面与二次确认。Commander 继续使用原共享流程。
- 实现、编译、运行、性能和玩家视觉验收分别记录。源码版Editor构建、加载、导表均通过；资源门禁拦截与遗漏Pawn接管通知均已修复。最新构建正常开局自动创建1个Ground HUD，原生布局2080.64×1080、数据文字回读通过，PIE保持开启。HUD完整交互、性能与玩家视觉待验收。详见[构建结果](Integration/build-result.json)、[加载回读](Integration/loaded-readback.json)和[交付说明](Integration/README.md)。
