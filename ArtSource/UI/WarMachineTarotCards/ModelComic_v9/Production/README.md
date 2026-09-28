# 战争机器经典美漫三牌 UE 替换

用户授权“开始替换UE5当中的资源”，并明确导弹伤害“同时换成此前v7美漫原图”。本版将三张插画分别制成六层图集，替换专用评审地图的牌面材质引用。旧源图和旧UE资产保留。

**交付状态：**三张新牌面与评审地图已保存，编辑器恢复后从磁盘重新加载核对成功；材质编译无错误，14项相关资产的直接依赖无缺失。助手未读图，实际画面与交互效果待用户核验。

| 位置 | 卡名 | 插画来源 | 新牌面材质 |
|---|---|---|---|
| 左 / 0 | 增加射速 | [v9补腿修订](../FireRate.png) | `MI_FireRate_ModelComic_v9` |
| 中 / 1 | 增加导弹伤害 | [v7美漫原图](../../ModelComic_v7/MissileDamage.png) | `MI_MissileDamage_ModelComic_v9` |
| 右 / 2 | 极速机动 | [v9速度光线修订](../HighSpeed.png) | `MI_HighSpeed_ModelComic_v9` |

材质名里的v9是本次UE集成版本，导弹伤害的插画来源仍是v7。

## 打开与操作

打开 `/Game/GuLiStrike/Cards/WarMachineTarot/Maps/LVL_WarMachineTarotReview`，实例为 `WarMachine_CardDirector`。使用单机游玩查看：

1. 三张正面牌从远处依次入场，每张0.5秒，总共1.5秒。
2. 鼠标在牌面上移动，所在一侧向内压；整牌每轴最多16°，插画内部视差深度4。移开后回正。
3. 点击一张锁定并翻背，翻面0.45秒；另外两张不能改选。
4. 翻面完成后再次点击所选牌，0.25秒局部闪光，然后三张在0.5秒内退场。
5. 点击“重新播放”重新入场。

本轮没有重新运行游戏，上述为保留的流程与玩家核验步骤。请重点核验补腿、速度光线、导弹构图、四边四角的层边界及文字排版。

原版对照地图：`/Game/GuLiStrike/Cards/RevealDemo/Maps/LVL_CardRevealDemo`。

## UE资源与参数

资源根：`/Game/GuLiStrike/Cards/WarMachineTarot`。

- `Textures/T_{FireRate,MissileDamage,HighSpeed}_Layers_ModelComic_v9`：三张新图集，实际1254×1254 RGBA，3列×2行，每格418×627。
- `Materials/M_CelCardParallax_ModelComic_v9`：项目父材质副本，保留六层视差、动态纹样和固定牌面窗口。
- `Materials/MI_*_ModelComic_v9`：三份新牌面实例，`BaseColor Map`与`Ability Layer Map`均指向该牌的新图集。
- `CardTextMaterials`继续使用v6无字框实例；卡背、文本控件、输入与流程蓝图复用。
- Director保持 `MaximumTilt=16`、`CardAreaMultiplier=2`、`CardThicknessMultiplier=2`。面积2倍对应线性缩放√2，不是宽高各2倍。

六层深度为 `0.10 / 0.04 / 0 / -0.40 / -0.75 / -1.10`，全局深度4。完整机械层位于参考平面，前后景相对它移动。主机械Alpha使用 `smoothstep(0.12,0.72,A)`，完整底图直接作为不透明RGB底色合成。

固定插画窗口UV为 x=0.045–0.955、y=0.043–0.772。远景和背景使用更大缩放覆盖窗口；机动能力层及环境中景额外扩展。具体数值见[集成记录](Inspection/integration.json)。这些是材质覆盖设置，不能代替极限偏转时的实际画面审核。

## 六层模板与实际输入

| 层 | 射速 | 导弹伤害 | 极速机动 |
|---|---|---|---|
| 1 前景 | 前景残骸 | 最近自由飞行导弹 | 前景速度线 |
| 2 能力 | 已飞离炮口的短弹迹 | 中距离自由飞行导弹 | 分离的推进尾流 |
| 3 完整机械 | 全部可见机械、腿盘、附着枪口火光 | 双舱、连接与出膛导弹 | 全部可见机械、腿盘、附着盘底光边 |
| 4 中景 | 击溃部队与爆炸 | 远处导弹及爆炸 | 地面结构与速度环境 |
| 5 远景 | 远处烟柱/部队 | 远处环境 | 工业远景 |
| 6 背景 | 补全天空与地面 | 补全天空 | 补全天空与地面 |

此表记录生成与材质分配意图，机械数量、分层位置和合成效果由用户核验。内置imagegen的原始图集输出原样保存于 `Layers`，未用图像脚本重画或后期修改。

完整实际调用及输入路径见 [layer-generation-requests.json](layer-generation-requests.json)。文字prompt：[射速](Prompts/FireRate_Layers.txt)、[导弹](Prompts/MissileDamage_Layers.txt)、[机动](Prompts/HighSpeed_Layers.txt)。历史v7/v8/v9插画prompt未改；本轮分层使用另外记录的生产prompt。

生成prompt要求3072×3072和四边各40%外扩，但实际文件为1254×1254；没有把尺寸或外扩要求记为已实现。仅核对尺寸、透明通道、文件哈希和来源，见[layer-file-check.json](layer-file-check.json)。未检查图像内容或认定机械/风格通过。

## 文案编辑与回退

打开 `/Game/GuLiStrike/Cards/WarMachineTarot/Data/DT_CardText`，修改各行 `Title`、`Description`，保存后重新播放。两字段仍为FText，Namespace为`GuLiStrike.Cards`，稳定行名与本地化Key保持。批量填表与后续翻译入口见[总操作说明](../../README.md#修改卡名与说明)。

如需回退，在地图的 `WarMachine_CardDirector → CardFrontMaterials` 中，按左中右恢复 `MI_FireRate_Comic_v6`、`MI_MissileDamage_Comic_v6`、`MI_HighSpeed_Comic_v6`，然后保存地图。[替换前绑定](Inspection/map-artwork-before.json)保留完整值。

## 验证边界与预览事故

[integration.json](Inspection/integration.json)记录三份新贴图、父材质与实例的保存成功、材质编译无错误和地图绑定保存。父材质沿用原网络，诊断报告59个纹理采样，不作为性能验证结论。

随后可选静态预览在创建21:9渲染目标时触发UE弹窗递归，编辑器退出；事故发生在资源和地图保存之后。[事故记录](Inspection/preview-incident.json)保留原因、影响和恢复进度，原预览脚本已停用。

`Previews`里的16:9、16:10文件仅为未完成批次的静态输出，没有运行时UMG文字，不证明视觉检查通过；21:9和倾斜预览未完成。助手遵从“不读图、由用户核验”，没有打开这些图片。[恢复后读回](Inspection/final-readback.json)已成功：地图从磁盘重载，仍为原有4个Actor，无预览临时对象；三张材质、参数、文字表与原塔罗默认配置符合预期，14项相关包的直接依赖无缺失。

UE最终视觉、鼠标手感与完整两次点击流程待用户核验；没有本轮PIE、原生编译、属性奖励或战斗资源替换。
