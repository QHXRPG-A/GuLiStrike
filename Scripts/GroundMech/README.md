# 轻型装甲地面玩家

在源码版 UE5.7 中打开 `/Game/Maps/LVL_GroundMech_Demo`，使用普通单人 PIE。地图配置 `BP_GroundMech_DemoMode`，优先分配 Ground 身份；正式战局通过现有 `RolePawnClasses` 使用 `BP_GroundMech_Light`。

| 操作 | 行为 |
|---|---|
| WASD | 按屏幕方向移动，走速 720 cm/s |
| 按住 Shift | 跑速 1,440 cm/s；斜向归一化 |
| 按住空格 | 火箭跳持续向上推进；松开后保留惯性并下落 |
| 空中 WASD | 改变飞行方向，水平速度上限 1,080 cm/s，Shift 不增加空中速度；下身按实际速度转向 |
| 按住鼠标左键 | 地面和空中持续开火，松开停止；瞄准空处也能射击 |
| 鼠标 | 上身水平瞄准，腿部朝移动方向 |
| 滚轮 | 镜头距离 3–8 倍机甲高度，默认 4 倍 |
| B、数字键 | 进入既有建造模式、按原权限选择建筑 |
| 右键 / Esc | 取消当前建造等既有操作 |
| F10 | 既有 GM 界面；关闭后恢复 Ground 输入 |

Actor 缩放为 1，模型比例 2.0512617，基准高度 748.3796 cm。胶囊半径 230 cm、半高 374.1898 cm；模型脚底偏移 -373.1249 cm。固定相机俯角 55°、水平角 0°、FOV 60°。空中下身以540°/秒朝实际水平速度转向；上身和机枪独立瞄准鼠标。

资源在 `/Game/GuLiStrike/GroundMech`。当前 `BP_GroundMech_Light` 引用 `Style_v9/Meshes` 四部件，主体 20,038 三角面、独立轮廓 11,325 面，使用三档明暗和内外线稿。封闭弧面头甲为赭金色，上腿甲为灰蓝色，排气孔带贴壳底座；旧 4,824 面 Default Lit 资源保留为回退来源。四部件分别是腿、装甲、肩部与机枪；正式机枪已按2026-09-21方案启用。动画使用项目副本、可编辑AnimGraph和原轻型腿骨架。

`ABP_GroundMech` 的 Root Motion Mode 必须为 **Ignore Root Motion**：源走跑和转向序列包含根位移，需要从姿态提取后丢弃，由 CharacterMovement 统一移动角色。保留默认的 Montages Only 会让根骨在动画循环内离开胶囊、循环结束时回跳。此配置已保存，并通过独立编辑器进程重新加载、编译验证。

建造仍受距离、资源和角色权限限制。原 Demo 保留模型与操控验证用途；2026-09-20 新增的开火候选入口见下节。

源码职责：`GuLiGroundMechCharacter`处理输入、瞄准、相机和占有生命周期；CMC驱动位移与转向；`GuLiGroundMechAnimInstance`只读取运动事实，`ABP_GroundMech`选择姿态、过渡并旋转双髋骨；Blueprint配置装配。Controller管理共享Enhanced Input、鼠标和界面，GameMode配置角色出生。瞄准通过ControlRotation及量化Yaw同步。

验证证据在 `TestResults/GroundMech`：`asset-validation.json`、`acceptance-summary.json`、`root-motion.json`、`network.json`、`regression-summary.json`、`buildids.json`、最终 Editor/Game 构建日志与 `PIE_final.png`。根位移修复后连续跑动采样中，根骨偏移为 0、最高速度为 1,440 cm/s，没有直接设置角色位置。

`verify_play.py` 只用于无人操作的专用 PIE：它会在用例之间重置角色位置，不能在用户游玩时运行。`observe_movement.py` 只采样当前角色，不输入、不移动角色。`verify_network.py` 复用项目既有双端 PIE 入口，连续移动，不传送角色。

当前外观同步见[交付说明](../../ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9/README.md)。同步时发现当前会话动画蓝图的 CDO 设置正确，但实例初始化仍使用旧 MontagesOnly；重编译刷新类初始化数据后保存，独立进程在编译前新建实例也取 IgnoreRootMotion。`import_assets.py` 已补上修改默认值后的编译步骤。本轮复跑连续跑动与双端同步，通过；未重跑前述完整玩法回归，也没有原生代码修改。

Spider风格版入口为 `/Game/GuLiStrike/Mechs/SpiderMech/BP_SpiderMech_Styled`，继承源商城蓝图与动画蓝图，已放在同一测试地图。其完整网格和描边面数、截图与性能验证边界见交付说明。

