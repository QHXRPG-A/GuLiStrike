# 卡牌美术目录与统一导入

日常源为 `data/Excel/GuLiStrikeCardArtwork.xlsx`，使用项目excelize-cli编辑。新增资源先登记，再导出与部署；不再在单卡脚本里维护贴图路径和参数。内置生图初稿、用户Qwen参考图、六层补绘和HTML阶段仍按美术主流程执行。

## 数据与运行时合同

| 工作表 | 维护内容 |
|---|---|
| Artworks | 稳定整数id、行名、显示标题、TemplateId、最终MI、AssetRoot、源目录/参考图/HTML入口 |
| Templates | 父材质、存储布局和组合规则：ModelComicAtlasV9、RegisteredSix、RegisteredSixForegroundMask |
| Layers | 每套恰好六个逻辑层，编号、语义、TextureParameter、深度、透明度、Scale/Offset的完整四通道 |
| Parameters | 其余标量/向量及所有贴图参数绑定；Texture对应完整UE对象路径，SourceFile可为项目相对路径；TextureRole为Color或Mask |

层编号不是叠放顺序；组合算法、图集切格、透明处理与前景遮挡由模板实现，表中选择模板。普通图层参数只在Layers维护，禁止再放进Parameters形成第二来源。Layers.TextureParameter引用本牌Parameters中Kind=Texture的Key；图集六层共用BaseColor Map，独立六层使用Layer N Map。

UE图层使用居中映射`p=(CardUV-0.5-Offset)/Scale+0.5`，不能直接照抄HTML的drawImage偏移或外扩画布偏移。接入时依据完整画布、原图窗口、实际卡框开口和CardFace网格宽高换算六层共同Scale/Offset；按物理宽高保持原图比例，并给当前刚性视差的最大位移留出原图安全范围。卡框开口合同见`Scripts/Cards/comic_card_edge_coverage.py`。外扩内容用于填满窗口和支撑运动，不能把原图左上角的扩边偏移再当成UE居中偏移。数值映射核对不代替用户画面验收。

`GuLiStrikeRogueCards.xlsx / Cards` 以必填整数 `ArtworkId` 指定牌面，源表不再手填FrontMaterial。导出器只把此列解析为既有运行时FrontMaterial，不增加或改名原卡表字段，也不改写Layers/Parameters中的ArtworkId。不同玩法等级可引用同一ArtworkId。

初始ArtworkId：1射速、2机动、3导弹伤害、4导弹仓、5雨点攻势、6折射子弹。6目前仅是美术目录项，不伪造Ground玩法行。地面玩家与WM01身份保持分离。

运行时沿用现有卡牌Actor与最终MI，GPU实时采样、叠合和视差。目录数据表是可浏览的生成物，位于 `/Game/GuLiStrike/Data/DT_GuLiStrikeCardArtwork_*`；不通过运行时读取PNG重新组装材质。

## 日常操作

在项目根目录，使用装有openpyxl的项目Python（本机Python313）；ComfyUI嵌入式Python未安装该依赖，不作为导出器运行环境。

```powershell
python Tools/DataPipeline/export_data_from_excel.py --validate-only
python Tools/DataPipeline/export_data_from_excel.py
python Scripts/Cards/card_artwork_pipeline.py preflight
python Scripts/Cards/card_artwork_pipeline.py deploy
python Scripts/Cards/card_artwork_pipeline.py verify
python Scripts/Cards/card_artwork_pipeline.py review --ids 6
```

- `preflight`只读检查目录、父材质参数类型、资源、源文件及已加载行结构；没有加载新行结构时报告native_schema_ready=false。
- `deploy`先完整预检，再更新所选牌面的贴图/MI与四张目录表。默认全部；`--ids 6`只应用这一牌面，目录表仍完整导入。新增或改变行结构须按项目规则先取得原生编译许可并加载。
- `apply`仅更新指定牌面资源；`tables`仅更新四张目录表。`verify`比较实际父材质、贴图及所有参数。
- `review --ids N`在共享地图准备一张牌面，保存后重新加载核对；不自动开始PIE。地图 `/Game/GuLiStrike/CardSystem/RevealDemo/Maps/LVL_CardArtworkCatalogReview`，Actor `CardArtwork_CatalogDirector`。进入PIE后鼠标压动、单击翻面、再次单击退场、点击重播。
- 正常部署会跟踪源文件哈希：已有纹理首次纳管不重导入；之后源文件变化时更新本卡目录内的纹理。`--reimport`明确重导入本卡目录内的纹理；公共/商城纹理继续只读复用。
- 源图缺失且现有UE纹理可用时继续复用并报告missing_sources；新导入或要求重导入时缺源则失败。来源失效不能标为已补齐源图。
- 部署校验Excel与导出manifest的源哈希；忘记导出时拒绝消费旧清单。保留用户未保存资产与活动PIE；不自动结束、保存无关资产或重启编辑器。

通用 `Scripts/import_data_to_engine.py` 同样识别这四张表并委托本入口，筛选时须四张一起导入。目录表原位填充并保存回读，避免替换已有对象。JSON/CSV、UE表、MI上的目录元数据均非日常维护源。

## 兼容与制作来源

- 保留现有三种父材质和所有正式资源路径，初始数据从UE实际值捕获；不采用历史制作脚本默认值覆盖现状。新卡按类型/单位/效果归属Textures、Materials，公共系统继续集中共享。
- `import_ground_ricochet_card_template.py`、包内ImportToUE.py和`import_wm01_registered_layers.py`仅为委托入口；折射评审入口也转到共享评审地图。
- `apply_modelcomic_v9.py`、`author_warmachine_cards.py`已阻止作为旧写入入口运行，历史内容仅供追溯；旧一次性数据初始化/迁移器也不再作为日常入口。
- 首次捕获 `capture_card_artwork_baseline.py` 和 `author_card_artwork_catalog.py` 只用于建立目录，已存在时拒绝覆盖。历史baseline只作迁移证据，之后不能用于重置源表。
- HTML继续使用用户确认的同一页面与六层产物。本目录登记其来源；不自动重新生图、重建旧图集或移动原图。

## 核验与交付

报告统一在 `Artifacts/CardArtworkPipeline`：baseline、源路径解析、导出兼容性、构建/BuildId、部署与回读。重复部署应没有额外资源与参数改写。失败报告记录已经保存的更改，修复输入后重跑，不声称多资产事务自动回滚。

`python Scripts/Cards/card_artwork_pipeline.py test`只修改`Cards/_Tests/CardArtworkPipeline`里的专用MI，用复制的层配置验证真实参数更新和重复应用；不会改正式Excel或正式牌面。离线回归在`Tools/DataPipeline/tests/test_card_artwork_catalog.py`。

视觉效果仍由用户在PIE验收；结构化回读、编译或shader诊断不代替视觉通过。
