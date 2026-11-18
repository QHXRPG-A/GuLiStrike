# 卡牌美术目录与统一导入

日常源为 `data/Excel/GuLiStrikeCardArtwork.xlsx`，使用项目excelize-cli编辑。新增资源先登记，再导出与部署；不再在单卡脚本里维护贴图路径和参数。内置生图初稿、用户Qwen参考图、六层补绘和HTML阶段仍按美术主流程执行。

## 正式制作顺序

1. **定位用户选定版本。** 从指定HTML及其实际加载清单读取每张牌的版本、六层/蒙版路径、完整画布、原图窗口、叠放顺序、运动归属、强度和最大位移，记录源哈希；不从旧脚本默认值或相似标题猜测。
2. **明确绑定和目录。** 核对ArtworkId、玩法CardId、单位/席位、最终MI及HUD IconId；仅美术的牌保持未绑定。缺少图标时沿用用户已授权的选择，不能把一次临时共用图标推广为所有卡的默认。每牌Materials/Textures归对应效果目录，共用材质/控件归CardSystem，原始文件留ArtSource。
3. **以原卡框为约束，再完成映射。** 读取实际运行入口的CardFace、WidgetComponent与卡框开口，核对本卡是否按固定图片区比例制作，再计算六层共同映射及运动边界。卡框外形/大小、文字位置、卡背/厚度和点击范围保持正式基线；预览使用同一正式MI和相同框配置。
4. **登记、导出、定向部署。** Excel是参数源；导出后仅部署本批ArtworkId，回读正式资源和目录表。保存目标玩法行的变更前后值，核对未授权字段和其他行未变。
5. **交付可切换评审场景。** 保存并重新加载对应地图，核对卡牌列表、材质、实际委托与入口，交给用户Play。技术检查和玩家视觉结果分开记录；“打开测试场景”不自动等于助手启动PIE。

## 数据与运行时合同

| 工作表 | 维护内容 |
|---|---|
| Artworks | 稳定整数id、行名、显示标题、TemplateId、最终MI、AssetRoot、源目录/参考图/HTML入口 |
| Templates | 父材质、存储布局和组合规则；以当前源表为准，含图集、独立六层及带互补蒙版的模板 |
| Layers | 每套恰好六个逻辑层，编号、语义、TextureParameter、深度、透明度、Scale/Offset的完整四通道 |
| Parameters | 其余标量/向量及所有贴图参数绑定；Texture对应完整UE对象路径，SourceFile可为项目相对路径；TextureRole为Color或Mask |

层编号不是叠放顺序；组合算法、图集切格、透明处理与前景遮挡由模板实现，表中选择模板。普通图层参数只在Layers维护，禁止再放进Parameters形成第二来源。Layers.TextureParameter引用本牌Parameters中Kind=Texture的Key；图集六层共用BaseColor Map，独立六层使用Layer N Map。

UE图层使用居中映射`p=(CardUV-0.5-Offset)/Scale+0.5`，不能直接照抄HTML的drawImage偏移或把原图左上角的扩边偏移当成UE居中偏移。完整计算见下节。

`GuLiStrikeRogueCards.xlsx / Cards` 以必填整数 `ArtworkId` 指定牌面，源表不再手填FrontMaterial。导出器只把此列解析为既有运行时FrontMaterial，不增加或改名原卡表字段，也不改写Layers/Parameters中的ArtworkId。不同玩法等级可引用同一ArtworkId。

历史初始ArtworkId：1射速、2机动、3导弹伤害、4导弹仓、5雨点攻势、6折射子弹。当前是否绑定玩法须读源表；不把历史编号清单当作可用ID或当前运行状态。地面玩家与WM01身份保持分离。

运行时沿用现有卡牌Actor与最终MI，GPU实时采样、叠合和视差。目录数据表是可浏览的生成物，位于 `/Game/GuLiStrike/Data/DT_GuLiStrikeCardArtwork_*`；不通过运行时读取PNG重新组装材质。

## 卡框比例硬指标

用户已明确要求保持**原来卡框的长宽比**。当前游骑兵Ground卡框以下值是制作硬约束，所有比例均按**宽:高**记录：

| 项目 | 固定值 | 用途 |
|---|---|---|
| 整卡画布/Widget DrawSize | 1280×1556，320:389，约0.822622108:1 | 完整卡片、卡框与文字排版 |
| 图片区归一化开口 | Min=(0.04,0.145)，Max=(0.96,0.84) | 正式卡框中的透明画面窗口 |
| 图片区像素坐标 | [51.2,225.62,1228.8,1307.04] | 对照整卡画布定位 |
| 图片区宽高 | 1177.6×1081.42，约1.088938618:1 | 新插画原图可见窗口的比例 |
| Widget世界高度与统一缩放 | 48.617；缩放=48.617/1556 | 保持原卡框大小与自然字体比例 |

