# Ground 修订交付（2026-11-03）

**会话状态：已完成（2026-11-03，按用户收尾指令）。** 正式资源与最终结构化证据保留；旧过程日志、截图和一次性回执已按清单清理，正文中的历史链接只记录当时验证。未执行的视觉或完整性能验收未改记为通过。


本轮沿用用户对结束 PIE、Editor/Coordinator 构建、重载及结构化验证的授权。正式入口为 `/Game/Maps/LVL_CommanderMassPrototype`；未截图、读图或进行助手视觉验收。

最终汇总见 [validation_summary.json](validation_summary.json)。已打开正常配置的新局，Ground角色与HUD就绪，隔离开关为0；三卡等级和旧爆燃计数均清空，见 [player_handoff_state.json](player_handoff_state.json)。本局按F4重新选择灼烧、爆燃即可玩家验收。

## 正式行为

- 灼烧弹道修复 HLSL 中 `along` 的声明顺序，制作脚本可重复修复旧版本；按用户追加要求改为火红主体和较弱橙色亮核。色基取美术规范的 `#8E3A2A`、`#EE9D58`，同色系发光，普通弹道与持续附着灼烧不改色。
- 爆燃仍独立每 2 秒结算一次。持续命中只刷新灼烧截止；伤害仍是宿主已损生命的 3%，固定 500 厘米三维球与敌人碰撞体相交，穿墙，冻结共同伤害和目标名单。
- 每个有效新爆燃回执播放完整 1 秒：0.5 秒线性扩张，0.5 秒保持 500 厘米外缘并淡出。源事件时间仍用于过期筛选，传输延迟不压缩已接纳动画。基础灼烧独立播放。
- 折射整条路线限制在首次命中位置周围 5000 厘米；每跳在该球内找当前落点最近的无遮挡、未命中敌人。直接伤害逐跳乘 0.8，附加效果不衰减。GroundRogue 状态/攻击/路线版本 3，旧路线拒绝，必须新局。
- 候选卡分别显示“游骑兵 普攻折射-{取得后等级}”“游骑兵 普攻附加灼烧-{取得后等级}”“游骑兵 灼烧每隔一段时间发生爆燃”。保留六层、关键词黄色、正面单击及服务器确认。
- 强化为三列、最多五行可见后滚动，按定义稳定排序，始终显示服务器确认等级角标；取消标题、名字与 +N。爆燃是火焰加扩张环。精简战斗文字，属性/喷气用矢量图标，保留技能键位、CD及动画。

## 漏播与范围修复

`SetAuthority` 会在每次账本更新前调用，不能每次初始化瞬时事件；普通状态片段也可能先于有效提交到达。现在只有首次绑定/恢复初始化事件身份，普通片段只更新附着状态，提交回调消费爆燃一次。事件身份独立于附着火焰槽位，避免状态到期后重复播放；换局、对象释放与 World 退出清理。

派生爆燃移除覆盖位置/尺寸的源速度、缩放模块，并把范围模块放到最终写入位置。CPU 数值检查又发现随机方向长度大于 1；正值随机输入和显式归一化保证三维球外缘，不再把 5 米放大到十余米。

## 资源与数据

正式资源位于 `/Game/GuLiStrike/FX/GroundMech/Burning/`：`M_GroundBurningBullet`、`NS_GroundBurningBullet`、`NS_GroundBurningStatus`、`NS_GroundBurningBurst`。爆燃继续复用已存在灼烧材质，未修改商城原件；每次两组各 32 个 CPU 粒子，固定 bounds ±502 厘米，已有池和可见性机制，专服不加载美术。

`Scripts/GroundMech/Rogue/burning_bullet_shader.py`、`author_burning_burst.py` 是修复/制作入口；`install_ground_revision_vfx.py` 原位应用。最终爆燃保存回读以 [burst_authoring.json](burst_authoring.json) 为准；[vfx_install.json](vfx_install.json) 保留较早的合并安装记录，后续几何修复已替换其爆燃版本。脚本哈希见 [final_build_readback.json](final_build_readback.json)。

