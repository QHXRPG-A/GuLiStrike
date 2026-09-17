---
schema: guli-progress/v1
id: ARC-20260911-012
work_id: ''
kind: archive
role: root
title: 资源经济、动态障碍与 Commander 适配层解耦
areas:
- architecture
- economy
- commander
- navigation
categories:
- gameplay
status: recorded
verification: passed
created: '2026-09-11'
updated: '2026-09-11'
summary: 将团队经济账本、动态障碍发布和 Commander 资源交互从资源世界拆成三个通用边界，既有玩法与测试用例保持不变，范围自动化、双客户端 PIE 及三类 Target 均通过。
next_action: ''
relations:
  work_items:
  - WORK-20260911-003
status_note: 范围验证通过。额外执行完整 Building 套件时，BuildingAssetTests 因当前 Outpost 二进制资产 BuildScale/尺寸偏离既有断言而失败；该资产问题与本次源码解耦无关，未修改测试或资产。
---

# 2026-09-11：完成资源经济、障碍与 Commander 接线解耦

## 变更清单

| 文件/资产 | 变更 |
|---|---|
| `Source/GuLiStrike/Gameplay/Economy/GuLiEconomyTypes.*` | 将资源种类与余额值对象下沉到通用经济层 |
| `Source/GuLiStrike/Gameplay/Economy/GuLiTeamEconomySubsystem.*` | 新增 `IGuLiTeamEconomy` 与服务器权威 WorldSubsystem，实现余额、入账、RequestId 幂等预留、提交和退款 |
| `Source/GuLiStrike/Gameplay/Navigation/GuLiDynamicObstacleRegistry.*` | 新增 `IGuLiDynamicObstacleRegistry`、稳定 Handle、Revision 通知及 WorldSubsystem 注册表 |
| `Source/GuLiStrike/Commander/Framework/GuLiCommanderResourceAdapter.*` | 承接矿车 Actor 选择、射线命中、人工命令、选择中心、Actor 网络 Owner、OwnerOnly 经济镜像与断线回 Auto |
| `GuLiResourceWorldSubsystem.*`、`GuLiResourceActors.cpp` | 移除经济账本、资源专用 Mass 代理与 Commander 语义；初始化通用服务并发布障碍/资源产出 |
| `GuLiBattleAuthoritySubsystem.*` | 只订阅/读取通用障碍注册表，并通过 Commander Adapter 路由矿车选择与命令 |
| `GuLiBuildingPlacementComponent.cpp`、`GuLiBuildingTypes.*` | 建筑拥有价格策略，通过通用经济接口预留、提交或退款，不再包含 ResourceWorld |
| `GuLiResourceBuildingCompatibility.cpp` | 隔离保留既有 `GetBlueBuildingCost(EGuLiBuildingType)` C++ 测试合同；生产建造路径不经过该接口 |

## 决策与实现

- 保持全部反射类型在 `GuLiStrike` 模块，避免改变 `/Script/GuLiStrike` 资产路径；解耦依靠同模块内 Public 窄接口与单向依赖，不为架构形式迁移 DataAsset。
- 经济服务由资源战局显式 `BeginMatch`，导航 Ready 后 `OpenTransactions`。工厂只执行 `Credit`，建筑只提交自身价格与稳定 PlayerGuid/RequestId；无静默余额回退。
- 障碍注册表保证输入与 Handle 合法，矿簇耗尽时注销一次并广播 Revision。Mass 收到通知只失效手工避障缓存；Recast 的局部重建仍由 NavModifier 生命周期触发。
- Commander Resource Adapter 位于 Commander 层并单向依赖资源域。资源核心不再包含 Commander 类型实现或 Mass Authority，也不再查找 Commander、设置玩家网络 Owner 或同步 PlayerState 私有经济。
- 对内部组合错误使用 `check/checkf` 暴露不变量；保留的条件返回只用于网络请求合法性、余额不足、无命中、非资源地图和 World teardown 等真实业务/生命周期分支，没有新增吞错式空指针兜底。
- 各 WorldSubsystem 在 teardown 只清理自身状态，不跨已进入析构的经济/障碍服务重复执行 `EndMatch` 或逐项注销；正常运行期的矿簇耗尽仍要求有效 Handle 且恰好注销一次。
- World teardown 时不会把即将销毁的矿车切回自动；正常 Commander Logout 仍要求 Adapter 存在并立即回 Auto。

## 验证

- 源码版 `D:\UnrealEngine-5.7`：`GuLiStrikeEditor`、`GuLiStrike`、`GuLiStrikeServer` 的 Win64 Development 构建均退出码 0。
- Editor BuildId：引擎与项目均为 `ca32e40e-6f35-4046-b75e-aeb3f6da9927`。
- [最终范围回归](../../TestResults/ResourceDecoupling/FinalAll/index.json)：资源、Commander、Navigation、Building 共 27/27 成功。
- [双客户端 PIE](../../TestResults/WORK-20260911-003/ResourcePIE/dual-client.json)：16/16 成功；39 次采集后障碍保留，第 40 次后注销并开放，Mass 仍为 500，FIFO 入账、OwnerOnly、命令覆盖与 3 秒回 Auto 均通过，最终进程退出码 0。
- [额外 Building 套件](../../TestResults/ResourceDecoupling/Building/index.json)：策略与 World 2 项成功；资产项 1 项失败，原因为当前 Outpost Source LOD BuildScale 为 1 而既有断言要求 20，且最终尺寸也不匹配。本次不改二进制资产。
- 五个既有测试源文件 SHA-256 与改造前一致：Commander Network `D2C760DA...A00B2`、Building Asset `DBA9C58E...BE7E7`、Building Placement `4B98633E...1DCCA`、Building World `6DCA5B5B...4D9B6`、Resource `DEFA3D1B...6AF2`。
- 静态依赖检查确认：Building→ResourceWorld、Mass→ResourceWorld、ResourceWorld→Mass/Commander 直接符号均已清零；旧的资源专用预留/障碍 API 已移除。

## 遗留问题

- `UGuLiResourceEconomyConfig::GetBlueBuildingCost` 只为“不修改既有测试用例”保留在隔离兼容编译单元；后续若允许迁移旧测试合同，可删除该薄层。
- Outpost 资产 BuildScale/尺寸与 BuildingAssetTests 的既有合同不一致，属于独立资产修复项。