此约束来自`GuLiGroundCardFace.cpp::RebuildWidget`与`GuLiRogueCardPresentation.cpp::SetLiveCardText`，评审脚本保持同值。**整卡比例不是图片区比例，CardFace的30×45网格也不是卡框比例。** 不得把2:3原图、补绘画布或其他席位框型当成上述比例；不得为了迁就卡图把原框改窄/改高。只有用户明确要求重新设计卡框时才修改这项基线。

窗口适配采用统一缩放：窗口改变时调整整牌尺寸、位置和间距，回正状态始终保持320:389。当前`card_presentation_size.py`计算单一CardScale，ApplyFrame将同值用于各轴；原生捕获目标按视口同比例缩放。不得按窗口宽高分别拉伸卡框或把屏幕16:9/21:9当成卡框比例。倾斜时的投影比例变化属于透视；不同窗口档位的结构检查和用户实际验收分别记录，不因公式正确就声称已测全部窗口。

新初稿、用户Qwen参考沟通、分层清单与HTML窗口都以固定图片区比例约束构图，主要对象从一开始安排在该窗口内。外扩画布仅增加视差余量，不改变可见窗口；受工具整像素尺寸限制时记录真实尺寸和取整差，装配窗口仍按精确比例映射。已完成但比例不匹配的资源须明确记录缺口和用户选定的处理，不擅自改框、拉伸、加空白或重绘已审核心。

## 完整构图与卡框映射

新制资源应同时满足：原构图完整、图片区铺满、原图等比、原卡框不变。通过制图阶段遵守上节窗口比例达成，不在UE接入末端轮流尝试裁切、留白或改框。用户明确要求恢复某个旧显示版本时按其选择恢复，但不得把恢复的历史裁切标为新制作指标通过。

区分四套尺寸：完整PNG画布、批准的原图窗口、运行时缩放后的CardFace物理宽高、卡框透明开口。标题/说明、Widget缩放/偏移和父组件变换均影响最终开口；名义贴图比例、网格UV范围或历史脚本值不能代替实际配置。WM01旧框参考`Scripts/Cards/comic_card_edge_coverage.py`；Ground框读取`GuLiGroundCardFace.cpp`及`GuLiRogueCardPresentation.cpp::SetLiveCardText`，二者不要混用。

正式原生框与评审必须使用上节固定配置，标题和说明位置也保持。发生明确授权的原生修改时，代码与已加载模块须一致；未加载的新代码不能当作当前效果依据。

对无旋转的共同画布映射，设画布为`Cw×Ch`，原窗口为`[l,t,r,b]`，实际开口物理大小为`Aw×Ah`，CardFace物理大小为`Fw×Fh`，目标窗口中心在CardUV中为`q`：

```text
先核对 Aw/Ah 与 (r-l)/(b-t) 相等（允许浮点误差）
f = Aw/(r-l) = Ah/(b-t)
Scale = (Cw*f/Fw, Ch*f/Fh)
c = ((l+r)/(2*Cw), (t+b)/(2*Ch))
Offset = q - 0.5 - (c-0.5)*Scale
```

`f`作用于原窗口，Scale仍按完整画布换算；六层共用基础映射。固定裁切矩形取映射后的原窗口并留必要滤波内衬，不能缩小图片后仍任由整个外扩区显示到卡框边缘。`Artwork Aperture`等参数必须使用父材质要求的坐标空间。`author_ground_four_card_artwork.py::ground_frame_layout`可参考当前Ground计算，但尺寸变化时重新读配置；不要重跑该一次性登记器覆盖已有目录。

保存前分别核对以下三件事：

- **静态构图包含关系：** 零倾斜、零位移时，原窗口四角经过实际Scale/Offset映射后均在最终卡框开口内，X/Y像素对应相同物理尺度；记录原窗口可见宽高比例。只检查贴图覆盖开口会漏掉“显示不全”。
- **图片区铺满关系：** 同时确认映射后的原窗口与开口边界相合，记录开口填充比例；仅检查原图四角在框内仍会放过两侧留白。卡框正常边线、标题区和说明区不算图片留白。
- **运动采样范围：** 按实际深度、MotionOwner、强度上限、XY最大位移及滤波内衬，将固定窗口反算到每层源坐标；确认最小余量。运动时仍按已定稿的窗口裁剪，不承诺所有移出窗口的对象始终可见。矩形未越界不等于已生成内容完整，后者交用户验收。

2026-11-17四张Ground牌的2:3原图用cover仅显示约61.22%的高度；改contain又产生两侧空白。随后准备改窄卡框，用户明确要求恢复原版并将原卡框比例记为硬指标。现有四张按要求恢复最初满框显示，保留原裁切；窄框方案未部署。正确的后续流程是**按固定开口比例制作原画**，不是默认允许留白或改框。没有越界、构图完整、图片区铺满、卡框不变须分别记录，技术数据不代表用户视觉通过。

## 贴图导入与保存回读

先按Color/Mask设置压缩方式，再设置sRGB；UE更改压缩可能重置颜色空间，保存后逐张回读。Color使用sRGB，Mask使用线性采样；同时核对实际尺寸、Alpha保留、地址模式、材质采样器类型与蒙版通道。仅修构图时更新Excel的映射参数并部署MI，不重导未变源图或改动配色。

