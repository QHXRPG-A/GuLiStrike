# Ground 爆燃、图标数据管线与 HUD 循环交付

**会话状态：已完成（2026-11-03，按用户收尾指令）。** 正式资源与最终结构化证据保留；旧过程日志、截图和一次性回执已按清单清理，正文中的历史链接只记录当时验证。未执行的视觉或完整性能验收未改记为通过。


2026-11-03。实现已接入正式 Ground 流程，Editor/Coordinator 已构建并重载。当前保留普通 Ground 新局供玩家查看，已关闭验证隔离并恢复开局席位选择弹窗。视觉验收等待玩家，未截图或读图。

## 已接入

- 爆燃重新派生自 `NS_GroundBurningStatus`，保留 `Dots`、`Smoke001`、`Trails`，沿用三个原材质、原火焰运动及 Ribbon 分组。独立系统播放；基础附着灼烧不重启。0.5 秒扩张至 500 厘米外缘，再 0.5 秒淡出，使用 Niagara 的 `DataInstance.Alive` 结束所有粒子。已去除复制发射器对原包私有对象的继承引用，资源可保存并冷启动。
- 八张内置生图透明图标已导入；`GuLiStrikeIcons.xlsx / Icons` 统一管理 `Id / Note / ResourcePath`。Cards、机甲 Skills、Buffs 和 HUD 的 `IconId` 均接入缓存读取，卡牌牌面仍由 `ArtworkId` 管理。更新图标不依赖标题判断，不增加网络请求。
- 移除 Ground 的 I 详情按键、按钮和模态。属性行重新分配间距；M 地图、F4 选牌、真实技能键位及已有短动画保留。
- 生命面板、小地图各接一段 HUDuiPRO 局部刻度弧循环。48 帧、96×96、4 秒周期、帧插值、0.25 秒首尾交叉淡化、透明度 0.18。入场后开始，地图/选牌/隐藏暂停，恢复保留相位。

伤害范围、2 秒周期、冻结伤害名单、固定首次命中点 50 米折射与火红弹道保持原规则。

## 证据与边界

| 检查 | 结果与证据 |
|---|---|
| Editor 构建 | 第一次因局部变量遮蔽 UWidget::Slot 失败，修正并统一Slate材质时钟后最终退出码 0：[最终构建](build-editor-final.log)；[前次重编译](build-editor-retry.log)保留 |
| Coordinator Release | 退出码 0：[构建日志](build-coordinator.log) |
| 引擎基线 | `D:/UnrealEngine-5.7`；引擎/项目 BuildId 同为 `ec746d14-0746-471d-9ee9-2dfd468f881d` |
| 数据管线 | 42 表导出通过；8 图标，重复 ID、非法路径、悬空引用及必填缺失会被拒绝：[校验](icon_validation.json)；5 张相关 DataTable 导入并逐行回读：[导入](data_import.json) |
| 资产与材质 | 三发射器保留，Sprite/Ribbon 实际渲染代理无默认材质回退，Niagara 全部编译成功并保存：[爆燃制作](burst_authoring.json)、[UI 资源导入](hud_asset_import.json) |
| 冷启动爆燃 | 新进程首次真实射击；连续四次相隔 120 tick，即 2 秒；4 接纳、0 抑制、0 拒绝，三发射器都有活粒子，最后粒子为 0：[实际开火](runtime_burst.json) |
| 范围与寿命 | 运行采样最大外缘 500.00003 厘米（浮点误差），粒子 Lifetime=1；独立数值模拟完成扩张/淡出并清空粒子：[数值采样](geometry_probe.json)。AdvanceSimulation 的首次调用用于初始化，requested_age 不是精确 System.Age，不据此伪报首帧时间。 |
| HUD 与循环 | 普通刷新实测约 4.99 Hz；静止循环不增加 CPU 短动画提交计数。地图暂停/恢复，选牌隐藏 4 秒普通刷新与短动画计数均不变、相位不变，退出恢复且不重复入场：[运行记录](runtime_hud.json)。循环控制统一使用Slate的AppDilatedTime，与UI材质时钟一致；最终重载单独复核见[循环回归](runtime_ambient_final.json)。 |
| 技能反馈 | 喷气与闪现释放计数增加，观察到真实闪现 CD 和一次就绪，瞄准状态渐变仍可触发：[技能记录](skills_runtime.json) |
| 输入和图标 | 运行时 Battle 映射有 M/F4、无 I：[映射](input_mappings.json)；M 真实 Enhanced Input 路由可打开地图：[地图](map_input.json)。八张图标在同一 HUD 使用，重复点击只完成一次真实提交。 |
| 布局 | 0/1/3/15/16 图标、123 角标与 16:9/16:10/21:9 设计边界通过结构化检查：[布局](layout_validation.json)。未检查像素或实际可读性。 |
| 驻留与退出 | 新旧动效纹理实际 60 MiB；世界退出旧 HUD 对象销毁，无存活 HUD：[退出](world_cleanup.json)。普通新局无旧射击/爆燃提示，8 图标重新加载、入场 1 次：[最终新局](final_normal_pie.json) |
| 正式场景 | 原地图 32 个相关实体保存回读，200/600 清单通过验证：[场景与清单](scene_final.json) |

第一次 HUD 自动按键检查把 F4 按下/抬起放在同帧，Enhanced Input 未采到按下。失败记录保留，改为跨帧保持 0.15 秒后真实 F4 路径通过；没有改动玩法输入来迁就测试。

本轮尚未重新执行移动中宿主死亡、网络重复回执/恢复的完整组合，也未做完整 CPU/GPU 压力测量；这些机制保持原实现，不能把本轮静态审查或采样称为完整验收。新图标造型、爆燃完整火焰观感、循环接缝和三画幅可读性均等待玩家。

## 玩家入口

地图 `/Game/Maps/LVL_CommanderMassPrototype`，Ground 席位。`GroundHUD_v1_PlayerEntry` 位于约 `(-2800,54000,1229)`；5 米靶区的 `GroundBuff_FiveMeter_Host` 位于 `(-13950,51150,1075)`，旁有说明牌。

1. F4 正面单击取得灼烧，再取得爆燃；再次 F4 可取折射。检查强化图标和右下等级，取消重开应恢复同一候选。
2. 射击 Host，观察火红弹道及每 2 秒的一次独立火焰外扩：0.5 秒扩张、0.5 秒淡出。基础附着火焰保持播放。
3. 检查新喷气、技能和强化图标；I 应无详情入口。M 打开地图、F4 打开选牌时边框循环暂停，关闭恢复；技能释放、CD 与就绪动效仍在。

原图、完整提示词、哈希及制作说明见 [美术资源目录](../../ArtSource/UI/GroundMech/HUD_Revision_20261103/README.md)。本轮构建、结束 PIE、重载和新局运行沿用用户明确授权。
