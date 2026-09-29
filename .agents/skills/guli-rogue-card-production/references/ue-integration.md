# UE资产组织、交互与交付

以下为2026-09-28的当前目录和接口。商城源只读；历史制作脚本、README中的旧目录可能通过redirector仍可加载，但不是新资产归属。

## 目录归属

一级按卡牌类型，二级按适用单位（有单位时），三级按效果：

```text
/Game/GuLiStrike/Cards/
  Commander/WM01/
    FireRate/Materials, Textures
    MissileDamage/Materials, Textures
    MoveSpeed/Materials, Textures
    MissilePod/Materials, Textures
    RainSalvo/Materials, Textures
```

尚未接通的类型可采用语义清晰的Ship、GroundMech、Building目录；这是命名建议，落地前读项目已有对应目录，不宣称其运行时已实现。无适用单位时跳过单位层，不伪造WM01。

共用资源：

| 用途 | 当前根或资产 |
|---|---|
| 卡牌/流程蓝图、输入、卡背、闪光 | `/Game/GuLiStrike/CardSystem/RevealDemo` |
| 共享美漫父材质、无字框、文本控件、评审资源 | `/Game/GuLiStrike/CardSystem/WarMachineTarot` |
| 卡牌/公共文本数据 | `/Game/GuLiStrike/Data` |
| 部队升级Niagara | `/Game/GuLiStrike/FX/RogueCards` |
| 商城原牌参考 | `/Game/Assets/card/RewardCards`、`/Game/Assets/card/ParallaxCardMaterial` |

三张正面材质为各效果目录下 `Materials/MI_FireRate_ModelComic_v9`、`MI_MissileDamage_ModelComic_v9`、`MI_HighSpeed_ModelComic_v9`。机动美术名HighSpeed与目录MoveSpeed映射见 `Scripts/Cards/card_asset_layout.py`。FrameMaterial是共用 `.../CardSystem/WarMachineTarot/Materials/MI_WarMachineFrame`，不要为不同卡名复制UI文字贴图。

2026-09-29新增 `MissilePod/Materials/MI_MissilePod_ModelComic_v1`、`RainSalvo/Materials/MI_RainSalvo_ModelComic_v1`，当前绑定 Production_v4 的独立全画布图层。原图、分层、背景外沿及UE预览分别留档；涉及数据反射类型变更时，先按已有授权完成编译加载，再导入卡表。

通过UE AssetTools迁移/重命名并保存引用；不要文件系统移动二进制资产。保留仍被引用的redirector，不能因目录整理强制删除。新建版本记录旧引用用于回退；保留旧源图。

## 演示和实战分离

- 原牌对照：`/Game/GuLiStrike/CardSystem/RevealDemo/Maps/LVL_CardRevealDemo`。
- 美漫评审：`/Game/GuLiStrike/CardSystem/WarMachineTarot/Maps/LVL_WarMachineTarotReview`，Actor `WarMachine_CardDirector`。
- 实际交付：`/Game/Maps/LVL_CommanderMassPrototype`，入口Note标识 `RogueCards_F4_Entry`。

评审地图不发奖励，可重播。实战使用现有指挥官相机与战场，F4打开卡牌；不要为了演示替换战斗GameMode或暂停整局。

## 现有组件与流程接口

卡牌Actor `BP_ParallaxRevealCard`：位置根 → HoverPivot → FlipPivot → 正反面视觉/框/文字，飞行、压动、翻面互不覆盖。实战位移采用展示根局部坐标。稳定命中平面不跟随小角偏转；以鼠标相对中心坐标计算压动，不移动牌面中心。

管理器 `BP_CardRevealDirector` 复用接口：

- `StartPresentation()`、`OnPresentationFinished(SelectedIndex)`，索引0左/1中/2右。
- `CardFrontMaterials`、`CardTextMaterials`各三项，`SetArtwork()`应用素材；CardTextMaterials承载框/UI材质，不是正式标题说明数据源。
- 实战 `UseLiveCardData`、`LiveCardIds`、`ExternalConfirmation`；第二次点击发送 `OnConfirmationRequested`，服务器成功后 `CompleteConfirmation()` 才继续闪光。
- `ActiveCardCount` 支持1/2/3张合格卡；仅实际卡参与布局、命中和交互，零张在原生入口显示暂无可选牌。Esc退出不丢弃服务器上的未确认选择，重开保留最新候选。
- 蓝图提供的动画接口与通用效果类分工不同，不能让每个效果类各复制一套选牌流程。