前后遮挡从清单核对互补蒙版、主图采样和透明度；附着物通过MotionOwner共用位移。不要因为某张牌将第3层拆成前后就把该层编号当成其他牌的固定语义。读取表时区分布局枚举字面值`None`与空字段，避免预检将合法模板误判为缺失。

## 日常操作

在项目根目录，使用装有openpyxl的项目Python（本机Python313）；ComfyUI嵌入式Python未安装该依赖，不作为导出器运行环境。

```powershell
python Tools/DataPipeline/export_data_from_excel.py --validate-only
python Tools/DataPipeline/export_data_from_excel.py
python Scripts/Cards/card_artwork_pipeline.py preflight --ids 6
python Scripts/Cards/card_artwork_pipeline.py deploy --ids 6
python Scripts/Cards/card_artwork_pipeline.py verify --ids 6
python Scripts/Cards/card_artwork_pipeline.py review --ids 6
```

以上6是命令示例，替换为本批实际ArtworkId；四张Ground牌评审可用`review --ids 10 11 12 13`。

- `preflight`只读检查目录、父材质参数类型、资源、源文件及已加载行结构；没有加载新行结构时报告native_schema_ready=false。
- `deploy`先完整预检，再更新所选牌面的贴图/MI与四张目录表。默认全部；`--ids 6`只应用这一牌面，目录表仍完整导入。新增或改变行结构须按项目规则先取得原生编译许可并加载。
- `apply`仅更新指定牌面资源；`tables`仅更新四张目录表。`verify`比较实际父材质、贴图及所有参数。
- `review --ids N [N ...]`在共享地图准备单张或多张可切换牌面，保存后重新加载核对；不自动开始PIE。地图 `/Game/GuLiStrike/CardSystem/RevealDemo/Maps/LVL_CardArtworkCatalogReview`，Actor `CardArtwork_CatalogDirector`。多张支持顶部按钮切换；Ground模板单张也使用对应正式框比例。进入PIE后鼠标压动、单击翻面、再次单击退场、点击重播。
- 正常部署会跟踪源文件哈希：已有纹理首次纳管不重导入；之后源文件变化时更新本卡目录内的纹理。`--reimport`明确重导入本卡目录内的纹理；公共/商城纹理继续只读复用。
- 源图缺失且现有UE纹理可用时继续复用并报告missing_sources；新导入或要求重导入时缺源则失败。来源失效不能标为已补齐源图。
- 部署校验Excel与导出manifest的源哈希；忘记导出时拒绝消费旧清单。保留用户未保存资产与活动PIE；不自动结束、保存无关资产或重启编辑器。

通用 `Scripts/import_data_to_engine.py` 同样识别这四张表并委托本入口，筛选时须四张一起导入。目录表原位填充并保存回读，避免替换已有对象。JSON/CSV、UE表、MI上的目录元数据均非日常维护源。

## 兼容与制作来源

- 保留仍被使用的父材质和正式资源路径，初始数据从UE实际值捕获；不采用历史制作脚本默认值覆盖现状。新卡按类型/单位/效果归属Textures、Materials，公共系统继续集中共享。
- 使用共享入口 `python Scripts/Cards/card_artwork_pipeline.py deploy --ids 6` 重导折射。重防号对应 ID 4、5。包内 ImportToUE.py 委托同一管线。
- 历史卡面写入工具已退役，日常维护通过 Excel 美术目录和共享制作管线。
- 首次捕获 `capture_card_artwork_baseline.py` 和 `author_card_artwork_catalog.py` 只用于建立目录，已存在时拒绝覆盖。历史baseline只作迁移证据，之后不能用于重置源表。
- HTML继续使用用户确认的同一页面与六层产物。本目录登记其来源；不自动重新生图、重建旧图集或移动原图。

## 核验与交付

报告统一在 `Artifacts/CardArtworkPipeline`：baseline、源路径解析、导出兼容性、构建/BuildId、部署与回读。重复部署应没有额外资源与参数改写。失败报告记录已经保存的更改，修复输入后重跑，不声称多资产事务自动回滚。

每次修正保留独立的配置差异、源哈希、原窗口映射与保存回读报告；目录按本次任务归档，避免覆盖上一版证据。初始化脚本仅用于首建，修复已有牌面从当前Excel定向改值；可同步修正初始化算法以防复发，但不重跑初始化覆盖记录。

`python Scripts/Cards/card_artwork_pipeline.py test`只修改`Cards/_Tests/CardArtworkPipeline`里的专用MI，用复制的层配置验证真实参数更新和重复应用；不会改正式Excel或正式牌面。离线回归在`Tools/DataPipeline/tests/test_card_artwork_catalog.py`。这些是按当前授权选用的工具，不因文档列出而自动运行或扩充测试。

视觉效果仍由用户在PIE验收；结构化回读、编译或shader诊断不代替视觉通过。
