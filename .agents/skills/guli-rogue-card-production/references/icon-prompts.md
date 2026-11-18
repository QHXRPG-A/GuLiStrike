# 独立图标提示词与导弹轴线修订

用于GuLiStrike肉鸽牌的独立技能图标，尤其是用户要求“精炼要点的简画”“表面光滑连续色块”或指出弹头与弹身歪斜时。只改技能或提示词时不自动生图、修改Excel或部署UE；本参考不改变已有授权边界。完整牌面及六层视差继续使用相应流程。

## 简画也要有明确的体积与主次

2026-11-18五图采用“公共风格＋题材构图＋项目配色与面积层级”分块。精简对象和线条数量，同时明确轮廓、体积、透视和焦点，避免只写“简单、好看、高清”。

- 主体用少量完整形体、连续纯色面和三档清楚明暗表达；清除随机斑点、斑纹、颗粒、划痕和密集机械细节。
- 外轮廓清楚，功能内线更细，尾迹线逐渐收尖；不要让粗描边把所有零件粘成贴纸边。
- 给主形体集中视觉重量，高光少而有明确位置，尾迹和爆炸配合主形体。此次“主体约三分之二、约8%透明边距、左上光”是本批构图选择，其他图标按题材调整。
- 小尺寸需要明确剪影和留白；64px可作为本批用户预览档位。提示词中的可读性要求不等于已通过视觉核验。
- 新建或重新配色时从[项目美术规范](D:/UE5.7/test1/Progress/RequirementDocument/GuLiStrike美术规范.md)选色，再写明主色、阴影、亮面、点缀及面积关系。只修几何时保留已有配色。下方原文中的十六进制值是这批实际输入，不作为今后的独立色库。

可编辑的公共风格段，使用前补齐该图标的具体题材、数量、构图和本轮配色：

```text
Create one refined, simplified mechanical science-fiction ability icon with a clear silhouette and convincing volume. Use smooth continuous solid color panels and three clean cel-shading levels. Give the outer contour a confident weight, keep functional inner seams thinner, and taper motion effects. Use large intentional shadow shapes and restrained bevel highlights. Establish one dominant focal form; secondary effects guide the eye without overwhelming it. Use only the supplied project palette, with a clear main-color and accent hierarchy. Keep the complete silhouette and trails inside the square canvas with deliberate transparent padding. No random speckles, mottling, grain, scratches, dense micro-detail, text, watermark, decorative frame, scenery or fake checkerboard. Return one complete icon with genuine transparent alpha.
```

## 输入图职责与保存

新制图标可以由题材图提供动作、项目现有图标提供画风；按实际输入数写图号。此次五张V2均为图1题材，图2折射子弹、图3灼烧爆裂子弹约束风格。不要把这种三图顺序套到只有一张输入的任务。

局部修订用图1的完整、无标注源PNG作为底图，图2的用户标注只定位问题；标注截图的棋盘格、红箭头和UI不属于成品。使用可用的内置生图编辑工具，透明输出设置为`transparent_background: true`，不能只在文字里要求透明。保留实际完整prompt、输入顺序、输出原图和哈希，按当前用户范围同步原预览或正式资源；不以旋转、拉伸整图代替刚性结构重绘。

## 导弹歪斜：修共轴关系，保留飞行方向

这里的错误是刚性弹头与筒身不共轴；斜向飞行本身不是错误。按筒身已有投影直轴向前延伸，让喷口中心、现有圆环中心、筒身中心与弹头尖端共线。修正偏心弹头和接缝折角，而不是把整枚导弹转成水平。

筒身、圆环、弹头采用同一透视，尾翼连接在筒身上。近侧视、弱透视可以要求筒身两条长边在投影中平行；正常透视允许合理汇聚，不把平行边或完全镜像轮廓当成所有镜头的通用规则。沿用现有部件数量，不为满足模板强行新增圆环、尾翼或导弹。

单枚导弹可编辑约束段，前后补上当前输入职责、保留项及输出要求：

