---
schema: guli-progress/v1
id: ARC-20260921-008
work_id: ''
kind: archive
role: root
title: FireReview导航阻塞与资产校验修复
areas: [ground-mech, commander, combat, presentation]
categories: [art, gameplay]
status: recorded
verification: partial
created: '2026-09-21'
updated: '2026-09-21'
summary: 修复动态装饰网格持续更新导航引起的0单位问题，原双端PIE恢复16台；修复飞行导航校验器适用范围，地图保存重载及资产验证通过。
next_action: 玩家确认FireReview双方待命、敌军红色描边与受击销毁效果。
relations:
  work_items: [WORK-20260921-003]
status_note: 在用户已开启的PIE中定向诊断和修复，未主动启动新PIE或自动化；沿用此前结束游玩、源码Editor编译重启许可，保存后重载核对，视觉效果仍待玩家确认。
---

# 2026-09-21：FireReview单位生成与保存校验恢复

## 问题与修复

用户开启`/Game/Maps/LVL_GroundMech_FireReview`双端PIE后要求确认是否创建成功。初次读取发现权威、两端名册和模型实例均为0；四个部署点和16个出生点导航投影正确，资源就绪且未暂停，生成流程在导航就绪检查处返回。

3秒导航脏区日志捕获4296次更新，来源为本图4组装饰石的12个动态网格。仅关闭这些组件的`CanEverAffectNavigation`：`Floating_Rock_2`、`Floating_Rock_Light_BP_5`各5个，`Spinning_Rock_BP_8`、`Spinning_Rock_BP2_11`各`RockFlat2`。静止`RockLong2`底座仍参与导航；不改变模型、动画、碰撞或出生检查。原会话导航随即空闲并生成16台单位。

保存时另发现`GuLiFlightNavigationWorldValidator`将所有World视为适用资产，却对非必需且无飞行体积的地图返回NotValidated，触发UE5.7接口ensure。将适用范围判断移到`CanValidateAsset_Implementation`，保留必需地图及启用体积的原Cook检查。FireReview没有飞行体积，也不在必需地图名单中，修复后正确跳过该校验器。

## 交付与证据

| 文件或资产 | 变更 |
|---|---|
| `/Game/Maps/LVL_GroundMech_FireReview` | 保存12个地图实例导航排除项与重建的地面导航 |
| `Scripts/GroundMech/author_fire_review_range.py` | 增加`repair_navigation`，重建组件后重新定位、最终读回；后续重建保留排除项 |
| `GuLiFlightNavigationWorldValidator.cpp` | 在CanValidate入口排除不适用的World |
| `Scripts/GroundMech/README.md`、需求、开发与玩法文档 | 同步修复、授权与验证边界 |

- 原双端PIE修复后：权威生成标记true，两端各16个唯一且一致的ID，全部满血存活，红蓝各4扫荡者/4战争机器。四个正常机体批次各4台，Stencil为1/2且CustomDepth启用。证据`outputs/firereview-20260921/pie-after-navigation-repair.json`。
- 地图保存重载后：12项保持false；装饰Actor的类、变换、网格、相对变换与碰撞对比无其他差异；2个静止底座仍参与导航；部署总量16。源码Editor重启后再读回通过，重复修复组件变化数0。证据`scene-after-navigation-repair.json`、`scene-final-readback.json`。
- Python语法、C++接口和差异空白静态检查通过，不新增测试文件。
- 沿用用户“允许编译并重启UE，完成场景”和“允许结束当前PIE，完成场景”许可。源码`D:\UnrealEngine-5.7`构建`GuLiStrikeEditor Win64 Development`退出0，用时16.46秒；8份BuildId均为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。证据`editor-build-validator.log`、`buildids-validator.json`。
- 新Editor加载修复后，FireReview按Save用途执行资产验证：1通过，0无效、0警告、0未验证、0跳过；地面导航检查通过。证据`asset-validation-after-fix.json`。

## 遗留边界

未主动启动新PIE、自动化或压力测试。实际红色描边、停火及命中销毁视觉效果仍待玩家确认；`verification / partial`不代表视觉验收。此前[静态交付归档](20260921-FireReview双阵营靶场静态交付.md)保留原阶段事实，本记录追加运行发现与修复。

- [开发记录及玩家操作](../DevelopmentDocumentation/20260921-FireReview双阵营靶场与敌方描边.md)
