# 轻型装甲地面玩家

在源码版 UE5.7 中打开 `/Game/Maps/LVL_GroundMech_Demo`，使用普通单人 PIE。地图配置 `BP_GroundMech_DemoMode`，优先分配 Ground 身份；正式战局通过现有 `RolePawnClasses` 使用 `BP_GroundMech_Light`。

| 操作 | 行为 |
|---|---|
| WASD | 按屏幕方向移动，走速 720 cm/s |
| 按住 Shift | 跑速 1,440 cm/s；斜向归一化 |
| 鼠标 | 上身水平瞄准，腿部朝移动方向 |
| 滚轮 | 镜头距离 3–8 倍机甲高度，默认 4 倍 |
| B、数字键 | 进入既有建造模式、按原权限选择建筑 |
| 右键 / Esc | 取消当前建造等既有操作 |
| F10 | 既有 GM 界面；关闭后恢复 Ground 输入 |

Actor 缩放为 1，模型比例 2.0512617，基准高度 748.3796 cm。胶囊半径 230 cm、半高 374.1898 cm；模型脚底偏移 -373.1249 cm。固定相机俯角 55°、水平角 0°、FOV 60°。

资源在 `/Game/GuLiStrike/GroundMech`。当前 `BP_GroundMech_Light` 引用 `Style_v9/Meshes` 四部件，主体 20,038 三角面、独立轮廓 11,325 面，使用三档明暗和内外线稿。封闭弧面头甲为赭金色，上腿甲为灰蓝色，排气孔带贴壳底座；旧 4,824 面 Default Lit 资源保留为回退来源。四部件分别是腿、装甲、肩部与机枪；机枪目前仅为装配表现。动画复用原轻型腿骨架和九样本混合空间，独立动画实例只读取实际速度和转向。

`ABP_GroundMech` 的 Root Motion Mode 必须为 **Ignore Root Motion**：源走跑和转向序列包含根位移，需要从姿态提取后丢弃，由 CharacterMovement 统一移动角色。保留默认的 Montages Only 会让根骨在动画循环内离开胶囊、循环结束时回跳。此配置已保存，并通过独立编辑器进程重新加载、编译验证。

建造仍受距离、资源和角色权限限制；此轮没有增加射击、伤害、成长或闪避。测试地图保留原 Demo 环境、灯光和曝光，并放置战争机器尺寸参照。

源码职责：`GuLiGroundMechCharacter` 处理移动、瞄准、相机和本地占有生命周期；`GuLiGroundMechAnimInstance` 处理动画；Blueprint 配置装配；Controller 管理共享 Enhanced Input、鼠标和界面；GameMode 只配置角色出生。移动沿用现有 CMC 与外部位移组件，瞄准通过 ControlRotation 及量化 Yaw 同步。

验证证据在 `TestResults/GroundMech`：`asset-validation.json`、`acceptance-summary.json`、`root-motion.json`、`network.json`、`regression-summary.json`、`buildids.json`、最终 Editor/Game 构建日志与 `PIE_final.png`。根位移修复后连续跑动采样中，根骨偏移为 0、最高速度为 1,440 cm/s，没有直接设置角色位置。

`verify_play.py` 只用于无人操作的专用 PIE：它会在用例之间重置角色位置，不能在用户游玩时运行。`observe_movement.py` 只采样当前角色，不输入、不移动角色。`verify_network.py` 复用项目既有双端 PIE 入口，连续移动，不传送角色。

当前外观同步见[交付说明](../../ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9/README.md)。同步时发现当前会话动画蓝图的 CDO 设置正确，但实例初始化仍使用旧 MontagesOnly；重编译刷新类初始化数据后保存，独立进程在编译前新建实例也取 IgnoreRootMotion。`import_assets.py` 已补上修改默认值后的编译步骤。本轮复跑连续跑动与双端同步，通过；未重跑前述完整玩法回归，也没有原生代码修改。

Spider风格版入口为 `/Game/GuLiStrike/Mechs/SpiderMech/BP_SpiderMech_Styled`，继承源商城蓝图与动画蓝图，已放在同一测试地图。其完整网格和描边面数、截图与性能验证边界见交付说明。

整批制作资源现已全部同步，见[八项资源总结](../../ArtSource/Mechs/StyleUnification_20260919/UE_AllAssets_v10/README.md)。独立展示关卡为 `/Game/GuLiStrike/Mechs/StyleShowcase/LVL_MechAsset_Showcase`。Mecha_01/02、三管炮、导弹武器及独立导弹均有项目资源与展示蓝图；当前玩家仍使用原Lv1机枪，其他武器未接入射击/换枪。本轮完整同步没有修改Ground操控或Demo地图。
