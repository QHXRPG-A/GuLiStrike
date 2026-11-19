# Ground HUD：HUDuiPRO 短动效接入

**会话状态：已完成（2026-11-03，按用户收尾指令）。** 正式资源与最终结构化证据保留；旧过程日志、截图和一次性回执已按清单清理，正文中的历史链接只记录当时验证。未执行的视觉或完整性能验收未改记为通过。


本轮保留左下生命与无单位属性摘要、16/18 号属性文字及现有布局，只补表现动画。原包不变，不新增媒体播放器；没有调用生图模型，也没有新增生图提示词。

## 资源与可复现来源

来源：[81 组件索引](../../HUDuiPRO/Components20261030/Catalog.md)。[manifest.json](manifest.json) 记录源序列、逻辑帧号、每帧 SHA256、输出 SHA256、尺寸和图集格子参数。源序列为 24 fps，输出由事件指定播放时长。

| 派生纹理 | 原组件 / 帧 | 单帧 | 图集 | 用途 |
|---|---|---|---|---|
| T_GroundHUD_Intro | Panel_1 / 0–18 | 1024×576 | 4112×2900 | 0.45 秒入场，0.18/0.12 秒模态开合 |
| T_GroundHUD_Cast | Box_1 / 0–12 | 256×256 | 1040×1040 | 0.18 秒技能展开亮边；瞄准和持续喷气的末帧高亮 |
| T_GroundHUD_Ready | Circle_Elements_03 / 0–18 | 256×256 | 1040×1300 | 0.30 秒展开、0.10 秒淡出 |
| T_GroundHUD_Cooldown | Circle_Elements_Bars_01 / 12 | 256×256 | 260×260 | 实时闪现冷却扇形 |

全部为 RGBA，采用预乘透明度缩放、每格 2 像素留边、双帧预乘插值；无 mip、UI 压缩、不流送。原始 RGBA 共 55.03 MiB，UE 运行回读的实际纹理驻留共 **56.25 MiB**，低于 64 MiB。预算只指本次四张派生动效纹理，不是整套 HUD 或整帧显存。

正式目录：`/Game/GuLiStrike/GroundMech/UI/Motion/Textures` 和 `Materials`。两个透明 UI 材质是 `M_GroundHUD_Atlas`、`M_GroundHUD_Cooldown`；由原 HUD Excel 的 36 个新增配置行引用，表结构未改，导出后共 71 行。图集使用显式 `Frame` 参数，不用材质 Time 节点；冷却材质从顶部顺时针推进，数字及键位位于其上方。

制作：[prepare_ground_hud_motion.py](../../../../Scripts/GroundMech/prepare_ground_hud_motion.py)。导入与逐字段回读：[author_ground_hud_motion.py](../../../../Scripts/GroundMech/author_ground_hud_motion.py)。重新生成需保留现有源 PNG；脚本只读指定帧，不枚举商城二进制目录。

## 正式接线

- `GuLiGroundPresentationEvent.h` 定义本地只读通知；武器在有效发射去重与过期过滤后发布开火，角色发布瞄准，闪现组件发布实际开始、结束和冷却结束。喷气复用已有表现状态通知。
- `GuLiGroundHUDData` 统一订阅、帧末合并和解绑。连续射击一帧最多合并三次；普通文字与数据保留 0.2 秒定时器，不追赶卡顿期间的多次刷新。恢复事务先重建状态基线，隐藏时清除暂存事件。
- `GuLiGroundHUDMotion.cpp` 管理固定数量短片段、入场位移和透明度、持续高亮与逻辑时钟冷却圆环。只有闪现显示冷却环，喷气继续显示燃料，不由 UI 判定可用。
- 双准星中心和枪口投影不移动；只有四臂扩张与切向轻抖，每发 4 设计像素，上限 12，抖动不超过 1.5，0.18 秒回弹。真实伤害回执仍由既有去重链路提供，命中 X 收缩淡出 0.16 秒、击杀 0.28 秒；同帧击杀优先。
- 入场首次 Ground 和重生触发；地图、详情关闭不重复。模态退出动画结束才归还游戏焦点；卡牌接管、销毁等强制边界立即关闭。隐藏时清除动画、暂停普通刷新，显示时只初始化当前状态。
- `UGuLiGroundHUDAssetLibrary` 的探针仅属于 Editor：读取动画状态和纹理内存、执行真实瞄准/中断输入及准备低燃料。生产 HUD 不调用探针、不发送新网络请求，专服不创建这些界面资源。

