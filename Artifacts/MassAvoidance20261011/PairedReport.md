# Mass 避障与转向固定对照证据

同进程专服＋双客户端＋编辑器；双客户端均为 1280×720。1 KB = 1000 B；每条连接预算为 250000 B/s。
每轮重建 PIE、预热 10 秒、采样 30 秒。基线与候选来自同一个构建，仅切本地开关。
压力窗口实际为 600 单位＋现有三来源 500 附加飞行物（167/167/166）。原计划四来源之一已退役，本轮未恢复；基线与候选一致，结论限定于实际场景。
整帧时间包含服务器、两个客户端和编辑器工作，不能推算独立客户端 FPS。

采用决定：完整组合在200与600+500场景的三组配对均通过原始CPU与整帧P95门槛，默认采用0.5秒/六候选/共享索引/保守零缓存/三倍转向；独立避障及旧避障加三倍转向在压力场景未全通过，不宣称这些组合已通过。
代码、失败配对诊断、网络归因与地图操作见[开发记录](../../Progress/DevelopmentDocumentation/20261011-Mass避障简化与转向提速.md)。

## 门槛

| 场景 | 候选 | CPU 节省 ms/帧 | A/A CPU 波动 | 各配对 P95 差 ms | A/A P95 波动 | 原始门槛 |
|---|---|---:|---:|---|---:|---|
| dense200 | avoidance | 0.168 | 0.002 | -0.037, -0.501, -0.425 | 1.234 | 通过 |
| dense200 | turn | 0.020 | 0.002 | -0.271, +0.267, -1.102 | 1.234 | 通过 |
| dense200 | combined | 0.184 | 0.002 | +0.480, -0.338, -2.762 | 1.234 | 通过 |
| stress | avoidance | 4.118 | 0.706 | -8.296, +0.949, -5.206 | 0.748 | 未全通过 |
| stress | turn | 0.108 | 0.706 | +1.814, +1.972, +5.200 | 0.748 | 未全通过 |
| stress | combined | 4.227 | 0.706 | -4.295, -3.129, -3.577 | 0.748 | 通过 |

CPU 指标为服务端预测避障总耗时＋软避障总耗时；其内部 Query/Grid/Prepare 不能再次相加。转向属于行为要求，CPU 节省门槛不适用，但仍检查整帧退化。原始门槛逐配对检查，失败样本保留，不用平均值覆盖失败。

## 各窗口