| 阶段 | 当前节奏与输入 |
|---|---|
| 入场 | 正面左中右各0.5秒，前一张落位下一张开始，总1.5秒；从中心远处缓出，全部完成才响应 |
| 待选择 | 中心不动，鼠标所在一侧压向屏幕内，每轴±16°；离开/失焦平滑回正，无悬停前移或持续漂浮 |
| 翻面 | 第一次点选锁定；0.45秒绕竖直轴翻180°，其他两张回正且不能改选；期间忽略点击 |
| 待确认 | 只所选牌可悬停和点；必须翻完后的新一次按下，不能连点穿透阶段 |
| 确认/闪光 | 实战等待权威结果；成功局部白金闪光0.25秒，避免全屏闪白 |
| 退场 | 三张同时0.5秒回中心远处；所选保持卡背，另两张正面 |
| 结束 | 清理；评审显示重播，实战直接回战场，F4可再次打开 |

面积2、厚度2、视差4独立参数见分层参考。初始20%/50%/80%中心布局与宽高限制在不同宽高比下重新读取/核验，不因倍增面积承诺永不碰边。

## 卡牌单行富文本

`WBP_CardText` 保持原标题、说明区位置及尺寸。`CardDescription` 使用RichTextBlock，`SetContent`和原生`SetLiveCardText`同步赋值；共享 `/Game/GuLiStrike/CardSystem/WarMachineTarot/UI/DT_CardTextStyles` 包含Default、Unit、Gain三行。Default为现有38号暖白，Unit/Gain为同字号黄色 `#FFD84A`、Bold并使用1像素同色描边，使缺少独立粗体字面的中文回退字形也加粗。颜色从sRGB转线性后存储。

这些样式值只在`GuLiStrikeRogueCardUI.xlsx/TextStyles`维护，经正常导出生成原生行结构和`/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCardUI_TextStyles`。UMG样式表由源数据表转换，不在Python、C++或UE资产中单独调数。导入采用原地填充，保存后按实际UE导出的Slate结构文本解码回读字体和颜色。

AutoWrapText=false、WrapTextAt=0；沿用DescriptionFit的ScaleToFit/DownOnly，完整显示一行而不裁断或省略关键增益。说明HitTestInvisible且无Tooltip，不新增输入或焦点归属。`Scripts/Cards/card_rich_text.py`为可复用制作入口；`apply_single_line_card_text.py`在匹配原生构建已加载、UE空闲时定向导入文本、迁移控件并保存回读真实F4入口。不得在游玩期间执行，不因文案修改重跑卡图、材质或整套选牌图表。

## 显式重选

原生 `GuLiRogueCardOverlay` 在底部右侧创建重选按钮；Phase 1（选择）/3（翻面完成待确认）可用，动画、提交和等待重选时禁用。按钮独立命中，禁用时也阻止向卡牌穿透。UI只请求 `ServerRerollRogueCards`；服务器复核拥有者、队伍、局次和候选资格，优先换入未显示的合格牌，无其他牌则保留当前选择并提示。重选不改变获取记录，不发奖励；替换Session阻止旧确认，重复请求返回同一结果。

成功重选只销毁旧卡牌、捕获与渲染目标并重建展示；覆盖层、模糊和战场输入锁保持。失败可恢复原牌，Esc重开保留最新候选。六条文案使用 `UI.RogueCards.Reroll*`、`UI.RogueCards.NoAlternatives` 公共文本。代码、资产保存回读、编译和玩家点击验收分别记录。

## 模糊、捕获与输入

实战使用UMG BackgroundBlur，当前强度8、打开/关闭各0.2秒。只捕获卡牌的SceneCapture输出透明画面合成在模糊层上方，卡框、WidgetComponent文字与闪光一起保持清晰。捕获只在选牌期间运行。

打开时取消拖选、建造预览、技能瞄准，隐藏指挥官HUD并锁定本地战场操作。UMG处理LMB/Esc，装饰文字不吞命中。提交前Esc取消不加成，提交中拒绝重复操作。退出按当前角色恢复HUD、输入和焦点。