## 验证边界

当前证据目录：[Motion20261102](../../../../data/GroundHUDValidation/Motion20261102/)，总入口为 [delivery-summary.json](../../../../data/GroundHUDValidation/Motion20261102/delivery-summary.json)。源帧与图集哈希、RGBA/透明首帧、帧范围、材质参数、Excel 导入与资产保存已检查。`GuLiStrikeEditor Win64 Development` 最终构建4步/6.04秒成功，源码引擎与项目BuildId一致，新代码已重启加载。

| 检查 | 结果与证据 |
|---|---|
| 原始资源和正式资产 | `atlas-validation.json`、`asset-import.json`：四图集、两材质、71行配置保存回读，原始55.03MiB / 驻留56.25MiB |
| 单发、连射、无效输入 | `runtime-events.json` / `runtime-first-summary.json`：有效发射与HUD脉冲一致，连续开火不排队；4.9985Hz普通刷新，无同帧多轮追赶 |
| 技能与隐藏 | `runtime-final.json` / `runtime-final-summary.json`：13项检查通过，含喷气耗尽/恢复、闪现中断/CD结束、瞄准渐变、隐藏停止普通刷新和短动画 |
| 选牌联动 | 最终材质准备完成、进入可选阶段，取消与重开后恢复HUD，入场/就绪不追播；未改变直接选择、卡池和插画 |
| 布局 | `aspects.json` / `aspects-summary.json`：实际近16:9界面的9个面板边界在内；16:10和21:9请求没有改变实际HUD比例，不能登记这两种画幅通过 |
| 场景保存 | `scene-readback.json`：5个相关实体、4001/A4B2、资源认证与71行配置回读；无脏包，PIE已结束，恢复下次窗口设置与席位询问 |

真实命中/护盾吸收/击杀X、重复伤害回执、死亡重生、跨局恢复和换席位，本轮完成源码路径检查，未登记完整运行通过。第一次喷气恢复采样尚在空中、早期选牌材质门控失败、Python不能反射FGeometry等尝试均保留原始证据；最后独立复查通过的项目单列，不抹去失败记录。

选牌准备卡住的原因是 `HasValidGameThreadShaderMap` 要求全量编译完成标记，但当前按需着色器缓存已有完整ShaderMap却没有该标记。现用实际平台和 `IsGameThreadShaderMapComplete` 检查，必要时异步提交缺失编译；最终准备成功并进入可选阶段。

没有助手截图、读图或视频帧检查。结构化运行数据和几何边界只能证明事件及布局状态，不能证明动画力度、观感、遮挡和文字实际可读性；这些由玩家验收。未把整帧 CPU/GPU 性能登记为通过。

## 玩家入口

地图 `/Game/Maps/LVL_CommanderMassPrototype`，选择 Ground。沿用 `GroundHUD_v1_PlayerEntry`、`GroundRogue_Entry=(-2800,54000,1149)` 和相邻 `GroundMobility_Entry`；正式角色仍是 ModelId=4001 / A4B2。

1. 入场后观察四周展开；用 M、I 打开再 Esc 关闭，返回战斗不重复整套入场。
2. 单发和连射观察准星四臂与左键技能亮边；对真实敌人造成伤害/护盾吸收观察浅色 X，击杀观察暖橙 X。现有静止靶也会被战斗摧毁，已死亡目标不作为命中检查证据。
3. Space 喷气观察开始亮边、持续高亮；耗尽后松开并落地，首次恢复可用时播放一次就绪环。
4. Shift 闪现观察亮边、顺时针 CD 与冷却结束就绪环；快速重复输入不应多播一次释放。
5. F4 进入原正面直选，取消、重开后确认战斗 HUD 不追播隐藏期间的射击/就绪；切局、重生后再次核对。
6. 在 16:9、16:10、21:9 下确认键位、文字、准星与动画边缘；数值几何检查不能替代这一步。