| 窗口 | 总避障 ms | 查询 μs/次 | 趋势/查询 | 缓存命中 | 整帧均值/P95 ms | GPU 均值/P95 ms | 枪口出生（两个客户端） |
|---|---:|---:|---:|---:|---|---|---|
| aa-dense200-onscreen-same-version-r1-baseline | 0.538 | 4.655 | 105.81 | 0 | 28.369/33.230 | 11.696/13.911 | 7922,7922 |
| aa-dense200-onscreen-same-version-r2-baseline | 0.536 | 4.581 | 106.71 | 0 | 28.740/34.463 | 11.790/13.521 | 7920,7920 |
| aa-stress-onscreen-same-version-r1-baseline | 5.446 | 11.031 | 271.45 | 0 | 59.887/70.934 | 23.914/35.799 | 620,0 |
| aa-stress-onscreen-same-version-r2-baseline | 6.152 | 12.799 | 319.41 | 0 | 58.753/71.682 | 23.218/28.334 | 0,0 |
| avoidance-dense200-onscreen-avoidance-r1-avoidance | 0.416 | 1.323 | 5.96 | 0 | 30.249/34.971 | 11.856/13.994 | 7920,7920 |
| avoidance-dense200-onscreen-avoidance-r1-baseline | 0.564 | 4.734 | 106.37 | 0 | 29.621/35.007 | 11.722/13.967 | 7920,7920 |
| avoidance-dense200-onscreen-avoidance-r2-avoidance | 0.384 | 1.290 | 5.95 | 0 | 29.118/34.487 | 12.261/14.209 | 7920,7920 |
| avoidance-dense200-onscreen-avoidance-r2-baseline | 0.566 | 4.744 | 108.56 | 0 | 29.823/34.988 | 12.307/14.322 | 8040,8040 |
| avoidance-dense200-onscreen-avoidance-r3-avoidance | 0.405 | 1.320 | 5.95 | 0 | 30.036/34.986 | 12.410/14.730 | 7920,7920 |
| avoidance-dense200-onscreen-avoidance-r3-baseline | 0.580 | 4.651 | 107.27 | 0 | 30.545/35.410 | 12.206/14.665 | 7922,7922 |
| avoidance-stress-onscreen-avoidance-r1-avoidance | 2.031 | 1.587 | 5.95 | 30 | 58.246/69.499 | 26.260/36.267 | 1691,0 |
| avoidance-stress-onscreen-avoidance-r1-baseline | 6.638 | 12.671 | 310.76 | 0 | 63.680/77.796 | 25.974/37.658 | 0,0 |
| avoidance-stress-onscreen-avoidance-r2-avoidance | 2.204 | 1.769 | 6.00 | 0 | 57.708/72.285 | 28.098/35.681 | 0,0 |
| avoidance-stress-onscreen-avoidance-r2-baseline | 5.616 | 11.156 | 282.28 | 0 | 60.337/71.336 | 25.523/36.763 | 31,0 |
| avoidance-stress-onscreen-avoidance-r3-avoidance | 2.036 | 1.626 | 5.92 | 107 | 57.823/69.992 | 24.181/31.112 | 1196,0 |
| avoidance-stress-onscreen-avoidance-r3-baseline | 6.373 | 12.491 | 317.56 | 0 | 62.261/75.197 | 26.858/41.831 | 0,0 |
| combined-dense200-onscreen-combined-r1-baseline | 0.582 | 4.555 | 105.97 | 0 | 31.797/37.304 | 11.783/14.308 | 6420,6420 |
| combined-dense200-onscreen-combined-r1-combined | 0.428 | 1.295 | 5.96 | 0 | 32.766/37.784 | 12.188/14.613 | 7922,7922 |
| combined-dense200-onscreen-combined-r2-baseline | 0.632 | 4.728 | 107.98 | 0 | 34.068/39.208 | 12.230/14.183 | 7922,7922 |
| combined-dense200-onscreen-combined-r2-combined | 0.430 | 1.292 | 5.95 | 0 | 33.588/38.870 | 12.124/14.612 | 7920,7920 |
| combined-dense200-onscreen-combined-r3-baseline | 0.615 | 4.475 | 105.14 | 0 | 33.890/39.827 | 12.083/14.569 | 7860,7860 |
| combined-dense200-onscreen-combined-r3-combined | 0.420 | 1.309 | 5.95 | 0 | 32.517/37.065 | 12.070/14.486 | 7920,7920 |
| combined-stress-onscreen-combined-r1-baseline | 6.043 | 11.173 | 282.54 | 0 | 65.945/77.526 | 25.468/40.612 | 165,0 |
| combined-stress-onscreen-combined-r1-combined | 2.125 | 1.541 | 5.93 | 16 | 62.479/73.231 | 26.997/37.274 | 1564,0 |
| combined-stress-onscreen-combined-r2-baseline | 6.004 | 10.978 | 278.30 | 0 | 65.165/76.233 | 25.298/31.994 | 6,0 |
| combined-stress-onscreen-combined-r2-combined | 2.307 | 1.753 | 6.00 | 0 | 60.990/73.103 | 25.890/33.055 | 0,0 |
| combined-stress-onscreen-combined-r3-baseline | 7.367 | 13.666 | 353.43 | 0 | 66.471/79.197 | 25.762/36.488 | 0,0 |
| combined-stress-onscreen-combined-r3-combined | 2.300 | 1.744 | 6.00 | 0 | 61.336/75.620 | 25.976/38.297 | 0,0 |
| turn-dense200-onscreen-turn-r1-baseline | 0.601 | 4.780 | 107.87 | 0 | 31.553/36.637 | 12.150/14.078 | 8040,8040 |
| turn-dense200-onscreen-turn-r1-turn | 0.567 | 4.470 | 107.14 | 0 | 31.200/36.367 | 12.177/14.013 | 7920,7920 |
| turn-dense200-onscreen-turn-r2-baseline | 0.610 | 4.690 | 107.46 | 0 | 32.254/38.015 | 12.138/13.991 | 8246,8246 |
| turn-dense200-onscreen-turn-r2-turn | 0.607 | 4.665 | 106.82 | 0 | 32.445/38.282 | 12.040/14.503 | 7922,7922 |
| turn-dense200-onscreen-turn-r3-baseline | 0.587 | 4.687 | 111.41 | 0 | 31.817/37.553 | 12.142/14.085 | 8220,8220 |
| turn-dense200-onscreen-turn-r3-turn | 0.564 | 4.500 | 106.29 | 0 | 31.052/36.451 | 12.144/14.077 | 8184,8184 |
| turn-stress-onscreen-turn-r1-baseline | 5.802 | 11.105 | 281.26 | 0 | 63.087/73.453 | 27.779/40.733 | 752,0 |
| turn-stress-onscreen-turn-r1-turn | 5.743 | 10.856 | 272.44 | 0 | 64.102/75.267 | 26.041/37.000 | 1473,0 |
| turn-stress-onscreen-turn-r2-baseline | 6.503 | 12.610 | 326.53 | 0 | 63.601/75.794 | 26.032/33.094 | 0,0 |
| turn-stress-onscreen-turn-r2-turn | 6.788 | 12.810 | 325.13 | 0 | 65.308/77.766 | 27.586/38.011 | 0,0 |
| turn-stress-onscreen-turn-r3-baseline | 6.666 | 12.790 | 324.30 | 0 | 64.453/76.054 | 25.817/34.200 | 0,0 |
| turn-stress-onscreen-turn-r3-turn | 6.115 | 11.208 | 271.49 | 0 | 66.958/81.254 | 23.758/29.455 | 1474,0 |