整批制作资源现已全部同步，见[八项资源总结](../../ArtSource/Mechs/StyleUnification_20260919/UE_AllAssets_v10/README.md)。独立展示关卡为 `/Game/GuLiStrike/Mechs/StyleShowcase/LVL_MechAsset_Showcase`。Mecha_01/02、三管炮、导弹武器及独立导弹均有项目资源与展示蓝图；当前玩家仍使用原Lv1机枪，其他武器未接入射击/换枪。本轮完整同步没有修改Ground操控或Demo地图。

## 2026-09-20 玩家机枪候选

在源码版 UE5.7 打开 `/Game/Maps/LVL_GroundMech_FireReview`，单人 PIE 即可试玩。候选 `BP_GroundMech_FireReview` 启用机枪组件：鼠标控制水平瞄准及枪管俯仰，按住左键连射，松开停止。建造、UI、失焦、解除占有和行动锁会释放持续开火。T 仍是既有指挥官施法入口，Ground 只具备被传送资格。

默认升级 `1.1`。服务器可调用角色 `Weapon.ApplyUpgradeById("1.2")` 或 `"1.3"`；无效 ID 返回 false，客户端不能自行切级。升级影响下一发及剩余冷却，已飞出的弹丸保留原伤害。经验和升级选择界面尚未实现。

唯一数值入口为 [GuLiStrikeMech.xlsx](../../Data/Excel/GuLiStrikeMech.xlsx)，三行元数据、两张中文 Sheet。升级 ID 为文本，三级射速/伤害分别为 2/10、4/15、6/20；技能表维护弹速 12000 cm/s、寿命 5 秒、半径 15 cm、曲线与资源引用。资源软引用须写完整 `包路径.对象名`。修改表后：

1. `python Tools/DataPipeline/export_data_from_excel.py` 导出 JSON 和行结构。
2. 若行结构变化，关闭编辑器并按项目规则构建源码 Editor/Game。
3. 在停止 PIE 的源码编辑器中经 `Scripts/ue_exec.py` 执行 `author_fire_assets.py`，只导入两张机甲表和候选资产。该脚本使用带 UTF-8 BOM 的 CSV 中间文件，避免中文注释损坏。

`author_fire_niagara.py` 从项目副本制作单枚弹道和逐发枪口候选；商城源资产保持只读。机枪使用 `ABP_GroundMech_Machinegun` 和五键平滑曲线，`Barrel_big` 从 Z=188.101471 回缩到 152，再于 0.15 秒复位。Mesh-only `Muzzle` 挂在 `Barrel_end`，跟随后坐；运行时校正镜像武器装配对特效朝向的影响。

授权专项验证入口为 `verify_fire.py`（隔离双端 PIE）和 `GuLiStrike.GroundMech.Fire`（原生自动化）。验证脚本会注入输入、切级、解除占有并恢复，勿在用户游玩中运行。结果在 [Fire 证据目录](../../TestResults/GroundMech/Fire)；VibeUE 布尔读写补丁的可重放入口为 `Scripts/PluginPatches/fix_vibeue_niagara_bool.py`，插件本体被项目 Git 忽略，更新插件后须重新应用并构建。

2026-09-20的候选技术结果与审核状态记录在[开火开发文档](../../Progress/DevelopmentDocumentation/20260920-玩家地面机甲开火与升级配置.md)。2026-09-21按用户确认的空中战斗方案，正式 `BP_GroundMech_Light` 已绑定两张表、机枪动画并启用武器；本次视觉验收仍单独记录。

## 2026-09-21 火箭跳

火箭跳接入正式Ground机甲与FireReview候选。构建、导入和实机验收的当前状态见[开发记录](../../Progress/DevelopmentDocumentation/20260920-地面机甲火箭跳.md)。

空格持续推进，容量100，每秒消耗20；落地并松开空格1秒后每秒恢复20。空中有余量可以再次推进；耗尽后需松键、落地恢复，不会持续按住自动起飞。最高上升1200cm/s，普通地面与有效Mass顶部均可起飞。

机甲右侧空间弧条仅自己可见，20格从顶部减少；≥50%绿色、20%–不足50%黄色、不足20%红色。使用立即全亮，容量未满时常驻显示，回满且未使用时1.5秒渐隐。容量条高度400cm；空中WASD以走速1.5倍转向，双喷口随方向偏转最多15°。有效空格按下即时点火，不等待离地；松键、耗尽或中断结束发射并自然消散。界面接管、建造、失焦、行动锁、解除占有和传送停止推进。

在Excel“技能表”修改2/RocketJump的容量、推力、挂点、火焰缩放和UI配置，运行原导出管线；结构变化须先构建源码Editor，再通过`Scripts/ue_exec.py`执行`Scripts/GroundMech/author_rocket_jump_assets.py`导入和接线。该脚本只更新技能表及本技能表现/输入，保留机枪数值和升级表资产。

使用项目规则中的源码Editor/Game完整构建命令和`-NoHotReloadFromIDE`。共享引擎产物的Live Coding选项应保持一致，切换`-NoLiveCoding`会使PCH及依赖缓存失效。客户端与服务器必须使用同一次协议17构建。验证产物位于`TestResults/GroundMech/RocketJump`，用户视觉审核不以编译或技术采样代替。

