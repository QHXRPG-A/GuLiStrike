# 折射子弹 · 六层模型补绘

此修订沿用同一预览网址和六张主图文件名。六张切片均通过内置imagegen逐层补绘，处理破碎边界、缺失表面、被遮挡结构和外沿。没有使用ComfyUI，没有修改卡牌数据、技能、效果或玩法逻辑。

## 六层与共同坐标

1. 近景：完整墙岩、植被、扬土与烟尘。
2. 速度线：连续的白色漫画速度线。
3. 机甲：补全机械结构及附着尾焰、枪口火光。
4. 场景：连续草原、石阶与中近景残骸。
5. 折射弹道和三个敌方机甲：补全敌人和受击效果；各单位按原位置装配，金色路径使用原图连续轨迹像素。
6. 远景：不透明的完整远景底图。

六张交付PNG统一为1536×1536；原始1024×1024位于[256,256,1280,1280]，各层使用相同基础映射。生成原文件的真实尺寸单独记录；只对生成补绘做归一化和坐标装配，原图核心像素不做单层UV补偿或重采样。
原始Master.png保持不变。补绘后的透明边界、遮挡区域和表面细节允许改变；主要可见色块按原图回贴保护，实际保护数量和生产装配参数见completion-r3-build.json。

## 前景遮挡与露边

近景是同一张完整补绘图。Masks/ForegroundFront_R3.png是一张技术遮挡蒙版，将其分到机甲前后显示，补绘的隐藏表面可以衔接。仍为六个内容分组；近景开关和透明度共同控制两个显示部分。
网页前景在窗口外侧2%范围固定源UV，2%至12%平滑过渡到内部视差；支持WebGL时使用边缘固定采样。后备Canvas2D固定前景，仅其他内容产生相对位移。补绘图仍保留统一外沿，不使用旧的边缘像素拉长方案。

## 查看与验收

打开preview.html，先查看原位叠合和原图，再切换视差。可独立开关六组内容并下载六张主图。
本轮做了尺寸、alpha、来源哈希、二值蒙版、共同坐标及脚本静态检查。UE导入及PIE结构化检查见下文。没有进行结果读图、截图对比或UE视口目检；构图、部件、轨迹连接、透明边缘和倾斜自然程度仍待用户视觉验收。生产中的特征装配不是画面验收。

## 可追溯记录与UE

完整提示词与按顺序列出的输入图见completion-r3-requests.json；原始生成来源见completion-r3-results.json；原始输出保留在Supplements/。前景和场景使用固定画布再次定点编辑，首轮生成记录另行保留，不再由页面消费。
ImportToUE.py已于2026-10-27在当前UE编辑器执行，六张1536×1536图层、前景技术蒙版及独立材质已保存并回读；结果见ue-import.json。牌面资源位于 `/Game/GuLiStrike/Cards/GroundMech/Ricochet`。父材质重编译API返回成功，统计为309条像素着色器指令、12个采样器；这不是视觉验收。

## UE与PIE浏览入口（2026-10-27）

- 地图：`/Game/GuLiStrike/Cards/GroundMech/Ricochet/Maps/LVL_GroundMechRicochetReview`。
- Actor：`GroundMech_RicochetCardDirector`。一张有效卡牌居中，最大压动角12°；其他两个复用流程对象隐藏。
- 进入该地图后点击Play；移动鼠标观察层次，单击翻面，再次单击退场，可点击现有重播按钮恢复。Esc停止PIE。
- 地图使用现有独立卡牌演示GameMode与PlayerController，仅配置本地图的Actor实例。没有修改共享蓝图默认值、实战地图、卡牌表或奖励逻辑。标题／说明数据表为空，避免沿用其他卡牌文案。
- 本次已实际启动PIE，并保持开启供用户浏览。结构化回读确认Phase=1、有效卡牌数1、显示对象绑定本牌材质、相机指向本牌Director、视口1920×1081。记录见ue-review-map.json、ue-pie-start.json、ue-pie.json；运行记录是当时状态，不保证之后仍在PIE。
- 可复用入口：项目 `Scripts/Cards/author_ground_ricochet_review.py` 仅创建／更新评审地图；`Scripts/Cards/inspect_ground_ricochet_pie.py` 只读运行状态。通过 `Scripts/ue_exec.py` 在正确项目编辑器执行，勿在其他PIE会话中重建地图。
- UE视觉效果、真实鼠标倾斜、翻面与重播仍由用户验收；助手未截图或读取渲染结果。未执行原生构建。

## 统一美术目录入口

本牌已以ArtworkId=6纳入项目GuLiStrikeCardArtwork.xlsx。包内ImportToUE.py委托项目Scripts/Cards/card_artwork_pipeline.py；参数、贴图和父材质来源从统一目录读取。当前共享评审地图为 `/Game/GuLiStrike/CardSystem/RevealDemo/Maps/LVL_CardArtworkCatalogReview`，通过review --ids 6准备。原独立地图保留为历史入口；日常不再在本包脚本内另改材质参数。完整管线说明见项目`.agents/skills/guli-rogue-card-production/references/artwork-data-pipeline.md`。
