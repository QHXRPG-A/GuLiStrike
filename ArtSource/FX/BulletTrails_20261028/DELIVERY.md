# 游骑兵与Mass弹道／照明：正式接入

2026-10-29用户要求直接应用，并要求开发直接接入正式流程、删除启用开关与被替换旧代码。正式源表、VFX表与Catalog已保存，清理后的Editor代码已编译重载。**正常开火直接使用新效果，无额外命令。** 2026-10-30获准进场调试后修复了指挥官弹道槽位和生命周期，数字回读通过；视觉及性能仍待玩家验收，助手未截图或读图。

## 玩家使用

1. 打开`/Game/Maps/LVL_CommanderMassPrototype`，通过原本的房间流程选择Ground席位，正常开火。
2. 单发／连续射击，核验明亮橙黄色固定尾迹，以及弹头附近对地面、墙体的照明。
3. 用先驱号、重防号、扫荡者正常机枪交火，核验约为玩家四分之一宽的固定细曲线和友黄敌红；密集条件可用现有GM快速建立。
4. 命中后路径停止延长、旧尾迹继续淡出、弹头灯关闭。移动角色、转动镜头及离屏回屏，核验旧路径固定且不重新生长。
5. 在72m至88m间改变观察距离，核验照明筛选和淡出；玩家与Mass共享最多6盏。

原图中的专用候选Actor、候选命令和资源覆盖接口已删除。旧Mass CPU激光分支及旧子弹灯数据上传已删除，正式Mass只使用GPU尾迹；Ground保持CPU固定尾迹承载，碰撞、伤害及网络权威仍由CPU原系统处理。

## 正式资源

资源包沿用`/Game/GuLiStrike/FX/BulletTrails/Review_A1`。

| 正式ID | 资源 | 参数 |
|---|---|---|
| 8 PlayerBullet | NS_GroundBulletTrail_Orange / M_GroundBulletTrail_Orange | 主体／尾部#EE9D58、亮核#FEE4D9、发光8；24cm宽，0.12s停留＋0.38s消散 |
| 38 GroundMachineGunTracer | NS_MassBulletTrail_GPU / M_MassBulletTrail | 256槽GPU批量，6cm宽，1200cm最大窗口，弯曲12cm，0.06s停留＋0.18s消散 |
| 58 BulletLightPool | NS_BulletLights_CPU | 6槽CPU池；无阴影、透明受光或体积雾散射；半径800cm，亮度25 |

源表为`Data/Excel/GuLiStrikeVfx.xlsx`，经excelize和既有导出器维护，导入`/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects`。Catalog为`/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects`，BulletLightVfxId=58、MassTrailWidth=6。其他源表行保持，生成行结构未变化。

Mass按出生、结束、颜色与代次修订上传，逐帧只更新统一时钟及必要Bounds；离屏保留实际年龄。已留下的路径不跟随角色或镜头，终止后按点消散；不进行GPU读回。

## 技术记录

- [正式应用清单](formal-reference-proposal.json)：applied=true，记录直接应用授权，不冒充视觉审核通过。
- [正式保存回读](production_readback.json)：Mass为GPU、Ground和灯为CPU，正式资源路径、材质及灯光属性符合。
- [源码Editor构建与BuildId](Production_20261029/build-readback.json)：退出0，UBT耗时51.96s，使用D:/UnrealEngine-5.7，BuildId一致。
- [场景清理](production_scene.json)：专用候选Actor已从原图移除并保存。
- [最终回读](Production_20261029/final_readback.json)：退役类与接口不存在，正式Catalog生效，原图资源23/23、导航及200/600运行清单校验通过，脏包为空。

脚本`Scripts/Vfx/promote_bullet_trails.py`直接应用正式配置。`Scripts/Vfx/author_bullet_trails.py`维护现有资源和正式场景回读，不再建立启用开关。历史候选证据保留作过程记录，当前状态以上述正式回读为准。

性能收益为“未实测”。GPU批量、组件数和上传策略是实施结构，不能当作FPS收益；后续测量须在同负载下比较CPU、GPU及整帧。

## 2026-10-30维修基线

Mass弹道与共享灯池使用`ExecIndex()`分配独立槽位；Emitter持续循环、SpawnBurst仅在第一轮分配。原来的全部槽位为0和一次性Emitter配置已修复，重制入口同步使用此配置。两个正式系统已编译保存，不需要原生重编。

Commander正常交火中，33个活跃组件采样均为256粒子及256个唯一槽位，有效弹道透明度正常产生；灯池诊断保持6个唯一槽位。完整证据和边界见[诊断交付](../../../Artifacts/CommanderTracerFix_20261030/README.md)及[资源修复回执](slot_pool_repair.json)。重新Play选择Commander观察正常交火，确认子弹可见、友黄敌红和停止后尾迹消散。
