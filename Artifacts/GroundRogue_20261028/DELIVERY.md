# 游骑兵肉鸽与折射子弹

功能源码、Excel、正式数据和验证场景已接入。获准的GuLiStrikeEditor Win64 Development及GuLiCoordinator Release构建通过，新模块已加载；未启动PIE，功能、联机及画面待玩家验收。

初次额外折射5–8次，重复获取整体+1；每跳三维30米、最近无遮挡、不重复目标；直接伤害D×0.8^k。弹种快照和命中接口供后续灼烧能力继承，本期未制作灼烧。个人本局构筑，死亡保留，换席暂停，新局清空。06.01复用ArtworkId=6。

## 玩家入口

地图`/Game/Maps/LVL_CommanderMassPrototype`，选择红方Ground，至`GroundRogue_Entry=(-2800,54000,1149.04)`，面向西侧Enemy_01。新增场景位于`Review/GroundRogue_20261028`。

1. F4显示“游骑兵 普攻折射5–8次”；获取后再次F4显示6–9次，依次递增。
2. 向Enemy_01开火，普通子弹命中后链应立即结算并同时显示固定路径；检查最近目标、衰减、不重复命中和首靶死亡后继续。
3. 对照Friendly、BehindWall、OutOfRange检查友军/墙体/30米排除；9个连续靶可观察叠加强化。
4. 核验死亡复活保留、换席暂停并恢复、新局清空，以及联机迁移和重试不重复伤害。

12个单单位部署点禁止自动开火并保持闲置，另有地面、墙、入口，共15实体；原11个部署点保留。目标不足会提前结束；远靶与链尾之间超过30米。卡牌画面、路径和命中特效由玩家判断。

## 接入与验证

- 正式Excel导出36表；定向导入并保存回读GroundRicochet 1行、Mech技能3行、公共文本244行、Cards 6行，StringTable键绑定通过。
- 200/600 Manifest分别292/692对象、167规则，原生静态校验通过。
- Editor与Coordinator最终构建均退出0；引擎/项目BuildId一致。Ground F4使用控制器独立战斗上下文，折射受击中心避免使用Mass脚底锚点。
- Python AST、核心语法检查和差异检查通过；场景保存及实体读回通过。未新增或运行自动化测试，未做助手视觉测试。

证据：[构建](build_report.json)、[正式资产](asset_install.json)、[保存场景](scene_delivery.json)、[实施记录](../../Progress/DevelopmentDocumentation/20261028-游骑兵肉鸽与折射子弹.md)。