正常结束、取消、初始化失败、角色切换、EndPlay共用幂等清理：模糊归零、移除覆盖层、停捕获、释放卡牌/渲染目标/引用；重复关闭不重发完成事件或升级事件。

### 已确认故障的诊断入口

| 现象 | 检查与约束 |
|---|---|
| 按F4场景白模 | 引擎继承F4→`viewmode lit_detaillighting`调试绑定；配置解除并在指挥官输入初始化去掉遗留项。F3可临时回Lit。先查ViewMode，不重做建筑材质，也不凭截图认定一定是模糊残留 |
| 卡牌灰蒙蒙 | 卡牌捕获的Fog/VolumetricFog/Atmosphere等；注册组件会重置裸ShowFlags，应通过ShowFlagSettings或注册后有效设置。同时核对透明合成，不盲改原图饱和度 |
| 看得见但点不了 | 检查覆盖控件LMB入口、焦点和命中索引；HUD文本不应拦截。避免以静态材质正常推断输入已通 |
| Alt+Tab还倾斜 | 已有最小原生焦点接口；保留应用焦点与鼠标离开回正。不要退回仅靠不可靠的纯蓝图窗口失焦检测 |
| 图层边缘出现矩形/空洞 | 按分层参考核对外扩、共同UV、背景补全、固定窗口；不是继续加Bloom |

原生入口：`Source/GuLiStrike/Gameplay/Cards/GuLiRogueCardPresentation.*`、`GuLiRogueCardSettings.*`；输入在现有Commander PlayerController/NetSync范围内查找。

## 工具与脚本选择

优先当前可用UE专用API；本机可通过 `python Scripts/ue_exec.py <脚本>` 使用编辑器12029接口。先只读确认项目、地图、PIE、脏包与类型可用性，再进行本轮明确修改。无连接时推进可离线工作，不能称已接入当前UE。

| 脚本 | 用途与边界 |
|---|---|
| `Scripts/Cards/card_asset_layout.py` | 当前目录映射，可只读复用 |
| `Scripts/Cards/migrate_card_asset_layout.py` | UE迁移；仅需要迁移时调用 |
| `Scripts/Cards/author_rogue_card_entry.py` | 分阶段部署：迁移、表、卡牌图表、确认契约、捕获材质、地图；不要为一处小改动整套重跑 |
| `Scripts/Cards/readback_rogue_card_entry.py` | 历史三卡入口核对，包含已移除Candidates假设；勿直接用于当前五卡规则 |
| `Scripts/Cards/finalize_rogue_upgrade_scene.py` | 历史固定三卡场景脚本，勿用于当前入口以免覆盖重选说明 |
| `Scripts/Cards/prepare_rogue_card_reroll.py` | 定向导入卡/文本、重建StringTable，保存并回读指定F4入口；不启动PIE，运行前需已加载卡表反射类型 |
| `Scripts/Cards/apply_single_line_card_text.py` | 匹配原生模块构建加载后接入单行黄色粗体；原地导入公共文本和Excel样式表，生成UMG样式并保存回读控件及F4入口，不启动PIE |
| `Scripts/Cards/apply_modelcomic_v9.py`、`author_warmachine_cards.py` | 历史制作来源，含旧路径/旧版/固定图像尺寸；用于参考，不原样运行覆盖当前资产 |
| `Scripts/Cards/validate_*`、`run_*preview*`、`benchmark_*` | 部分启动PIE/创建夹具；只有当前任务相应授权时运行，不当作默认检查 |

仅保存修改过的明确项目资产。编辑器/原生模块重启前核对当前任务授权和脏包，保留用户未保存工作。渲染目标编辑曾因超限警告在Slate回调中触发模态重入；预览时先验证支持尺寸、避免在tick回调里设置会弹窗的编辑器属性。

## 收尾

完成静态检查与所需蓝图/材质编译，按任务授权做运行检查。保存对应地图并回读实体配置与引用；技术检查、运行时交互、性能、用户画面审核分开记录。F4→单击翻面→再次确认是实际玩家入口，QA直接调用命中函数不能当作真实鼠标命中验收。

向用户给出地图、当前代码是否已加载、操作步骤和仍待核验项。调整比率后覆盖16:9、16:10、21:9；没有实际执行的档位标未验证，不能用其他比例截图代替。