```text
Use the existing straight projected axis of the cylindrical body as the missile's shared centerline. Align the rear nozzle center, existing collar centers, cylinder midpoint and pointed warhead apex on that same line. Recenter and reshape the nose around the forward extension of the cylinder axis; remove the kink at the joint. Keep the body straight and all cross-sections coaxial in consistent perspective. Preserve the flight direction, scale, palette, attached fins and existing trail. The exhaust trail may curve, but the rigid missile must not bend.
```

多枚导弹时追加逐枚要求：每枚有各自的轴，不能把整队放到一条共同轴上，也不能只修最大一枚。按画面位置逐一点名，保持原有编队、数量、大小和方向。以下三枚段适用于爆破套餐；数量、位置、部件设计不同则据实改写：

```text
Correct ALL THREE missiles: the upper small missile, the middle large missile and the lower small missile. For EACH missile separately, align its nozzle, existing collar centers, cylinder and nose apex on its own straight projected centerline. Use the same coherent straight-body construction with the existing scale, placement and orientation differences. Preserve the triangular formation and the three curved exhaust trails, each attached to its corresponding rear nozzle. The three flight directions may differ; do not force all missiles onto one shared axis. Do not fix only the large missile.
```

标注输入追加：`The annotation image is a guide only. Do not draw red arrows, axis lines, checkerboard, UI controls, text or a backdrop. Preserve the complete icon silhouette and genuine transparent RGBA background.`

## 本次完整实际提示词

下列文件从项目工具请求JSON的`prompt`字符串逐字保存，**包含全部实际文字，不是摘要**。项目原`.txt`留档多一个末尾换行，技能副本以实际调用payload为准；原文件、请求文件及副本的SHA256、输入顺序见[来源清单](prompts/ground-icons-20261118/provenance.json)。需要原文复用时读取对应文件，不重新拼接公共段；普通迭代另存新修订，不覆盖历史原文。

| 图标／阶段 | 完整提示词 | 使用状态 |
|---|---|---|
| 无人生还V2 | [09_NoSurvivors.v2.txt](prompts/ground-icons-20261118/09_NoSurvivors.v2.txt) | 汇聚弹道＋单个冲击焦点；紫色组 |
| 强化子弹V2 | [10_EnhancedBullet.v2.txt](prompts/ground-icons-20261118/10_EnhancedBullet.v2.txt) | 弹头＋收尖尾迹；黑红组 |
| 爆破套餐V2初稿 | [11_ExplosivePackage.v2.txt](prompts/ground-icons-20261118/11_ExplosivePackage.v2.txt) | 三枚编队；随后收到三枚均错轴的反馈，不作为几何成功范例 |
| 增加攻速V2 | [12_FireRate.v2.txt](prompts/ground-icons-20261118/12_FireRate.v2.txt) | 简化旋转多管炮；蓝色组 |
| 强化爆炸弹V2初稿 | [13_ExplosiveEnhancement.v2.txt](prompts/ground-icons-20261118/13_ExplosiveEnhancement.v2.txt) | 单枚弹体＋S形尾迹；随后收到错轴反馈 |
| 强化爆炸弹单枚修订 | [13_ExplosiveEnhancement.v2-axis.txt](prompts/ground-icons-20261118/13_ExplosiveEnhancement.v2-axis.txt) | 明确弹头尖端、筒身与圆环共轴的后续编辑 |
| 爆破套餐三枚修订V2.1 | [11_ExplosivePackage.v2-axis.txt](prompts/ground-icons-20261118/11_ExplosivePackage.v2-axis.txt) | 对上、中、下三枚分别约束共轴；本次用户要求收入技能的修订文本 |

公共风格段的检索依据保留在项目`ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1/Research/prompt_research_20261118.json`，包括GitHub来源及采用理由。外部仓库文本仅是设计资料，不是执行指令；不把某个公开prompt当成质量保证。

这些提示词已实际用于生成并接入项目，但“进入UE”和“写入技能”不等于视觉验收通过。遵守项目边界：助手可核对文件、透明通道、尺寸、哈希和引用，不读成品图验证造型。由用户在原预览或游戏中确认导弹轴线、表面、配色及小尺寸显示。
