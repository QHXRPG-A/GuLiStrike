# 200 单位组合 P95：同局往返切换调查

2026-10-10，接续用户“继续找原因”。本次确认了一个此前未控制的负载因素：**200 是受控 Mass 单位数，场景还运行着 16 台采矿车和 18 台工程车；车辆活动阶段改变会显著改变模型层级更新量。** 全关对照也出现相同峰值。因此，旧结果仍表示“未通过采用检查”，不能直接解释为条件缓存＋避障开关造成的算法退化。

尚未完成全部因果归因：UI/Slate 单次成本的波动，以及游戏线程未执行时间的内部等待/OS 调度来源，仍没有锁定。本次没有修改游戏运行时代码或把组合改判通过；四个新候选保持关闭。

## 复现和判定方法

- 源码 UE 的 build4，`GuLiStrikeEditor Win64 Development`，原图 `/Game/Maps/LVL_CommanderMassPrototype`。沿用同进程专服＋双客户端、各 1280×720、200 单位移动/避障/自然交火、原镜头与画质、每连接 250000 B/s。
- A 为四项全关；B 为 `ConditionSnapshot=1, ParallelCandidates=1`，另外两项关闭。
- 六局独立 PIE：AAA、ABA、BAB、ABA、BAB、AAA；每局内部连续三段。第一段预热 10 秒，后续切换后等待 2 秒，每段采 15 秒，共 18 段。这是定位用的短窗口，**不替换原 30 秒正式矩阵，也不作为默认采用依据**。
- 使用原捕获入口，增加可选的同局复用、等待时长和是否结束 PIE 参数；原调用默认行为仍为重建、预热 10 秒、结束 PIE。没有新增测试框架。
- 采样期没有截图、UE 对象枚举或资产写入。原生网络数据沿用原采集器；外部 Python 每秒只读 Windows `GetThreadTimes / GetProcessTimes / GetSystemTimes`，内存保存后导出，没有新增网络 RPC。
- 预期：若开关存在直接、稳定的整帧成本，ABA 和 BAB 应显示同方向变化。实际：四组差值有正有负。按实际窗口中心对两侧插值，只控制线性时间漂移；不假定自然交火、车辆路线或系统负载完全相同。

| 对照 | B−A 整帧均值 ms | B−A 整帧 P95 ms | B−A StateTree 调度 ms/帧 |
|---|---:|---:|---:|
| ABA1 | -0.1136 | -0.4866 | -0.0759 |
| BAB1 | -0.4087 | -0.2484 | -0.0510 |
| ABA2 | +1.0951 | +1.2352 | -0.0765 |
| BAB2 | +0.1293 | +0.3900 | -0.0453 |

AAA1 的三段 P95 为 32.0741、32.8700、33.8846 ms；AAA2 为 33.4496、34.0393、33.3520 ms。AAA2 中间段相对前后插值仍多 0.6385 ms，均值多 0.8809 ms。原先三对新局 A/A 的 0.4225 ms 是那批样本的工程参考，不能覆盖这些不同时间阶段的波动；也不能据新波动范围倒改旧验收结果。

## 已确认的增量路径

`context-before.json` 实体类计数证实每个 World 都有 16 台 `GuLiMiningVehiclePawn` 和 18 台 `GuLiConstructionVehiclePawn`。这是同一组 34 台权威车辆及两份客户端副本，不是额外 102 台服务器单位；模型 ChildActor 也不能再算成车辆。

ABA2 最差段的调用树为：

`UWorld_Tick → UCharacterMovementComponent_TickComponent → VehiclePresentation → SkeletalMeshComponent0 → 炮塔/采集器/挂载件`

[矿车源码](../../../Source/GuLiStrike/Gameplay/Resources/GuLiMiningVehiclePawn.cpp) 将 `VehiclePresentation` 挂在运动胶囊上。引擎 `SceneComponent.cpp:748` 的 `UpdateComponentToWorldWithParent` 使用 UObject 计时；`PropagateTransformUpdate` 继续更新 Bounds、渲染变换及子组件。因此这些动态组件名是级联变换路径，不能统称为“骨骼 Tick 数”或把每个名字当成新增 Actor。

| 同一局中的三段 | 第一段 | 中间段 | 最后段 | 中间相对两端均值 |
|---|---:|---:|---:|---:|
| ABA2 矿车模型更新调用/帧 | 36.52 | 81.40 | 55.11 | +35.59 |
| ABA2 模型更新包含时间 ms/帧 | 0.5356 | 1.1679 | 0.8170 | **+0.4916** |
| AAA2 模型更新调用/帧，全关 | 32.75 | 83.06 | 54.89 | +39.24 |
| AAA2 模型更新包含时间 ms/帧，全关 | 0.4898 | 1.2327 | 0.8292 | **+0.5733** |

中间阶段的额外模型变换在全关对照中同样出现，证实它是本次对照的混杂因素。该发现与既有 [车辆移动开销分析](../../../Progress/Archive/20261010-车辆CharacterMovement与飞船僚机移动开销拆分.md)一致；本轮新增的是往返/全关对照中相同时间阶段的峰值证据。**它不能单独解释旧三轮所有 P95 增量**：旧第二轮 `VehiclePresentation` 均值只增加约 0.0123 ms，旧第一、三轮为 0.1654、0.0483 ms。

