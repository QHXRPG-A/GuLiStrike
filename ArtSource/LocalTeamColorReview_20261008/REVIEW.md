# 红蓝模型分区对照 v1

18 个现有模型的独立派生材质预览。蓝红为候选队色，黄色为可变区标注；所有实际模型引用尚未切换。

[打开并排审核画廊](index.html)

范围：四种 Mass 兵种、八类玩法建筑、六座现有 SSF 建筑。采矿车、建造车、玩家地面机甲和 DIY 飞船模型不改色。

先核对黄色分区，再确认蓝红参考色。敌方可配置其他非蓝色，本版先用红色参考。固定机构、线稿、功能色和已有明暗参数保留；黄色标注采用独立显示，便于识别金属区域。

基础兵营和地图据点仍是当前玩法占位模型；前哨与工厂首版仅提出窄阵营标识条带。哨戒炮原导入父材质漫反射权重为零，本版仅在独立预览父材质中读取其已有底色纹理，原件不改。

这些是编辑器美术预览，不是新 UI、双客户端或环境隔离的运行验收。

## 01 · 先驱号

现有天蓝及珊瑚装甲色块；灯具、暖白和底盘固定

[蓝色原图](Renders/DefaultSoldier_blue.png) · [红色原图](Renders/DefaultSoldier_red.png) · [黄色分区](Renders/DefaultSoldier_regions.png) · [配色配置](Configs/DefaultSoldier.json)

## 02 · 重防号

橙色装甲及其暗橙分区；机械蓝灰、暖白、黄灯及线稿固定

[蓝色原图](Renders/WM01_blue.png) · [红色原图](Renders/WM01_red.png) · [黄色分区](Renders/WM01_regions.png) · [配色配置](Configs/WM01.json)

## 03 · 扫荡者

橙色侧甲；车轮、机枪、蓝灰机械结构、黄灯固定，保留无线稿例外

[蓝色原图](Renders/SweeperSummon_blue.png) · [红色原图](Renders/SweeperSummon_red.png) · [黄色分区](Renders/SweeperSummon_regions.png) · [配色配置](Configs/SweeperSummon.json)

## 04 · 彼之矛

青绿装甲与砖红标识；深灰结构、米色固定

[蓝色原图](Renders/BiZhiMao_blue.png) · [红色原图](Renders/BiZhiMao_red.png) · [黄色分区](Renders/BiZhiMao_regions.png) · [配色配置](Configs/BiZhiMao.json)

## 05 · 防空炮

仅 MetalPlate / OrangeMetalPlate 装甲槽；线缆、镜片、危险条纹和结构固定

[蓝色原图](Renders/MissileTurret_blue.png) · [红色原图](Renders/MissileTurret_red.png) · [黄色分区](Renders/MissileTurret_regions.png) · [配色配置](Configs/MissileTurret.json)

## 06 · 哨戒炮

原绿色彩绘甲片候选；灰色机构和炮口固定

[蓝色原图](Renders/SentryTurret_blue.png) · [红色原图](Renders/SentryTurret_red.png) · [黄色分区](Renders/SentryTurret_regions.png) · [配色配置](Configs/SentryTurret.json)

## 07 · 前哨建筑

无现成队色标记：候选为混凝土侧面窄阵营条带，混凝土主体固定

[蓝色原图](Renders/ManualOutpost_blue.png) · [红色原图](Renders/ManualOutpost_red.png) · [黄色分区](Renders/ManualOutpost_regions.png) · [配色配置](Configs/ManualOutpost.json)

## 08 · 基础兵营

当前玩法占位 Cube：仅窄阵营条带，未制作新建筑模型

[蓝色原图](Renders/BasicBarracks_blue.png) · [红色原图](Renders/BasicBarracks_red.png) · [黄色分区](Renders/BasicBarracks_regions.png) · [配色配置](Configs/BasicBarracks.json)

## 09 · 护盾发生器

青绿装甲和砖红标识；青色功能发光、灰白结构固定

