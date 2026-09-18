# 局内曝光与 Demo_Map 对齐

2026-09-18 更新：两张玩法地图已分别完成真实 PIE 前后对照，并定向保存光照与后处理修改。用户视觉复核仍待确认；战争机器拥挤是独立的尺寸参数问题，未在本次擅自改动玩法源码。

## 当前结果

- [实际截图对照](Review.html)：Demo 参考、指挥官修改前后、Ship 实际游戏镜头修改前后。
- [战争机器拥挤诊断与修复建议](WarMachineSpacing.md)：有效宽约12.5米，实际中心间距3.6米，统一避让半径1.5米。
- 新读取的 [Demo 参数](demo-reference-restart.json) 与 [参考视口设置](reference-viewport.json) 显示：Demo 地图自身曝光范围为0.5–0.6，但用户看到的编辑器视口固定在 **EV100=0**，并未使用地图曝光。
- 两张玩法地图固定曝光改为 **EV100最小=最大=0、补偿=0**，中性色彩校正和 Film 覆盖标记对齐实际参考。空战地图清除遗留 Gamma W=0.54、Contrast W=1.39、灰色色调0.614583的叠加压暗。
- 太阳使用白色、强度45、间接强度1；天空光强度2、白色、间接强度1，下半球颜色对齐Demo。Demo太阳3的临时候选使测试地图的棋盘地面过暗，已排除，最终以真实PIE校准结果为准。
- 保留太阳方向、阴影/实时捕获开关、Bloom、GI/反射方法、布局、模型材质、相机配置、游戏模式与玩法参数。没有修改全局渲染配置，也没有修改Demo_Map。
- 指挥官地图51个Actor、空战地图34个Actor，应用前后Actor名称/类/变换清单一致。只调用当前目标关卡保存；没有SaveAll，也没有保存原包报错角色蓝图。
- 保存后重新加载，两图各80个已应用后处理字段读回完全一致；没有未保存地图/内容包。临时单客户端预览已结束，PlayNumberOfClients恢复为原值2，当前回到指挥官地图。

## 证据与回退

| 内容 | 文件 |
|---|---|
| 指挥官真实PIE前/后 | [修改前](commander-restart-before.png) / [修改后](commander-demo-calibrated.png) |
| 指挥官辅助视角 | [40米臂长近景](commander-near.png) / [俯视](commander-top.png) |
| 空战真实PIE前/后 | [修改前](air-runtime-before.png) / [修改后](air-runtime-calibrated.png) |
| 原参考实际画面 | [Demo单位与环境](demo-reference-units.png) |
| 正式修改字段 | [Commander](commander-formal-applied.json) / [Air](air-formal-applied.json) |
| 定向保存结果 | [Commander](commander-save.json) / [Air](air-save.json) |
| 保存后读回 | [Commander](commander-saved-readback.json) / [Air](air-saved-readback.json) |
| 字段比对/会话恢复 | [每图80字段一致](saved-readback-comparison.json) / [恢复双客户端](session-restored.json) |

修改前原图备份位于 `Backup/`，保留当时用户已有内容；回退应先确认此后是否又有新地图改动，不能盲目覆盖。

| 文件 | 修改前SHA256 |
|---|---|
| Commander | EE00AFCDAE3F78F1B96F5D596A4177E9CB8176D977F30DCB46CAD417FF23F209 |
| Air | 2840A3303D7AEC216A2235F7CE0C508B8E4A74EAF54AFDCFDD7351A6B3ACA8B8 |
| Demo参考（未修改） | E8B86EF6AF41F72FA90437C880A31CA01F31A7CEFD1EE356035A7375A5ACD0A9 |

本次使用源码版 `D:/UnrealEngine-5.7` 实时编辑器。未修改C++/原生插件，未新增自动化测试或压力测试，故未重新执行原生编译。实际截图是光照检查，不代表完整战斗特效、性能或全部0.2缩放验收通过。

