# 指挥官兵种弹道不可见：2026-10-30诊断交付

玩家明确对象为“指挥官兵种的弹道”，并授权进入对局调试。使用现有Commander普通PIE及数字采集，没有截图、读图或自动化测试。

## 最终修复

- `NS_MassBulletTrail_GPU`与`NS_BulletLights_CPU`：槽位初始化使用HLSL `ExecIndex()`，不再通过参数图读取零默认值。
- Emitter为Infinite，SpawnBurst启用UseLoopCountLimit、Loop Count Limit=1，每次激活只生成固定槽位。
- `Scripts/Vfx/author_bullet_trails.py`的完整制作与`repair_slot_pools`定向维修共用该函数。
- 只修改项目自有资源；原生源码、数据表路径、伤害、阵营色、线宽和Ground弹道未改。

## 最终证据

1. `final_runtime_index_fix.json`：普通Commander交火24.03秒、13次采样，无采集错误。33个活跃组件样本均为256粒子、256个唯一槽位、最小0最大255；9次交火采样存在有效透明度，单组件为5–35个有效粒子。末尾累计发射/回收均3875，活弹归零，组件回池。
2. `light_slot_runtime.json`：CPU灯池7.31秒内9次采样保持6粒子及0–5独立槽位，覆盖空闲/重新启用输入和多轮Emitter周期。布尔属性未由int读取得到数据，不作为开关或实际照明通过证据。收尾调用的`deactivate_immediate`未暴露给Python，留下清理异常；随后正常停止本次PIE完成组件和会话清理，见下一项。
3. `final_readback.json`：两个系统valid/ready、VM UpToDate，GPU完成编译，循环/首次分配配置及正式ID38/58回读正确，保存成功。原地图28个部署点可读；会话active=false、cleanup_complete=true；默认Ground席位和开局询问恢复。
4. `ArtSource/FX/BulletTrails_20261028/slot_pool_repair.json`：正式资源0编译错误、0警告及维修配置。

## 保留的中间诊断

`initial_runtime.json`、`reactivation_probe.json`、`after_reactivation.json`记录初始组件状态；`asset_loop_fix.json`、`controlled_runtime.json`等只反映循环调整的中间阶段。单独改Infinite曾造成重复分配，见`numeric_fixture.json`/`numeric_fixture_attributes.json`，最终已加入首轮限制。

`particle_runtime.json`误用带Particles前缀的SimCache属性名而读取空数组，不证明粒子为零。`final_runtime.json`属于尚未修好槽位索引的中间版本：虽然已固定256粒子，仍全部读取槽位0；不能将该文件当作最终通过。最终依据是`final_runtime_index_fix.json`。

## 玩家入口和边界

重新Play `/Game/Maps/LVL_CommanderMassPrototype`，选择Commander；在已有兵种交火区观察机枪。调试关注位置为世界坐标约(-4300,54000,905)cm，未改地图的28个部署点。确认子弹可见、友黄敌红、命中后尾迹消散及近景照明。

画面和同负载性能尚未由玩家确认。SimCache只用于获准诊断，正式运行没有新增GPU回读。本次无原生代码修改或原生构建；无自动化、截图或其他网络/伤害专项回归。
