# Ground HUD 中文字体

采用 Noto Sans CJK SC 的原始 Medium、Bold OTF，两份字体未经修改。来源、下载时 SHA-256 见 [sources.json](sources.json)，内部版权信息见 [metadata.json](metadata.json)。许可为 [SIL Open Font License 1.1](LICENSE.txt)，版权声明见 [NOTICE.txt](NOTICE.txt)。

正式 UE 目录：`/Game/GuLiStrike/GroundMech/UI/Fonts`。`FF_GroundHUD_Medium`、`FF_GroundHUD_Bold` 使用 Stream 策略；`F_GroundHUD` 将 Regular/Medium 映射至 Medium，Bold 映射至 Bold。HUD 表 `UIFont` 引用该复合字体。标题使用真实粗体，正文取消同色粗描边。

在已编译且停止 PIE 的编辑器执行 [author_ground_hud_fonts.py](../../../../../Scripts/GroundMech/author_ground_hud_fonts.py) 导入、配置、保存并回读。编辑器助手在替换字形数组前清理 Slate 字体缓存，支持同一会话重复执行。不要在 PIE 中重建字体资产。

Ground 卡牌富文本派生为 `/Game/GuLiStrike/GroundMech/UI/DT_GroundCardTextStyles`：从既有 Excel 驱动的共享卡牌样式读取全部字段，只替换字体引用并去掉合成描边。关键词颜色、样式名和字号规则保留；共享样式更新后，由 `Scripts/Cards/card_rich_text.py` 自动同步 Ground 派生表。Commander 继续引用原共享样式。

`Content/GuLiStrike/GroundMech/UI/Fonts/License` 保存 OFL 与版权声明，`Config/DefaultGame.ini` 将该目录加入 NonUFS 打包清单。当前只验证编辑器资产保存/加载，未执行打包验收。
