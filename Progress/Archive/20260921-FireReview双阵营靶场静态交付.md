---
schema: guli-progress/v1
id: ARC-20260921-007
work_id: ''
kind: archive
role: root
title: FireReview双阵营停火靶场与敌方描边静态交付
areas: [ground-mech, commander, combat, presentation]
categories: [art, gameplay]
status: recorded
verification: partial
created: '2026-09-21'
updated: '2026-09-21'
summary: 两队各8台真实Mass部队的部署已保存并重载核对，停火与阵营描边代码编译通过，实际效果交由玩家确认。
next_action: 玩家在/Game/Maps/LVL_GroundMech_FireReview确认待命、描边与受击销毁效果。
relations:
  work_items: [WORK-20260921-003]
status_note: 本轮只做静态检查、获准Editor构建和编辑器场景实体核对；用户另行许可结束外部启动的双端PIE，助手没有启动或执行PIE验收。
---

# 2026-09-21：FireReview双阵营靶场与敌方描边

## 变更清单

| 文件/资产 | 变更 |
|---|---|
| GuLiCommanderDeploymentPoint / GuLiBattleAuthoritySubsystem | 部署点自动开火开关，默认true，初始单位继承；false时保留受击与目标身份，停止主动攻击通道 |
| GuLiCommanderPresentationActor | 正常机体实例按兵种和阵营分批，写入阵营模板；换阵营迁移槽位，闪白与残骸不覆盖正常机体的模板 |
| GuLiCommanderPresentationPerformanceTests | 仅适配既有类型路由用例的阵营批次查询，不新增或运行用例 |
| 12个已有VFX调用文件 | 仅把所属头文件置为首个include，修复此前迁移留下的构建诊断 |
| /Game/Maps/LVL_GroundMech_FireReview | 四个2×2停火部署点、两个玩家起点、一处覆盖靶场的导航边界；保留原展示参考 |
| Scripts/GroundMech/author_fire_review_range.py | prepare/deploy/readback定向布置，固定Actor标签复用，未加载新字段或正在游玩时拒绝部署 |

## 决策与实现

- 用户选择每队4扫荡者与4战争机器的靶场模式，总计16台真实Mass单位，原本两台展示参考没有战斗阵营并予以保留。
- 复用原FireReview地面机甲与角色分配，单人属于红方、蓝方是敌人；红色描边由特效目录ID 15和当前玩家阵营共同决定，没有新增视觉资源或网络字段。
- 原定红方扫荡者中心X=5000导致一处位置距导航415cm，超过150cm投影容差。仅将该组中心调整到X=5500，保持Y=-5000、700cm间距和2×2阵型，其余中心维持计划。
- 默认停火不关闭伤害、碰撞或既有友军接触退让，销毁目标通过重新进入地图重置。

## 验证

- 静态代码审查、接口/调用扫描、差异空白检查及布置脚本Python AST解析通过；证据`outputs/firereview-20260921/static-review.json`与`source-changes.diff`。
- 用户明确许可“允许编译并重启 UE，完成场景”。使用`D:\UnrealEngine-5.7`构建`GuLiStrikeEditor Win64 Development`，两次退出码均0；首次发现的12处首头文件诊断已修正，最终构建不再出现。未构建Game目标，也没有启动自动化测试。
- 引擎、项目与6个本地原生插件共8份BuildId全部为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`，新编辑器读到停火字段默认true。日志与清单在`editor-build-final.log`、`buildids.json`。
- 场景保存并重新加载，读回四个部署点：Red=8、Blue=8，全部`allow_automatic_fire=false`，兵种与2×2数量正确，16个位置满足导航及两两间距规则。
- 重复部署前后四个部署点和导航体积路径完全一致，无重复对象。两个原展示参考的类与变换未变；玩家起点、Ground优先角色、相机管理器、ID 15及红色参数`(1,0.015,0.025,1)`已核对。证据`scene-readback.json`、`authoring-idempotency.json`与`navigation-readback.json`。
- 期间检测到由外部启动的双端PIE；收到“允许结束当前 PIE，完成场景”后只结束该会话并继续编辑器布置，没有将其作为验收证据。

## 遗留边界

实际描边、待命和命中销毁表现待玩家确认，开发状态为`verification / partial`。对应地图已保存、重载并完成实体数据核对，本轮无需等待玩家试玩反馈。

- [需求](../RequirementDocument/20260921-FireReview双阵营靶场与敌方描边.md)
- [开发、位置及玩家操作说明](../DevelopmentDocumentation/20260921-FireReview双阵营靶场与敌方描边.md)
