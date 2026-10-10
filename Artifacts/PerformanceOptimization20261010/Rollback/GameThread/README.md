# 游戏线程完整分账与 GPU 瓶颈

此前列出的五项约22ms，只是少量原生子系统计数，不是完整GT。这里从完整GT事件树逐帧扣除直接子Scope，汇总互斥时间，避免重复计算父子包含时间。

同进程专服＋双客户端＋编辑器。压力600单位及四来源500附加飞行物；包含编辑器开销，不能当作独立客户端或独立专服成本。

| 归属 | 回退前完整压力窗口 ms/帧 | 回退后短检查 ms/帧 |
|---|---:|---:|
| UI与Slate（游戏和编辑器） | 18.09 | 16.27 |
| 移动组件、飞行Pawn及僚机相关Tick | 9.45 | 9.56 |
| 事件复制、姿态与网络收发 | 7.70 | 6.54 |
| 避障与共享路径处理 | 7.54 | 6.52 |
| 服务器弹丸模拟、碰撞和结算 | 6.93 | 5.90 |
| 其余引擎、编辑器与玩法Scope | 6.12 | 6.29 |
| 单位呈现、动画与渲染状态更新 | 5.65 | 5.71 |
| UWorld_Tick内未细分的自身耗时 | 4.69 | 4.90 |
| 客户端飞行与战斗特效表现 | 3.50 | 3.92 |
| StateTree | 1.95 | 2.24 |
| 任务调度和等待Scope自身耗时 | 1.90 | 2.04 |
| Niagara和特效组件CPU工作 | 0.83 | 0.97 |
| **合计** | **74.33** | **70.86** |

前列为30秒窗口中402个完整引擎帧（aa_final-stress-onscreen-aa0-r2-baseline），接近六轮GT均值中位数74.68ms；后列仅5秒中的69个完整帧。它们不是同负载阶段的A/B，不据此声称回退收益。Native捕获的首尾范围与Trace完整帧略有不同。
每帧所有Scope自身耗时之和与FEngineLoop::Tick闭合，最大误差小于0.000001ms；帧根之间平均间隙分别约0.00087和0.00060ms。

## 之前漏算的具体部分

- UI的8.45ms仅是GuLiSceneUI_Paint；整个Slate时间/Widget阶段14.39ms，LocalPlayer Slate处理3.29ms，另有输入和World中的UI更新。完整归属18.09ms。包含游戏与编辑器，不能全部称为游戏HUD。
- 避障5.3ms只含候选；还存在ManualAvoidanceRefresh约1.61ms，以及建网格、求解和共享路径工作。完整归属7.54ms。
- 服务器GuLiCombatEffects_Runtime包含弹丸池模拟、宽窄碰撞、WorldSweep与结算，合计6.93ms；它与客户端飞行物表现是两条不同路径。
- Projectile Movement完整4.21ms，CharacterMovement完整1.26ms，BP_CombatAvatarFly01_C约2.07ms，另有飞船/僚机Tick和组件传播，完整移动类分组合计9.45ms。ProjectileMovement是引擎组件名，不等于所有这些对象都是联网弹丸。
- CombatEffectReplication完整3.25ms之外，还有连接/通道收包、RPC处理、State/Pose编码解码等。分组7.70ms包含这些处理子树；它是GT耗时，不是带宽利用率，也不是仅socket传输。
- 单位呈现2.2ms之外还有骨骼组件/动画、DeferredRenderUpdates及渲染状态维护，完整分组5.65ms。发生在移动/RPC子树中的更新按调用方归属，避免重复。
- UWorld_Tick自身4.69ms没有更细的子Scope覆盖：说明耗时位于该作用域，尚不能认定全部是某一种引擎框架操作。
- ProcessUntilTasksComplete包含22.48ms，扣掉所执行的子任务后自身只有1.63ms；不能称22.48ms为空等。各调度/等待Scope自身合计1.90ms，仍可能含调度或未埋点工作，不是精确的纯阻塞时间。

## 剩余小项同样保留明细

“其余引擎、编辑器与玩法Scope”合计6.12ms；主要自身项如下，其余几百项仍可在accounting.json中逐项核对。

| Scope（已扣直接子项） | ms/帧 |
|---|---:|
| `GuLiCommander_SoldierCombat` | 0.595 |
| `FViewport_Draw` | 0.492 |
| `Presentation` | 0.399 |
| `UpdateCoreCsvStats_BeginFrame` | 0.385 |
| `Tick_Engine` | 0.375 |
| `Tick_Core` | 0.241 |
| `GuLiGroundMassContact_RefreshSnapshot` | 0.201 |
| `UPrimitiveComponent::GetStreamingRenderAssetInfoWithNULLRemoval` | 0.156 |
| `[TEDS] Process ObjectsNeedingSyncTags` | 0.149 |
| `GuLiWarMachineHoverPreviewComponent` | 0.116 |
| `Dispatch Processors` | 0.100 |
| `Chassis_Transporter_Cockpit_Lvl2` | 0.087 |
| `BeginRenderingViewFamily` | 0.087 |
| `ULandscapeSubsystem::Tick` | 0.079 |
| `FActorComponentTickFunction::ExecuteTick` | 0.078 |

所有归属按事件祖先路径确定，一个片段只计入一组。StateTree表项与之前2.4ms Native调度范围不同，不能将两张表混合相加。该账目为GT墙钟时间，含线程被调度走；没有足够的上下文切换记录把每项精确拆成on-CPU与等待。

## GPU

六个完整压力基线：总GPU窗口均值中位数23.57ms。主要已归属阶段：RenderVelocities 4.79ms、BasePass 4.17ms、SlateUI 2.14ms、CustomDepth 1.82ms、ShadowDepths 1.81ms；Unaccounted 2.57ms。200单位总GPU约11.54ms，ShadowDepths约1.54ms、NaniteVisBuffer约0.93ms。
GPU分类来自原CSV，可能有嵌套/异步，不按CPU互斥表方法直接相加。未进行逐网格/材质归因；当前最主要限制仍为GT。压力零真实枪口，不能拿此GPU数据证明完整反馈表现已优化。

## 原始证据与复现

- `*/input.json`：源Trace、时钟窗口、CSV统计和Insights工具路径。
- 完整 GT 事件 CSV/gzip 属于可再导出的中间文件，2026-10-11 清理后从 `input.json` 指向的本地原始 `session.utrace` 重导出；原始 Trace 与最终分账保留。
- `*/accounting.json`：完整自身/包含计时、组内明细、帧数与闭合误差。
- `aggregate.py`：读取CSV或gzip，重做相同逐帧互斥分账。
- `../bottlenecks.json`：六轮GPU/原生热点；`../smoke-verification.json`：回退后短检查及初始化错误边界。