---

以下保留先前诊断和中断经过，不能当作当前完成状态。

## 当前方向

用户最新指定：“根据 demo_map 来调整可以吗？”后续以
`/Game/StylizedPineEnvironment/Maps/Demo_Map` 的实际呈现作为参考，调整
`/Game/Maps/LVL_CommanderMassPrototype` 与
`/Game/Maps/LVL_ShipWingmanAirCombatPrototype`。

只处理两张玩法地图的照明与后处理；保留布局、玩法、相机角度、模型和既有材质风格，不改 Demo_Map 和全局渲染配置。正式落盘前必须读取当前 Demo_Map 参数，并以真实局内画面验证，不能把旧记录直接当成当前值。

## 已取得的证据

- `baseline.json`：两张地图初始后处理、灯光、相关控制台变量及脏包状态。
- `commander-runtime-before.json`：指挥官双客户端 PIE；相机未发现额外曝光覆盖。
- 空战地图仍有启用的 Gamma W=0.54、Contrast W=1.39 与 SceneColorTint RGB=0.614583。指挥官地图上述项已恢复中性，不能将其继续偏暗简单归因于同一组旧参数。
- 两张地图原有固定曝光上下限均为 2，补偿 1.25；太阳强度 80、天空补光 2。单位三档材质使用固定艺术光的 Unlit 路径，太阳强度不是单位自身亮度的直接控制项。
- `commander-army-before.png` 与 `commander-army-exposure2.png` 为同机位实际 PIE 对照。第二张仅在临时 PIE 世界将曝光补偿 1.25 改为 2；单位更清楚，但地面也明显变白，不是最终方案。
- `commander-exposure-probe.json` 记录上述临时修改，保存包列表为空。
- `commander-before.png` 与 `commander-units-before.png` 是定位过程中的错误取景，不能作为单位前后对照。

已有 Demo 历史读回位于 `ArtSource/Environment/PineDemoAdaptation_20260918/`：太阳强度 3、曝光上下限 0.5/0.6、补偿 0，且有独立的后处理清理记录。这些仅用于下次读取时对照；尚未完成本轮实时复核。

## 编辑器中断

2026-09-18 17:31:35（北京时间），尝试 `Shot SHOWUI` 后编辑器退出。冻结日志
`editor-screenshot-crash.log` 的异常栈位于 Slate / D3D12 `RHIReadSurfaceData` / `TArray<FColor>` 截图读回路径，为访问异常。它支持截图读回相关崩溃的判断，但不构成已修复引擎崩溃的证据。

`commander-live-ui-exposure2.png` 未生成。后续降低太阳与天空补光的调用遇到连接重置，不能确认执行，不计作已验证结果。

本轮未保存任何 UE 包或正式地图，PIE 候选随退出丢弃。开始时存在一个用户预先未保存的内容包
`/Game/Assets/Props/Buildings/Stylized_Turrets_Tower_Defense/Stylized_Turrets_A_a`，本轮未代为保存，崩溃后其恢复状态未知。

## 中断时的下一步（历史）

编辑器关闭后按实时编辑器技能暂停连接重试，已请用户重新打开源码版项目。恢复后：先确认脏包与当前关卡；只读获取 Demo_Map 的实际灯光、曝光与色调映射；在临时预览中做同单位、同机位对照；分别检查指挥官和 Ship 的实际游戏镜头。避免重复使用导致此次异常的带 UI 截图路径。技术验证和用户视觉验收均未完成。

## 恢复后的处理

用户重启编辑器后，两张地图与内容包均无未保存改动。原包 `BP_ThirdPersonCharacter` 编译提示由用户明确点击“编辑器中运行”后继续，未修改或保存该角色蓝图。后续只使用已稳定取得图像的 `HighResShot` 路径，不重复触发原 `Shot SHOWUI` 读回；本轮恢复后未再次观察到截图崩溃，不声称引擎截图缺陷已修复。
