---
schema: guli-progress/v1
id: ARC-20261020-005
work_id: ''
kind: archive
role: root
title: Hybrid交火CPU优化阶段记录
areas: [performance, network, combat]
categories: [gameplay, performance]
status: recorded
verification: partial
created: '2026-10-20'
updated: '2026-10-20'
summary: build-09一轮有效前台配对平均24.776→23.412ms、P95 43.221→36.804ms；60Hz时钟及命中特效过期检查通过，平均15ms和三轮验收未完成。
next_action: 继续优化Hybrid主线程应用与网络压缩，重新完成三轮配对；玩家自行核验视觉效果。
relations:
  work_items: [WORK-20261020-004]
status_note: 本文记录可复现阶段事实，不代表整体目标完成；禁止助手读图测试。
---

# 2026-10-20：Hybrid交火CPU优化阶段记录

关联[需求](../RequirementDocument/20261020-Hybrid交火CPU与追帧优化.md)和[实施记录](../DevelopmentDocumentation/20261020-Hybrid交火CPU与追帧优化.md)。全部证据位于`D:/UE5.7/PerformanceCaptures/GuLiStrike/hybrid-cpu-20261020`。

## 变更与正确性边界

| 位置 | 本阶段实现 |
|---|---|
| `GuLiNetAsyncClient.cpp/.h` | 完整切点之间允许已排队逻辑时间按原三步分片推进；跳过空家族回调但保持逐步对象年龄交付；本地压缩缓存轻量索引及只读发送回调 |
| `GuLiHybridProjectilePool.cpp` | 生命周期轻量读取、重入独立的遍历数组复用、仅在需要时投影完整对象视图 |
| `GuLiUnitFeedbackSubsystem.cpp/.h`及命中准备入口 | 初始化准备相同命中资源，编辑器中提前请求缺失的死亡特效编译并非阻塞轮询 |
| `GuLiHybridWorldSubsystem.cpp/.h`、`GuLiHybridSessionSubsystem.cpp/.h` | 保留原队列处理，原生消息避开UE容器往返复制，故障注入及身份规范化仍走原入口 |
| `GuLiLocalPIESubsystem.cpp` | 将比较选项同步到所属逻辑客户端 |
| `Scripts/Performance/capture_hybrid_cpu.py`及分析脚本 | 原生30秒窗口、实际前台与镜头检查、客户端生效选项读回；显式后台诊断永不纳入验收 |

保留60Hz逻辑、原碰撞/伤害/退休时机、可靠协议、队列容量、Motion频率及视觉资源。缓存指纹只用于8MiB本地索引，命中仍逐字节比较正文；完整性SHA-256、压缩格式和字典不变。发送回调借用缓冲只持续到同步返回，拒绝时Outbox继续持有数据，接纳时队列保存独立副本。

修复过一个重要回归：最初跳过空回调时也遗漏了主线程对象年龄，远端新命中特效被误判为超过0.5秒。`offline-04-repro`复现失败，build-06修复后重新测试；没有放宽过期门槛。`pair-01`至`pair-05`存在时钟、镜头或后台问题，不能作为当前收益证据。

## 验证

- 静态检查：队列处理提取前后主体逐字比较相同；差异检查与Python语法检查通过。协议测试覆盖连续切点时钟、周期0对象年龄、两种缓存索引的精确编码及只读借用后的阻塞重试和FIFO；最新`offline-07/result.json`通过。
- 编译：沿用用户授权的源码`D:/UnrealEngine-5.7`、`GuLiStrikeEditor Win64 Development`及必要重启。`build-09.log`206项动作、93.07秒、退出码0；引擎、项目、GuLiFrameProfiler BuildId均为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`，编辑器已加载新模块。build-07首次因重复局部Scope声明失败，删除重复声明后重编通过，失败日志保留。
- 前台配对：用户恢复桌面后完成`pair-10`，同构建、2359×1748、133.4米、600初始单位、同冻结交火输入/画质/地图身份，Trace关闭，后台帧0，镜头匹配。对照保留当前构建共同的正确性修复与接口调整。
- 场景：`/Game/Maps/LVL_CommanderMassPrototype`已保存EditorOnly的`PerfHybridCPU_Entry`，默认停用，位置厘米`[10139.432,-55434.276,1101.779]`。Note包含可执行脚本入口及操作说明；`review-entry.json`核对类、标签、位置、文本和保存结果。该保存发生在采样之后，后续采样使用新地图身份。
- 玩家视觉：待用户自行核验发射、命中、死亡、移动及选择；未截图、读图或执行视口目检。

| pair-10指标 | 对照 | 候选 |
|---|---:|---:|
| 平均帧间隔（ms） | 24.776 | 23.412 |
| P95（ms） | 43.221 | 36.804 |
| 峰值（ms） | 106.303 | 67.506 |
| 逻辑步/墙钟秒 | 59.997 | 59.971 |
| 命中特效过期 | 0 | 0 |
| 命中特效接收/提交 | 7450 | 7179 |
| 命中特效年龄P95（ms） | 191.147 | 191.151 |

本轮平均改善约5.51%，不能归因于其中一项改动，也不能替代三轮验收。实际交火吞吐差异保留在原始证据中。

## 当前瓶颈与遗留事项

`diag-11-foreground`前台Trace中的Hybrid GT范围并集约11.981ms/帧，状态应用批次8.700ms包含子项；整个编辑器进程的Slate布局/绘制约4.138ms，本地玩家Slate操作另约1.281ms。没有完整游戏窗口归属证据，不能把全部Slate成本都归于游戏UI。

GPU Graphics队列`Frame`均值15.459ms、P95 17.713ms；Compute均值19.764ms、P95 34.939ms。队列范围包含空隙、同步和等待，GPU队列彼此及与CPU不能相加。该Trace不是关闭Trace的配对窗口，二者差值不是精确采集开销。

压缩缓存后台隔离诊断`diag-09-cache`每次查找约7.899→3.001μs；主要编码开销仍在实际压缩。全部后台诊断保留`acceptance_eligible=false`，不计入收益验收。

平均15ms目标仍未达成，三轮最终配对及玩家视觉验收尚未完成。本阶段只归档已实现、已构建和已测得的事实，工作项继续推进。