[蓝色原图](Renders/ShieldGenerator_blue.png) · [红色原图](Renders/ShieldGenerator_red.png) · [黄色分区](Renders/ShieldGenerator_regions.png) · [配色配置](Configs/ShieldGenerator.json)

## 10 · 矿厂

实际工厂蓝图 Body 的 Armor 材质窄条带；门、室内、细部与坡道固定

[蓝色原图](Renders/ResourceFactory_blue.png) · [红色原图](Renders/ResourceFactory_red.png) · [黄色分区](Renders/ResourceFactory_regions.png) · [配色配置](Configs/ResourceFactory.json)

## 11 · 地图据点

当前玩法占位 Cylinder：仅窄阵营条带，未制作新建筑模型

[蓝色原图](Renders/TerritoryStronghold_blue.png) · [红色原图](Renders/TerritoryStronghold_red.png) · [黄色分区](Renders/TerritoryStronghold_regions.png) · [配色配置](Configs/TerritoryStronghold.json)

## 12 · 彼之矛（施工体）

沿用彼之矛装甲和标识分区，建造体结构固定

[蓝色原图](Renders/BiZhiMaoConstruction_blue.png) · [红色原图](Renders/BiZhiMaoConstruction_red.png) · [黄色分区](Renders/BiZhiMaoConstruction_regions.png) · [配色配置](Configs/BiZhiMaoConstruction.json)

## 13 · SSF 空军基地

复用现有 UV4 队色标记/Accent 分区；主装甲、结构、线稿、平台、灯具和无人机固定

[蓝色原图](Renders/SSF_AirBase_blue.png) · [红色原图](Renders/SSF_AirBase_red.png) · [黄色分区](Renders/SSF_AirBase_regions.png) · [配色配置](Configs/SSF_AirBase.json)

## 14 · SSF 克隆中心

复用现有 UV4 队色标记/Accent 分区；主装甲、结构、线稿、平台、灯具和无人机固定

[蓝色原图](Renders/SSF_CloningCenter_blue.png) · [红色原图](Renders/SSF_CloningCenter_red.png) · [黄色分区](Renders/SSF_CloningCenter_regions.png) · [配色配置](Configs/SSF_CloningCenter.json)

## 15 · SSF 指挥中心

复用现有 UV4 队色标记/Accent 分区；主装甲、结构、线稿、平台、灯具和无人机固定

[蓝色原图](Renders/SSF_CommandCenter_blue.png) · [红色原图](Renders/SSF_CommandCenter_red.png) · [黄色分区](Renders/SSF_CommandCenter_regions.png) · [配色配置](Configs/SSF_CommandCenter.json)

## 16 · SSF 军工厂

复用现有 UV4 队色标记/Accent 分区；主装甲、结构、线稿、平台、灯具和无人机固定

[蓝色原图](Renders/SSF_MilitaryFactory_blue.png) · [红色原图](Renders/SSF_MilitaryFactory_red.png) · [黄色分区](Renders/SSF_MilitaryFactory_regions.png) · [配色配置](Configs/SSF_MilitaryFactory.json)

## 17 · SSF 反应堆

复用现有 UV4 队色标记/Accent 分区；主装甲、结构、线稿、平台、灯具和无人机固定

[蓝色原图](Renders/SSF_Reactor_blue.png) · [红色原图](Renders/SSF_Reactor_red.png) · [黄色分区](Renders/SSF_Reactor_regions.png) · [配色配置](Configs/SSF_Reactor.json)

## 18 · SSF 战略中心

复用现有 UV4 队色标记/Accent 分区；主装甲、结构、线稿、平台、灯具和无人机固定

[蓝色原图](Renders/SSF_StrategyCenter_blue.png) · [红色原图](Renders/SSF_StrategyCenter_red.png) · [黄色分区](Renders/SSF_StrategyCenter_regions.png) · [配色配置](Configs/SSF_StrategyCenter.json)