## 网络

| 场景/候选 | 服务器连接发送 KB/s（每条，均值） | 产生事件/墙钟秒 | 产生事件/模拟秒 | 发送调用/秒 | 飞行队列增长/秒 | 结束队首年龄 s |
|---|---|---:|---:|---:|---:|---:|
| dense200/avoidance/baseline | 74.1, 74.2 | 266.0 | 266.0 | 20.00 | 0.0 | 0.00 |
| dense200/avoidance/avoidance | 74.1, 74.4 | 264.6 | 264.6 | 20.00 | 0.2 | 0.05 |
| dense200/turn/baseline | 75.2, 75.1 | 268.5 | 268.5 | 20.00 | -1.7 | 0.05 |
| dense200/turn/turn | 74.8, 74.8 | 265.8 | 265.9 | 20.01 | 0.6 | 0.05 |
| dense200/combined/baseline | 72.9, 72.6 | 266.8 | 266.8 | 20.01 | 2.9 | 0.02 |
| dense200/combined/combined | 73.7, 73.4 | 266.0 | 266.1 | 20.01 | 0.6 | 0.01 |
| stress/avoidance/baseline | 250.0, 250.4 | 1026.1 | 1025.8 | 16.11 | 830.2 | 11.79 |
| stress/avoidance/avoidance | 250.0, 250.1 | 1004.3 | 1004.2 | 17.26 | 768.7 | 10.67 |
| stress/turn/baseline | 249.3, 249.9 | 1087.6 | 1087.7 | 15.69 | 931.1 | 12.70 |
| stress/turn/turn | 248.0, 249.9 | 970.1 | 970.1 | 15.28 | 687.1 | 10.07 |
| stress/combined/baseline | 248.2, 250.1 | 1031.5 | 1031.4 | 15.18 | 820.0 | 12.00 |
| stress/combined/combined | 249.8, 250.0 | 1039.3 | 1039.0 | 16.23 | 836.0 | 11.50 |

UE 收发为连接层计数，飞行/单位字节为应用载荷，不能相加作为总出口；同一连接的服务器发送与客户端接收也不能相加。事件年龄和时钟估计误差见 connection-summary.csv 的独立列。DroppedShots 包括过期及其他拒绝原因，不能全部标为过期。

压力场景枪口出生为零的窗口仅用于 CPU/网络诊断，不用于证明完整客户端表现收益。采样窗口运行错误与初始化阶段错误分别保留。

## 原始证据

- `analysis.json`：原始门槛及全部 World/连接统计；`window-summary.csv`、`connection-summary.csv`：便于筛选比较。
- `Paired/<窗口>/frames.csv`、`session.utrace`：帧数据与原始 Trace。
- `native-capture.json`、`network-timeseries.json`：原生阶段计数与每秒网络时间序列。
- `result.json`、`counters-before/after.json`、`context-before/after.json`、`clock.json`、`runtime.log`：场景配置、前后状态、反馈、时钟与日志。
- `Sessions/`：临时控制的原值/恢复值及正式配置哈希；`Build/verification.json`、构建日志及 `QA/results.json`。
- `Attempt1/`：首版与无效早期采样，完整保留但不混入最终配对。

默认采用：[adoption.json](adoption.json)；实际场景核对：[scenario-audit.json](scenario-audit.json)；行为：[Behavior/summary.json](Behavior/summary.json)；恢复及退出：[cleanup-final.json](cleanup-final.json)。
