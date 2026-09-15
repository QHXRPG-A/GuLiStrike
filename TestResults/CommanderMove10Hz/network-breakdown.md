# 1,200 单位移动：下行数据占比实测

2026-09-15。一个服务端、两个真实渲染客户端，同机运行；沿用锁定镜头、在线逐批增兵后的 1,200 单位折返场景。按此前 `InBytesPerSecond` 的连接计数口径，每客户端平均 **187.43 KB/s**，其中 **89.72% 是单位姿态 RPC**。KB 采用十进制 1,000 字节。

## 结果

客户端各采 40 秒，服务端采 50 秒；本表只取三端共同运行的中间 **30.002525 秒**。按两条连接的字节总量求占比，再将速率除以两个客户端。没有把服务端双连接合计当作单人带宽。

| 内容 | 每客户端平均 KB/s | 下行占比 |
|---|---:|---:|
| 单位姿态：位置、速度、朝向、单位 ID、命令 ID、移动状态及姿态块/RPC 封装 | 168.166 | 89.72% |
| 协议封装与未细分开销 | 13.678 | 7.30% |
| 单位名册和状态增量 | 2.439 | 1.30% |
| 移动起点、终点和命令关联信息 | 1.984 | 1.06% |
| 建筑、据点、工程车及其他世界状态 | 1.123 | 0.60% |
| 其他公共姿态 RPC | 0.040 | 0.02% |
| **合计** | **187.429** | **100.00%** |

消息类型来自本次实际序列化事件。这里的“单位名册和状态”不是说血量在本轮持续变化；`ReplicatedSoldiers` 是包含这些字段的名册增量。本场景周期性改变移动命令，也会更新其中的命令关联状态。

“协议封装与未细分开销”可以进一步拆分。下列各项已包含在上表 7.30% 中，不能再与总量相加：

| 内容 | 每客户端平均 KB/s | 占全部下行 |
|---|---:|---:|
| 连接记账的 IP/UDP 开销：实测每包 28 字节 | 8.777 | 4.6830% |
| UE ACK、往返时间等包头信息 | 2.080 | 1.1096% |
| UE Bunch 头 | 1.384 | 0.7383% |
| UE 包序号 | 0.549 | 0.2927% |
| 其他容器、终止位、字节对齐及未归类余量 | 0.889 | 0.4741% |

最后一项没有强行分配给某个玩法功能，也没有把它称作已经测得的重传率。

## 实际消息明细

- `ClientReceiveSoldierPoseChunk`：每条连接在窗口内各 **11,635 次**，约 **387.801 次/秒**；平均约 **433.63 字节/次**，包含 RPC 自身封装。一次调用是一块单位姿态，不是整个世界的一帧。两客接受的完整姿态捕获帧均约 10Hz。
- `GuLiSoldierStateReplicator.ReplicatedSoldiers`：每连接 **36 次**，共 **73,030.5 字节**；`SnapshotRevision` 再占 144 字节。
- `MoveEndpoints`：每连接 **18 次**，分别 **59,565.75 / 59,490.75 字节**。采样器使用服务端合成命令所有者，但正式组件仍会向指挥官同步其阵营的活跃终点，因此这部分有实际流量。没有把它误写成真实鼠标上行 RPC 的成本。
- 其余属性包括矿车 `ReplicatedMovement`（每客约 461.74 B/s）、建筑状态/血量/目标句柄、据点进度与红蓝地面单位计数、世界时间、玩家资源私有状态与 Ping。
- `ClientReceivePublicPoseFrame`：每连接 300 次，共 1,200 字节，约 40 B/s。

完整 RPC、属性名、次数、位数和连接分项见[结构化统计](network-profile-n1200-c2/network-breakdown.json)。小数字节来自按位序列化的归属分摊，不表示网络发送半个字节。

## 计数口径与交叉核对

引擎 `UNetConnection::ReceivedRawPacket` 在 PacketHandler 处理后累计 `InTotalBytes`，`FlushNet` 的 `OutTotalBytes` 使用处理前的连接缓冲区大小；两者都加上 `PacketOverhead`。Network Profiler 的 `SocketSendTo` 记录则位于 PacketHandler 的发送处理之后，因此三种数字不能直接混称。

本次只加载默认 StatelessConnectHandler。逐包使用 profiler 记录的包序号、ACK、Bunch 位数，加上引擎写入的 1 个连接终止位，再逐包向上取整到字节并加实测 28 字节开销，重建与原连接计数相同的口径。其与两条连接累计发送字节的差异如下：

