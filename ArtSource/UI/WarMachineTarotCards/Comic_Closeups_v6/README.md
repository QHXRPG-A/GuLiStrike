# 重防号能力卡：干净美漫风 v6

本版使用粗细明确的深色墨线、完整装甲色块、大块阴影、克制火光与低干扰背景。保留原机械身份、局部镜头和橙红/暖白/藏蓝配色。用户已授权制作并替换UE资源，实际交付见[制作与操作](Production/README.md)、[最新UE三牌画面](Production/Previews/three-cards-comic.png)。

下面三张完整图是最初的imagegen风格预览，后续按用户标注修订了炮口/火光、导弹位置与完整悬浮盘；它们不是最终UE合成截图。

## 三张完整图

| 卡牌 | 图像 | 重点 |
|---|---|---|
| 增加射速 | [FireRate.png](FireRate.png) | 双联炮的连续大色块；近远残骸简化成可读机械剪影。 |
| 增加导弹伤害 | [MissileDamage.png](MissileDamage.png) | 仰视双舱、2×2发射孔、出膛导弹和远处命中。 |
| 极速机动 | [HighSpeed.png](HighSpeed.png) | 完整连接支架、连贯盘面、简洁向后尾流。 |

## 本轮收敛过程

1. 根据用户标注识别炮身倒角细线、支架碎面、悬浮盘接缝与残骸碎片等噪声来源。
2. 射速首轮仍保留较多浅色倒角线，保存在 [Drafts/FireRate-r1.png](Drafts/FireRate-r1.png)，不作为最终风格参考。
3. 射速第二轮进一步减少亮线、提高外轮廓和整块阴影的权重，作为另外两张共同风格参考。
4. 导弹与机动仍使用各自v5完整卡面固定构图；机动另带用户红框定位，生成时明确移除红框。

## 提示词与来源

- [用户原文与解释](Prompts/UserBrief.txt)
- [当前共同风格约束](Prompts/Common.txt)
- 最终调用的完整提示词：[射速](Prompts/FireRate.txt) · [导弹](Prompts/MissileDamage.txt) · [机动](Prompts/HighSpeed.txt)
- 首轮提示词：[FireRate-r1.txt](Prompts/FireRate-r1.txt) 与 [Common-r1.txt](Prompts/Common-r1.txt)
- `References`保留用户两张标注、三张v5实际卡面与三张原始清晰源图。原始路径、引用顺序、输出来源与SHA-256见 [manifest.json](manifest.json)。

要求保留：`干净视觉图`、`no grain, no dirty texture, no random speckles, no messy background, no harsh glow`。新增限制包括禁用网点、排线、印刷颗粒、刮痕与满边倒角亮线，避免美漫做旧效果重新引入噪声。

## 接入与验收边界

用户随后明确要求“内部视差深度×4，整牌偏转±16°，开始替换原先资源”，并追加同口径双联炮及[UE画面标注](References/UE-layout-user-markup.png)。已完成六层生产素材、材质接入与评审地图保存，面积和厚度仍各2倍，可编辑UMG/FText沿用。源图、8份生产提示词、修订历史和UE检查记录集中于 `Production`。

预览中的文字仅供审排版；正式资源的标题/说明由数据表FText与独立UMG绘制，卡框无字。附着火光、出膛导弹和盘底发光边缘均与相应机械保持同层。

这次仅调整三张卡的画风，不改变全项目美术规范。助手视觉检查与用户美术终验分别记录，用户最终验收仍待反馈；v5技术检查不作为v6的验证证据。