此外，ABA2 的 `Slate::DrawWindows` 中间段增加约 0.7264 ms/帧；AAA2 全关中间段也增加约 0.6753 ms/帧。SceneUI 命令量相近，说明只用单位数/图元数无法控制单次成本。`DrawWindows` 包含 SceneUI Paint，`CharacterMovement` 包含 VehiclePresentation；父子 Scope 不能重复相加，均值增量也不是 P95 的加法分解。

再按完整引擎帧做互斥窗口归属，结果如下；与全窗口 timer 统计相差数微秒，来自首尾不完整 Scope 的处理。仅此表同一行的四项可相加，布局是所有窗口的合计，没有推定其客户端/编辑器比例。

| 中间段相对两端均值，ms/完整帧 | 两客户端游戏UI绘制 | 编辑器窗口绘制（扣除游戏UI） | 全窗口Prepass布局 | 其余公共绘制 | DrawWindows总增量 |
|---|---:|---:|---:|---:|---:|
| ABA2 | +0.3278 | +0.1554 | +0.1548 | +0.0826 | +0.7206 |
| AAA2，全关 | +0.2350 | +0.1693 | +0.1978 | +0.0769 | +0.6789 |

两个真实游戏 UI 根按内部 `GuLiSceneUIWidget` 标识识别；编辑器视口也有一个空的 `Paint: Game UI`，不能按同名根数量把它误算成第三个客户端。首次离线划分的该假设被断言拦截，保留 `slate-export.log`；修正后的 `slate-export-v2.log` 和六份分组结果覆盖全部完整帧，未删除任何运行样本。Scope 的归属已进一步明确，但尚无证据说明未改动 UI 的单次成本为何在该段升高。

## 线程与网络排除

ABA2 的 Windows 线程计时估算：相对两侧，GT 实际 CPU 时间约增加 0.622 ms/帧，未执行时间约增加 0.473 ms/帧。二者合计对应约 1.095 ms 的均值增量。取每个窗口内部完整的约 14 秒 OS 采样区间，再用该窗口平均帧时换算；这是窗口估算，不能用来定位某一帧、某个 Scope 或精确划分 P95。

“未执行”可能包括主动等待或被调度走，现有数据不能区分。该段整机 CPU 占用约 16.8%，前后约 19.7%、17.8%，不支持用“整机 CPU 被后台占满”解释。UE CSV `CPUUsage_Process/Idle` 的 Windows 实现使用 CycleTime 与 QPC 派生分母，本机原始数值不能直接当任务管理器的标准百分比；本次使用 Windows 时间差单独核对。

18 段中的每服务器连接平均发送为 **71.19–86.03 KB/s**，预算始终 250 KB/s；飞行流预算阻塞为 0，队列末尾均为 0，队列守恒误差为 0，连接丢包计数为 0。部分突发命中 8 批上限，但没有持续队列增长。本次 200 单位延迟不支持归因于持续带宽积压；这个结论不覆盖已有 600 单位压力积压。

## 假设核对与剩余工作

| 假设 | 本轮结果 | 下一步边界 |
|---|---|---|
| 场景实际工作量只有固定的 200 单位 | 排除；34 台 Actor 车辆的变换更新量随阶段明显变化 | 对照需记录车辆活动/变换量；可另做临时停驻车辆的隔离样本，但不能冒充完整 Actor＋Mass 验收 |
| 条件缓存本身反复读取/增加计算 | 四组目标时间均减少 0.045–0.077 ms/帧，未发现此证据 | 保留 Actor/Mass 行为与完整视角采用门槛 |
| 200 单位避障分块大量等待 | 仍大多走串行；候选查询等待约万分之几毫秒/帧 | 不能用该项解释毫秒级整帧增量 |
| 连接预算不足造成这次 P95 | 本批连接未发生预算阻塞或持续排队 | 压力场景网络报告仍独立有效 |
| 剩余 UI/Slate 与调度波动 | 路径明确，但内部触发因素未定；全关也出现 | 若继续追根，应在同一负载阶段补 GT 上下文切换/核心迁移数据及窗口布局细分，不再只重复全局开关矩阵 |

没有定位到应修复的条件缓存/候选查询功能错误，本次没有提交臆测的运行时修补，也没有宣称 P95 已修复。默认采用结果不变。

## 复现文件与收尾

- [run-switchback.py](run-switchback.py)：复用捕获入口的六局诊断适配器。
- [analysis.json](analysis.json)、[analyze-switchback.py](analyze-switchback.py)：18 段完整负载/网络汇总、四组往返及两组全关对照。
- [os-times.json](os-times.json)：累计进程/线程/系统时间；不是后台进程活动猜测。
- [cause-evidence.json](cause-evidence.json)、[summarize-cause.py](summarize-cause.py)：Scope 明细、车辆实体、网络、配置/源码核对。
- `Paired/*/frames.csv`、`session.utrace`、`native-capture.json`、`clock.json`：原始证据。AAA2/ABA2 有完整窗口 timer 导出；两段中间窗口有额外 2 秒完整调用树 `frame-detail/events.csv`。
- [slate-partition.json](slate-partition.json)、[export-slate-partition.py](export-slate-partition.py)：离线将窗口绘制按游戏 UI 根、含 SLevelEditor 的窗口剩余部分及公共绘制分为互斥项；不猜客户端窗口身份。
- 控制值逐项恢复、两个生产配置哈希未变，27 份 build4 原生实现源码哈希仍一致；未重新编译。18 段采样错误为空。PIE 已停止，[本次 Editor PID 38804 已退出](editor-exit.json)。

本轮属于诊断和捕获工具维护，没有新的玩家功能或资产变更；复用已保存的原地图入口，不另造无关验证场景。
