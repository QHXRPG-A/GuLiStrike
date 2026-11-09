---
schema: guli-progress/v1
id: ARC-20261102-012
work_id: WORK-20261102-001
kind: archive
role: root
title: GroundHUD原包短动画接入
areas: [ui, presentation, combat, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-11-02'
updated: '2026-11-02'
summary: 正式接入HUDuiPRO派生入场/模态/技能/就绪短动画、真实CD环与射击回弹，普通刷新约5Hz，四图集驻留56.25MiB；构建重载和结构化PIE检查完成。
next_action: 玩家检查实际动画力度、文字/键位遮挡、命中/护盾/击杀X、死亡重生与跨局，并补齐16:10/21:9及完整战斗性能验收。
relations:
  work_items: [WORK-20261102-001]
  requirement: REQ-20261102-001
  development: DEV-20261102-001
status_note: 沿用本HUD任务已有编译重载授权，最新构建成功并保存正式地图5个相关实体；PIE已结束、无脏包。本轮严格未截图读图。实际HUD画幅未跟随16:10/21:9请求，未记通过；历史失败记录保留。原暂停的一比一扩展目标未恢复。
---

# 2026-11-02：Ground HUD 原包短动画接入

用户要求“接动画”“原资源有动画吗，尽量复用”，并明确批准按HUDuiPRO短片段实现。本轮保留左下生命与无单位属性合并、标签16/数值18字号、现有图标/卡图/卡池/布局，只补动画与必要的事件、生命周期接线。

## 资源制作及正式路径

| 来源 | 派生 | 时序及用途 |
|---|---|---|
| Panel_1，0–18帧 | Intro，1024×576单帧 / 4112×2900图集 | 首入/重生0.45秒；地图/详情开0.18秒、关0.12秒 |
| Box_1，0–12帧 | Cast，256方形单帧 / 1040方形图集 | 实际释放0.18秒亮边，瞄准渐变与喷气持续高亮 |
| Circle_Elements_03，0–18帧 | Ready，256方形单帧 / 1040×1300图集 | 0.30秒展开、0.10秒淡出 |
| Circle_Elements_Bars_01，第12帧 | Cooldown，256方形单帧 / 260方形图集 | 真实闪现CD，顶部起顺时针扇形 |

源文件只读，RGBA预乘缩放、每格2像素留边，材质双帧预乘插值、项目青蓝/暖橙着色。没有整段媒体播放器、材质Time节点、生图调用或新提示词。原始RGBA共55.0308MiB，UE运行纹理驻留56.25MiB；64MiB预算指这四张派生纹理，不能解释为整套HUD显存。

来源/输出SHA256、帧范围、尺寸和导入参数保存在[动效资源清单](../../ArtSource/UI/GroundMech/HUD_Motion_20261102/manifest.json)。正式目录为 `/Game/GuLiStrike/GroundMech/UI/Motion/Textures` 与 `Materials`，4张纹理和 `M_GroundHUD_Atlas`、`M_GroundHUD_Cooldown` 保存回读。`GuLiStrikeGroundHUD.xlsx` 经excelize新增36行、原结构不变，既有管线导出共71行；315行公共文本不变。源脚本为 `Scripts/GroundMech/prepare_ground_hud_motion.py`、`author_ground_hud_motion.py`。

## 接线与性能约束

武器在实际发射身份去重/过期过滤后发布本地通知，瞄准和闪现发布实际状态边沿，喷气复用既有事件。HUD数据子系统统一订阅、分组帧末合并和解绑，没有新网络请求。普通数字继续0.2秒定时刷新；动画按显示帧，CD从现有逻辑时钟计算，UI不决定可用状态。

四臂准星每发外扩4设计像素，累计不超过12，切向抖动不超过1.5，0.18秒回弹；中心和枪口投影不移。已有真实伤害/吸收/击杀回执链路继续筛选局次与本地来源，X增加收缩淡出，普通0.16秒/击杀0.28秒，同帧击杀优先。初始/恢复只建立当前基线，隐藏清空短动画及待播事件、暂停普通计时；模态关闭不重播入场，销毁/换Pawn清理订阅。连续开火使用固定大小状态并合并，不堆动画队列。

验证选牌隐藏时，发现既有准备门控把按需ShaderMap误判为不可用：真实资源已存在、编译完成且无错误，但 `HasValidGameThreadShaderMap` 仍为false。改用实际RHI平台及 `IsGameThreadShaderMapComplete`，必要时异步编译。最终进入可选阶段、取消重开及HUD恢复通过；保留直接选择和既有可见时按需捕获。

## 编译与结构化运行证据

证据总入口：[delivery-summary.json](../../data/GroundHUDValidation/Motion20261102/delivery-summary.json)，完整说明见[资源README](../../ArtSource/UI/GroundMech/HUD_Motion_20261102/README.md)。

- 沿用用户明确编译/重启授权。首轮因局部 `Slot` 遮蔽UWidget成员失败，已改名；后续测试探针直接调用GAS造成Editor链接缺失，改用既有Rocket接口，不新增模块依赖。所有失败日志保留。最终 `build-delivery.log` 为 `GuLiStrikeEditor Win64 Development`，4步/6.04秒成功。引擎和项目BuildId均为 `ec746d14-0746-471d-9ee9-2dfd468f881d`，新编辑器已加载。
- 源帧/图集哈希、RGBA透明首帧/非空末帧、帧范围、材质参数、配置导入逐字段比较、资产引用与保存回读通过。Python AST、源码接口/包含与生命周期检查、差异空白检查完成。
- 第一组1081帧记录：单发一次，连续发射累计6次与有效发射回执一致，无效输入不播，准星上限/CD范围正常，普通刷新4.998529844Hz，无同帧追赶多轮。模态开关返回入场计数仍为1。早期喷气回充尚在空中，未作为就绪通过证据。
- 最终独立运行13项检查通过：瞄准渐变、实际低燃料喷气耗尽与一次就绪、实际闪现中断和冷却后一次就绪、卡牌材质就绪/可选阶段、取消重开、隐藏普通刷新及动画更新停止、恢复不追播入场/就绪。Editor探针只用于本地PIE准备和只读观测，生产HUD不调用。
- Python无法反射FGeometry，最初零尺寸结果已单列无效。后改用原生HUD尺寸及实时UMG属性，实际近16:9下9个面板在界内；16:10/21:9请求未改变实际HUD比例，不能登记这两种画幅通过。没有读图或键位可读性判断。
- 本轮未实测真实命中/护盾吸收/击杀X、重复伤害回执、死亡重生、同World恢复/换席位/跨局全流程；源码路径已核对，运行待验。尝试读取的蓝队靶已被普通战斗摧毁，没有伪造伤害回执或据此登记命中通过。未执行整帧CPU/GPU性能实测。

## 正式地图保存与玩家入口

`/Game/Maps/LVL_CommanderMassPrototype` 已保存动画操作说明，只修改已有Note文字；未新增部署或重烘焙地形。保存后[场景回读](../../data/GroundHUDValidation/Motion20261102/scene-readback.json)确认：

| 实体 | 保存位置 | 用途 |
|---|---|---|
| GroundHUD_v1_PlayerEntry | (-2800,54000,1229.04) | 动画操作和验收说明 |
| GroundRogue_Entry | (-2800,54000,1149.04) | Ground入口 |
| GroundMobility_Entry | (-2800,50000,1149.04) | 相邻机动区 |
| GroundHUD_ShieldTarget | (-5000,53000,905) | 蓝队1号兵，400生命/500护盾，静止不开火 |
| GroundHUD_KillTarget | (-6200,53000,905) | 蓝队1号兵，20生命/0护盾，静止不开火 |

正式角色仍为Model4001与A4B2骨架网格；表71行/动效36行、资源认证有效、脏包为空。结束自建PIE，编辑器保留正式地图，恢复开局席位询问和下次PIE窗口3840×2037。

玩家重新Play选择Ground，用左/右键、Space、Shift、M、I、F4检查本轮动画；真实命中X请对尚存活敌人测试。文字大小、遮挡、双准星辨识、短促力度和三种画幅由玩家验收。本轮不读取截图或视频帧，编译/数字证据不替代玩家观感。