| 共同窗口口径 | 两条连接合计字节 |
|---|---:|
| 从网络事件重建连接记账 | 11,246,716 |
| 服务端累计 `OutTotalBytes` 差值 | 11,246,760 |
| 客户端累计 `InTotalBytes` 差值合计 | 11,241,579 |
| Socket 实际发送字节 + 28 字节/包 | 11,264,651 |

服务端累计计数比重建值多 **44 字节，0.000391%**；两端累计计数在每世界帧采样，边界未强行对齐到包事件。客户端收包合计比重建发送量低 **0.045676%**，两客窗口边界相对选取时点偏移在约 5.4ms 内；此差异不作为丢包率。

Socket 口径额外 **17,935 字节**来自 PacketHandler 处理与字节取整差异：连接 A 8,967 个包比连接记账多 1 字节、445 个包相同；连接 B 分别为 8,968 和 430 个包。其平均每客 **0.299 KB/s**。如果采用这个更外层的 Socket + IP/UDP 口径，总量为 **187.728 KB/s**，姿态占比为 **89.5793%**；这是初步分析中 89.58% 的来源。上方最终表使用与此前 188 KB/s 相同的连接口径，因此为 **89.72%**。

本轮完整客户端窗口中的逐帧 `InBytesPerSecond` 平均值是 188.413 / 188.298 KB/s；它是引擎按约一秒刷新的速率读数，且窗口为各自 40 秒。不能用该均值替代本表共同 30 秒窗口的累计字节差值。未将协议文件大小当作游戏流量，未将 RPC、属性、Bunch、Socket 的嵌套总量重复相加。

## 采集有效性

- 1,200 单位在服务器和两个客户端的真实移动人数 P05 均为 1,200；服务器 9.998748Hz，无丢步。两客姿态捕获帧接受频率为 9.998152 / 9.996925Hz。
- 两客锁定同一视点，CSV 的相机速度全程为 0；沿用 1080p/Epic、offscreen、客户端不限帧和服务端世界帧上限 60。
- 用户授权的是同场景网络组成测量。本次没有新增自动化测试用例、扩展单位档位、修改玩法复制协议或实施网络优化。原有 61 项自动化结果保留，本轮未重跑。
- 这是一轮持续移动场景的统计，包含场景原有的维护、占优采样和矿车等后台活动；不等同于全军交火、持续新兵入场、晚加入全量快照或真实玩家连续操作时的流量比例。

## 证据与复现

- [原始 Network Profiler 记录](network-profile-n1200-c2/server/network.nprof)：2,102,913 字节，v14，完整结束标记；SHA-256 为 `3b1fd88db6ca06aaec2331f4f96357267cf74f43a9cebd19efad6f70e768b88c`。
- [启动参数](network-profile-n1200-c2/launch.json)、[移动负载验证](network-profile-n1200-c2/analysis.json)、[服务端连接累计值](network-profile-n1200-c2/server/network.csv)、[客户端1](network-profile-n1200-c2/client1/network.csv)、[客户端2](network-profile-n1200-c2/client2/network.csv)。
- 采集开关位于既有 `GuLiCommanderMoveStress.cpp`，仅非 Shipping、显式参数启用。服务端在预热完成时开启 profiler，结束时关闭；不写入引擎代码，也不增加模块依赖。
- 源码引擎 `D:/UnrealEngine-5.7`；Win64 Development [Editor 最终构建](editor-build-network-profile-final.log)和[Game 构建](game-build-network-profile.log)退出 0；[BuildId 核对](build-id-network-profile.json)一致，为 `26d441ba-b96a-4e7b-b104-c2c6cd3e663c`。初次诊断编译的 const 调用错误已修正，[失败日志](editor-build-network-profile.log)保留。
- 解析格式参考本机 UE5.7 `Engine/Source/Programs/NetworkProfiler/NetworkProfiler/Tokens.cs` 和 `StreamParser.cs`。计数来源核对 `NetConnection.cpp`、`NetworkProfiler.cpp`、`IpConnection.cpp`；RPC Header/Parameter 的关系核对 `NetDriver.cpp`。所有百分比均从本次原始记录计算。

在项目根目录重跑时使用未存在的 Label：

```powershell
./Scripts/run_commander_move_stress.ps1 -Population 1200 -Clients 2 -Seconds 40 -Label reproduce-network -Port 17961 -JoinBeforePopulation -CaptureNetwork
python Scripts/analyze_commander_move_stress.py TestResults/CommanderMove10Hz/reproduce-network-n1200-c2
python Scripts/analyze_commander_network_profile.py TestResults/CommanderMove10Hz/reproduce-network-n1200-c2 --start 5 --seconds 30
```
