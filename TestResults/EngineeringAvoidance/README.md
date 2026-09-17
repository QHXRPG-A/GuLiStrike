# 工程车避障验证记录（2026-09-15）

源码引擎：`D:/UnrealEngine-5.7`，UE5.7.4。地图：`/Game/Maps/LVL_CommanderMassPrototype`。两客户端 Listen PIE，服务器通过车辆现有命令入口下令，客户端只读取复制状态；没有新增或修改自动化测试代码，也没有修改地图／资产。观察脚本仅作为当次 MCP 指令与只读采样使用。

## 构建与既有测试

- [最终构建／BuildId](buildids.json)：Editor、Game 均退出0；引擎、项目、5个原生插件一致。
- [最终既有测试](ExistingTestsFinal/index.json)：资源6、放置策略1、伤害账本3、Commander Mass导航34，共44通过、0失败。
- [旧BuildingWorld](BuildingWorld/index.json)：1失败。最先失败于测试使用不存在的建筑定义ID 0，未抵达后续导航断言。该测试源码还保留“PlacedBuilding不影响导航”的旧假设，本次未修改。

## 已观察

最终版本复查：

- [最终地面路线](final-ground-routes.json)、[统计](final-ground-summary.json)：矿区、兵营、停放车绕行及会车共5辆移动测试车全部进入500cm容差，终点距离482–496cm；矿区最大横向偏移6948cm、兵营3462cm。会车最小中心距2282.77cm，绕停车最小中心距2287.36cm，停放车位移0。
- [最终采矿循环](final-mining-cycles.json)、[统计](final-mining-summary.json)：世界112.82秒首次采到资源就绪；两辆初始矿车均未改变任务或坐标，各完成3次返厂卸货并再次采矿。地面行驶Mode0，采矿／等待／厂内轨迹Mode1。新部署中心由冷启动配置读取；500槽位校验成功后按GM20上限生成每队20人。
- [最终基地画面](final-base-view.png)已检查。`final-mining-view.png`首次截图镜头未对准车辆，只作为原始记录保留，不用于证明采矿或绕行。

下表保留此前定位及参与状态观察，详细版本区别见后文。

| 场景 | 证据 | 结果及范围 |
|---|---|---|
| 两建造车相向 | [head-on](head-on.json) | 两车抵达；最小中心距2279.04cm。 |
| 建造车与矿车交叉 | [crossing-aligned](crossing-aligned.json) | 两车到达容差范围；最小中心距2235.52cm，之后矿车按原规则恢复自动控制。 |
| 绕原位停放车 | [parked-original](parked-original.json) | 停放车位移0；最小中心距2287.46cm；行驶车抵达并恢复被动状态。 |
| 绕Mass士兵 | [mass-obstacle](mass-obstacle.json) | 稳定ID61保持(40000,-155000)、moving=0；最小中心距1899.57cm，车辆抵达。GM原始记录位于PIE-participation.log。 |
| 穿越整片矿区 | [ore-route](ore-route.json)、[实际场景](ore-view.png) | 车辆绕行约5853cm横向偏移后抵达；采样同时覆盖矿车进厂／对接／出厂的被动模式。 |
| 穿越兵营 | [building-route](building-route.json) | 横向绕行约3604cm后抵达；兵营中心不可投影。 |
| 采空／摧毁 | [removal](removal.json) | 调用正式采矿与伤害入口，采空矿体和Destroyed兵营原占位随后可导航投影。 |
| 相位退出／落地注册 | [transit-participation](transit-participation.json) | Ascending至WaitingForExit均Mode2／Registered0／无碰撞；ExitFlash与Ground为Mode1／Registered1，客户端阶段一致。 |
| 运输后继续地面移动 | [after-transit-and-factory](after-transit-and-factory.json) | 建造车重新建立走廊并抵达，最终客户端位置误差小于0.01cm；其中矿车未完成自动循环，见下方问题记录。 |

上述早期地面指令仍叠加胶囊半径，到达容差约1215cm。最终严格使用调用者给定的车体中心容差（普通500cm、采矿100cm），已由最终复查确认。

## 保留的中间失败与验证限制

- `parked-vehicle.json`：直接把初始停放车改坐标却未重建旧Detour走廊，不能当作正常停车验收。已用未改坐标的原位车辆复测通过。
- `crossing.json`：矿车在观察开始前恢复自动任务，非同步交叉；用`crossing-aligned.json`替代。
- `static-before.json`、`ore-before-samples.json`：矿体／建筑内部仍被视为可走。
- `static-after.json`、`static-after-ready.json`：只启用导航标志仍不足，初始化尺寸未刷新NavModifier缓存。`static-bounds-diagnostic.json`为边界重算诊断，不代替真实行驶。
- `after-transit-and-factory.json`：矿车停在采矿位前，MovingToCluster／Idle重复；最终撤掉额外胶囊到达半径后，两矿车各完成三次卸货及下一轮采矿，问题复查通过。
- 矿体开始阻挡导航后，默认500槽位第165个后与矿区冲突，整批生成被拒绝。将默认部署中心Y从±196000cm改为±180000cm后，完整500槽位预检通过；冷启动最终配置亦通过。未放宽导航和间距校验。
- 运行时导航初始生成耗时约两分钟；本轮未进行启动耗时优化、600单位性能或密集狭路压力验收。
- 不把位置采样或旧运输重连证据扩展为全部网络条件、全部地图或长期堵塞已通过。
- `PIE-final.log`是观察前启动失败的Editor日志：与同时启动的无头既有测试争用了Python桥端口，尚未进入PIE。结束该空闲进程、待测试退出后重新启动，最终有效冷启动日志为`PIE-final-native.log`。`PIE-participation.log`中的NavigationSystem CDO ensure来自Python只读导航投影调用；最终冷启动未出现该ensure。

PIE结束后已恢复GM每队人口上限300、占优采样0.25秒，未保存关卡。最终文件哈希见[source-hashes](source-hashes.json)。
