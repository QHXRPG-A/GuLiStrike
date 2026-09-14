---
schema: guli-progress/v1
id: DEV-20260912-005
work_id: WORK-20260912-005
kind: development
role: root
title: ShipComponent正式部件蓝图 — 制作与迁移
areas: [ship, assets]
status: done
verification: passed
created: '2026-09-12'
updated: '2026-09-12'
summary: 14个部件蓝图及两个飞船目录已保存，旧4个占位已清理；82个Socket保留，冷启动读取、源码Editor构建及既有2项部件验收通过。
next_action: ''
relations:
  requirement: REQ-20260912-005
status_note: 以重启编辑器后的磁盘读取和既有2项部件测试为最终证据。批量热重载触发过UE类重实例化崩溃，恢复后未重试该路径；最终无脏资源或地图，临时帧率设置已恢复。
---

# ShipComponent 正式部件蓝图 — 制作与迁移

对应[需求](../RequirementDocument/20260912-ShipComponent正式部件蓝图.md)。完整资产表和配置说明见[使用说明](../../outputs/ship-component-blueprints/README.md)。

## 制作方案

统一放在`/Game/GuLiStrike/Ship/Parts/BP_SC_*`。7个炮台引用骨骼版本；其余使用原静态模型。武器继承`GuLiStrikeWeaponPart`，无人机舱、干扰装置、护盾发生器继承通用部件类。原材质、单位安装偏移、现有炮口均保留。

文档01…07组定义保存在蓝图说明、元数据及`catalogue.json`中；CIWS复用于04／05组，Thor按06／07分成两个蓝图。伤害／射速／质量沿用原生默认值，PartId为空，通用弹丸复用现有BP_ShipProjectile；不借用旧占位数值行。

两个飞船蓝图的部件目录均切换为14项，母蓝图4个旧默认部件清除，当前无畏舰仍为空默认装配。旧4个蓝图删除前确认所有资产引用为空。源码仅替换既有2项部件测试中的资源路径，不新增测试或断言。

## 任务清单

- [x] 读取原文与4张槽位截图，备份旧蓝图／飞船配置／测试源文件，保存当前Socket基线。
- [x] 创建14个部件并编译保存，7项无分配部件保持空槽位数组。
- [x] 更新2个飞船目录，删除4个占位蓝图及源码硬编码引用。
- [x] 保存前后核对22个网格、7个Skeleton与82个Socket（无畏舰49＋母舰体7＋炮口26）一致。
- [x] 源码版Editor构建成功，退出码0；引擎、项目与5个项目插件BuildId均为`26d441ba-b96a-4e7b-b104-c2c6cd3e663c`。
- [x] 编辑器冷启动读取验证通过；既有InstallationRollback和VisualLifecycleAndSockets均成功，0错误、0警告。

## 已知设计差异

`bottom_mid_0/1/2`与`air_18`在当前无畏舰上不存在；兼容表保持文档名称，未映射到其他Socket。组05将`air_13`同时分给单管炮和CIWS，当前系统仍是一槽一部件。已在使用说明和配置报告中标出。

本次不做依赖／互斥运行判定、空地自动选敌、多炮口轮射、特殊部件能力或出生升级方案选择。

## 保存验证方式

批量热重载部件与父子飞船蓝图时，`UPackageTools::ReloadPackages`触发`BP_CombatAvatarFly01_DEADPACKAGE`与`REINST`默认对象冲突，编辑器崩溃。新资产和目录已提前保存，旧资产清理报告已写出。保留失败日志，改用正常启动编辑器重新读取磁盘，不重试同一热重载路径。

证据在[制作目录](../../outputs/ship-component-blueprints/README.md)；原始备份位于`outputs/ship-component-blueprints/backups/`。

最终状态见[验证报告](../../outputs/ship-component-blueprints/final-validation.json)和[增量归档](../Archive/20260912-ShipComponent部件蓝图与占位清理.md)。测试结束后回收临时世界、恢复帧率控制；编辑器保持原空战测试地图，未保存临时验收场景。