Excel 源表经 excelize-cli 修改、标准管线导出；7 张正式 DataTable 及 StringTable 原位更新回读。两份 200/600 Manifest 重导出并验证，分别 292/692 对象、169 规则。HUD 使用现有 Slate 图标系统，本轮无需新位图。

## 检查记录

| 范围 | 结果与证据 |
|---|---|
| 构建 | Editor Win64 Development、Coordinator Release 成功，最终引擎/项目 BuildId 一致；[构建回读](final_build_readback.json)、最终 `build-editor-handoff.log`、`build-coordinator.log` |
| 资产/数据 | [7 表导入](table_install.json)、[资源及原生边界检查](asset_and_boundary_checks.json)、[最终地图实体](scene_final.json) |
| 原生边界 | 49.9/50 米接纳，50.1 米拒绝；逐跳向外漂移、竖直越界及版本 2 路线拒绝。这是原生编码/校验检查，未将这些精确距离全部当作实战靶位通过 |
| 真实折射 | [runtime_ricochet.json](runtime_ricochet.json)：两次真实单击得到 1、2 级，单发 6 跳均在固定球内，伤害 8/6.4/5.12/4.096/3.2768/2.62144（含护盾吸收），携带三种效果 |
| 爆燃数值 | [geometry_probe.json](geometry_probe.json)：22 个 0.05 秒采样；0.5 秒外缘 500.00003 厘米，1 秒 0 粒子，范围浮点误差小于 0.001 厘米 |
| 冷启动真实爆燃 | [runtime_burst.json](runtime_burst.json)：四个周期、120 tick 间隔，4 接纳/0 抑制/0 拒绝，外缘不超过500.001厘米，最终无残留；首次射击按实际Niagara Sprite材质路径查询，无默认材质回退/着色器编译错误 |
| 连续命中 | [runtime_burst_refresh.json](runtime_burst_refresh.json)：真实连续射击3秒，截止延长166tick，Started不变，爆燃仍在Started+120/+240，刷新未推迟周期 |
| 伤害范围 | 宿主、两处体积相交邻机及护盾有回执；范围外与友军不扣血。邻机中心在 5 米外而碰撞体相交，仍正确入选；每轮名单冻结，单轮伤害一致 |
| HUD/选牌 | [runtime_ui_checks.json](runtime_ui_checks.json)：实际三图标角标 2/1/1；重复和无效确认不增级；取消重开同一候选；普通刷新约 5Hz，隐藏约 22 秒无普通刷新 |
| 布局 | 原生瞬态控件检查 0/1/3/15/16 图标、123 级角标与三种画幅数值边界；不等于实际三画幅视觉验收 |

## 玩家入口与剩余验收

选择红方 Ground，F4 获取灼烧再获取爆燃，向 `GroundBuff_FiveMeter_Host` 射击；该靶区位于约 `(-13950,51150,1075)`。观察持续灼烧期间每两秒外扩一次，半秒扩至五米、半秒消退；首次及折射弹道应为火红色。`GroundRogue_Entry=(-2800,54000,1149.04)` 面向 Enemy_01，可核验固定 50 米折射、不重复目标与遮挡。

请核验火焰形态、红色亮度、五米感知范围和 HUD 可读性。完整 CPU/GPU 性能、真实多客户端恢复/晚加入、宿主结算途中死亡，以及三种实际画幅的视觉检查未登记通过。结构化检查不能代替玩家视觉验收。测试隔离开关默认关闭，收尾清理隔离测试局并恢复正常配置，打开干净的Ground新局供玩家使用；没有把测试准备改为正式玩法默认行为。

Progress索引构建与检查为0错误、20条既有结构/篇幅提示。游骑兵玩法总文档超过30KB，建议后续按移动、战斗、UI分册；本轮保留历史，不擅自拆分。