## 2026-09-21 动画蓝图、地空开火与下落调参

当前腿部动画蓝图：`/Game/GuLiStrike/GroundMech/Animations/ABP_GroundMech`。打开AnimGraph的`GroundMechLocomotion`查看地面、起飞、空中循环、落地四状态；普通融合0.1秒，自然跌落跳过起飞，再次离地可中断落地。`UpdateFlightLean`管理15°和1秒进出；只旋转Hip_L/R，不影响上身挂点。速度10cm/s进入、5cm/s退出，方向取实际水平速度。

商城22段动画复制在`Animations/Source/Legs`和`Animations/Source/Weapon`；项目`BS_GroundMech_Locomotion`的9个采样引用这些副本。`author_animation_assets.py`负责复制、图表制作及正式武器接线；默认保留已制作的蓝图图表，显式传入`REBUILD_ANIMATION_GRAPH=True`才重建。`import_assets.py`已沿用该入口。

正式`BP_GroundMech_Light`绑定两张机甲表、默认升级1.1及机枪动画。射线未命中时使用远端目标；推进、滑行、下落和落地期间都允许持续射击，输入中断规则保持一致。

推力明确保持2180cm/s²、上升上限1200cm/s。根据下落慢的反馈，Excel新增`FallGravityMultiplier=2`，仅对无有效推进且已经下降的阶段生效；重新推进恢复原重力。构建与实机状态见[本次开发记录](../../Progress/DevelopmentDocumentation/20260921-地面机甲动画蓝图与空中战斗.md)。

2026-09-21本轮收尾：2倍下落已实测约1960cm/s²；上升参数保持。Mass顶面向上离开时不再误判侧碰，已实际验证从同一单位顶部起落并恢复容量。用户已确认空处射击与当前双喷、腿部倾斜和新版下落；容量条独立视觉终验及未覆盖专项见开发记录。最终源码构建、13项既有回归、联机采样及恢复记录位于`TestResults/GroundMech/Animation`。


## 2026-09-21 双阵营停火靶场

`/Game/Maps/LVL_GroundMech_FireReview` 已配置四个真实Mass部署点：红蓝双方各4台扫荡者、4台战争机器，总计16台，默认不自动开火。单人玩家是红方，蓝方敌军按本地阵营显示红色描边，引用现有特效ID 15。保留血量、受击、销毁、碰撞和友军接触退让。

玩家起点位于XY=(8600,-7500)/(9400,-7500)，两组部队在X约5000/13000、Y约-5000至1000的区域。向部队接近、滚轮拉远镜头，左键射击蓝方检查受击与销毁；重新进入游戏可重置目标。后续按用户要求检查其已开启的双端PIE，修复装饰石持续更新导航导致的生成阻塞，主机与客户端均已读回16台存活单位。实际描边与命中销毁视觉效果仍待玩家确认。

定向脚本为`author_fire_review_range.py`，只作用于上述地图，不启动游玩：

- `prepare`：排除12个动态装饰网格的导航影响，设置两个原PlayerStart与导航边界，保存地图并请求导航构建。
- `repair_navigation`：仅修正这12个装饰组件，完成导航构建并保存；不改部署、起点、视觉或碰撞。适用于旧版靶场升级。
- `deploy`（默认）：导航完成且新原生字段已加载后，创建或更新四个2×2部署点并设置停火，保存地图。
- `readback`：读取部署、导航、装饰组件排除项、起点、角色与描边配置，输出结果。

在编辑器Python中用`runpy.run_path('D:/UE5.7/test1/Scripts/GroundMech/author_fire_review_range.py', init_globals={'FIRE_REVIEW_STAGE':'prepare'})`选择阶段；导航完成后依次执行`deploy`和`readback`。已配置地图可直接通过`python Scripts/ue_exec.py Scripts/GroundMech/author_fire_review_range.py`重复部署，不会叠加部队。如重新运行旧的全量`author_fire_assets.py`，最后重跑本脚本三个阶段以恢复靶场起点。

装饰导航排除只覆盖本图的`Floating_Rock_2`、`Floating_Rock_Light_BP_5`两个悬浮石Actor的5个动态网格，以及`Spinning_Rock_BP_8`、`Spinning_Rock_BP2_11`的`RockFlat2`。旋转石的静止`RockLong2`底座仍参与导航。修改Blueprint组件属性可能重新构造其他组件，脚本按名称重新查找并在全部修改后再次读回；不修改共享蓝图资产，也不绕过士兵出生位置校验。

红方扫荡者中心从计划的X=5000调至5500，避开距导航415cm的无效位置；其他中心与间距保持计划值。证据在`outputs/firereview-20260921`，完整记录见[开发文档](../../Progress/DevelopmentDocumentation/20260921-FireReview双阵营靶场与敌方描边.md)。
