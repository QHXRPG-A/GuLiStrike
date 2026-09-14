---
schema: guli-progress/v1
id: ARC-20260912-006
work_id: ''
kind: archive
role: root
title: ShipComponent部件蓝图与占位清理
areas: [ship, assets]
status: recorded
verification: passed
created: '2026-09-12'
updated: '2026-09-12'
summary: 14个正式部件蓝图替换旧占位目录，82个Socket完整保留；冷启动配置校验、源码Editor构建及既有2项部件验收通过。
next_action: ''
relations:
  work_items: [WORK-20260912-005]
status_note: 本轮仅制作部件及装配配置；升级依赖、互斥和火力定位保存在说明与元数据，未扩展运行期判定。热重载崩溃已通过冷启动恢复并验证保存结果。
---

# 2026-09-12：ShipComponent 正式部件蓝图与占位清理

依据用户提供的`D:\学习文档\ship第一版组件系统.md`及4张槽位图片实施，原文SHA256为`901d6d44406ad7250494b6c1df9c6f4b1b8353dcb0a987081b0b67647a0f1e6c`。对应[需求](../RequirementDocument/20260912-ShipComponent正式部件蓝图.md)与[开发记录](../DevelopmentDocumentation/20260912-ShipComponent正式部件蓝图.md)。

## 变更

| 文件／资产 | 结果 |
|---|---|
| `/Game/GuLiStrike/Ship/Parts/BP_SC_*` | 新建14个组件蓝图：7个Rigged炮台、6种其他模型及Thor二级版本；7个未分配部件的CompatibleSockets为空 |
| `BP_GuLiStrikeShip`、`BP_CombatAvatarFly01` | PartCatalogue改为上述14类，DefaultParts为空；保留舰体资产、安装变换及Socket |
| 原4个Engine／Weapon占位蓝图 | 清空引用后删除，备份保留在制作目录外置backups子目录 |
| `Scripts/author_ship_component_blueprints.py` | 可重用的配置、基线快照、蓝图制作和保护校验脚本 |
| `Source/GuLiStrike/Gameplay/Ship/Tests/GuLiShipPartVisualTests.cpp` | 两处旧Laser资源路径切换为新Thor蓝图，既有断言与测试范围不变 |

完整蓝图、模型与槽位对照见[使用说明](../../outputs/ship-component-blueprints/README.md)。父蓝图原4个默认占位装配已清除；本轮没有选择出生升级组或自动装满舰体。

## 配置边界

武器继承`GuLiStrikeWeaponPart`，无人机舱、干扰装置与护盾发生器继承通用部件类。7个炮台均使用已有骨骼网格，不复制静态炮台版本。材质沿用模型，安装微调为单位变换，PartId为空；质量、伤害与射速沿用原生默认值，通用投射物复用现有`BP_ShipProjectile`，尚非正式平衡数值。

已有炮口保留，武器的默认炮口名设为`Socket_1`；没有炮口的Missile_Bay留空。多炮口轮射、炮管俯仰、特殊部件能力不在本次范围。组01…07及依赖、互斥、空地定位写入蓝图说明、元数据与`catalogue.json`，没有新增运行期升级判定或自动选敌。

文档中的`bottom_mid_0/1/2`、`air_18`尚不在当前无畏舰上；保留原名称，不擅自增加或映射安装点。组05的`air_13`同时允许单管炮与CIWS，实际仍是一槽一部件。

## 验证

- 14个部件和两个飞船蓝图编译保存通过，0错误、0警告；冷启动读取结果与配置一致，4个旧资产已不存在。
- 22个网格、7个Skeleton和82个Socket保持一致：无畏舰49个安装点、母舰体7个安装点、骨骼炮台16个炮口、Thor／燃烧弹舱10个炮口。网格、材质、Skeleton及其安装原点未重写。
- 源码引擎`D:\UnrealEngine-5.7`构建`GuLiStrikeEditor Win64 Development`成功，退出码0。引擎、项目及5个项目插件的BuildId均为`26d441ba-b96a-4e7b-b104-c2c6cd3e663c`；本轮未新增运行期C++，未额外构建Game目标。
- 既有`GuLiStrike.Ship.Parts.InstallationRollback`与`VisualLifecycleAndSockets`均成功，0错误、0警告，见[自动化结果](../../outputs/ship-component-blueprints/Automation/index.json)。
- 测试后回收临时世界，恢复帧率控制；[最终校验](../../outputs/ship-component-blueprints/final-validation.json)显示无未保存资产或地图，原空战测试地图保持打开。

批量热重载父子飞船蓝图时，UE发生`DEADPACKAGE／REINST`默认对象冲突并崩溃；所有改动在此前已保存。保留`editor-reload-failure.log`，不重试同一热重载，改以正常重启后的[磁盘读取结果](../../outputs/ship-component-blueprints/cold-load-validation.json)验收。恢复窗口后，等待交互帧率的既有测试正常执行完成。

## 交付与后续

资产位于`/Game/GuLiStrike/Ship/Parts`，制作脚本及报告位于`Scripts/author_ship_component_blueprints.py`、`outputs/ship-component-blueprints/`。删除前的4个占位、两个飞船蓝图及测试源文件均已备份。后续需由设计确定缺失槽位、重叠槽位归属、正式数值和出生装配，再接入升级与能力逻辑。
