# 爆破套餐三枚导弹轴线修订

2026-11-18，用户对V2爆破套餐指出“这三个导弹都歪了”，并以三条红箭头标示方向。使用内置image_gen，将未标注的完整透明PNG和用户反馈图作为输入编辑，提示词要求每枚弹头尖端、圆环中心、筒身及尾部中心共线，保留原有编队与弯曲尾迹。结果外观待用户核验。

源图版本为`RogueCardIcons_20261118_v2_1`，正式资源仍为`/Game/GuLiStrike/GroundMech/UI/Icons/T_GroundIcon_ExplosivePackage`，IconId11及Card11.01引用不变。原始输出为1254×1254 RGBA，未经脚本重绘、抠图、重配色或缩放；UE构建尺寸256×256。其余四张当前图标保持，包括前次已修改的强化爆炸弹。

- [源图更新记录](source-update.json)：仅Icon11哈希改变；Excel和导出JSON的字节保持，其他目录条目及PNG哈希保持。
- [定向导入及保存回读](install-report.json)：只导入Icon11，实际源路径、哈希、UI压缩、256尺寸和Alpha保留设置核对；两个UE数据表、其他四个纹理的源元数据均不变。
- [生成回执](../../../ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1/generation-package-axis-result.json)与[完整提示词](../../../ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1/Prompts/11_ExplosivePackage.v2-axis.txt)保存工具输出来源及编辑要求。
- [同一预览](http://127.0.0.1:8198/?revision=v2-1-package-axis)默认V2.1；爆破套餐增加“查看轴线修订前”按钮，保留强化爆炸弹对比和V1总切换。
- [交付检查](delivery-verification.json)核对HTTP文件、PNG哈希、下载包和数据文件；未读结果图、截图、启动PIE或原生构建。结构化检查不能作为轴线造型通过的证明。

旧Icon11原图在`Inputs/11_ExplosivePackage_v2_before_axis.png`，旧页面/清单/包在`Revisions/v2_before_package_axis/`。当前下载包随版本更新为`RogueCardIcons_20261118_v2_1.zip`，包含新图、旧图、提示词、追溯清单和对比页面。
