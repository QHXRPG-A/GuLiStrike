# v9之后的旧姿态协议清理

本轮清理正式源码中v8姿态传输的残留，线上的v9字节格式与量化精度保持。旧代码只作为历史基线保存在证据目录和Git历史，不参与正式模块构建。

## 变更

- `FGuLiCompressedSoldierPose`改为`FGuLiQuantizedSoldierPose`，与内部`FGuLiSoldierPoseChunk`一起改为普通C++结构。仅编码字节块和ACK保留网络反射序列化；内部块不再保存重复的协议版本。
- 删除旧姿态`Sanitize`中的静默改状态、裁剪、去重和版本重写。编码器检查内部契约，解码器验证网络边界，失败直接拒绝完整块。
- 删除已失效的空间锚点和重复相对位置字段；硬校正诊断保存当前/上一条世界坐标。表现层使用v9已验证时间戳，移除旧时间兜底和重复协议检查。
- 删除测试旧姿态直通入口；既有战局/版本门禁用例现在生成v9编码块，经与正式RPC相同的解码/入队函数验证。旧v8拒绝测试修改真实字节头，未新增测试用例。

## 验证

- 源码引擎`D:/UnrealEngine-5.7`，`GuLiStrikeEditor Win64 Development`和`GuLiStrike Win64 Development`均退出0：[Editor](editor-build.log)、[Game](game-build.log)。
- [BuildId](build-id.json)四份一致：`26d441ba-b96a-4e7b-b104-c2c6cd3e663c`，包括引擎、项目及GuLiMapAuthoring/GuLiFlightNavigation插件。
- [既有回归](automation/index.json)65项成功，0失败、0未运行（网络23、导航34、战斗8）。
- [源码检查](source-audit.json)：正式Commander源码中已列出的旧类型、旧入口、锚点和旧清理函数引用为0。[独立补丁](cleanup.patch)与[清理前](before-source-manifest.json)/[清理后](after-source-manifest.json)SHA清单可核对。
- 沿用既有1200单位、一NullRHI服务端、两真实渲染客户端场景，客户端40秒/服务器50秒，取QPC对齐共同30秒；[结果](integration-summary.json)通过。服务器300步即10Hz、0丢步，客户端0解码失败。

| 进程 | 存活数 | 移动人数均值 | 移动人数P05 / 最低 | 单兵有效姿态Hz |
|---|---:|---:|---:|---:|
| 服务端 | 1200 | 1199.950 | 1199 / 1199 | 不适用 |
| 客户端1 | 1200 | 1199.932 | 1199 / 1198 | 7.501 |
| 客户端2 | 1200 | 1199.962 | 1200 / 1198 | 7.498 |

```powershell
& ./Scripts/run_commander_move_stress.ps1 -Population 1200 -Clients 2 -Seconds 40 `
  -Label integration -EvidenceRoot TestResults/CommanderPoseCompression/protocol-cleanup `
  -JoinBeforePopulation -Port 18091
```

目录已存在时换Label/端口，完整参数见[launch.json](integration-n1200-c2/launch.json)。本轮用于确认清理后正式链路；未重做前后三轮CPU/内存/带宽对比，上一阶段的[性能报告](../REPORT.md)仍对应其当时源码快照。没有将本轮未开截帧/NetworkProfiler的结果混入该对比。

需求、开发和Gameplay已同步；索引build及全量check结果见[progress-final-check.json](progress-final-check.json)。
