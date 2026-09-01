# ProjectTitan 进度文档体系

> 由 `gulistrike-progress` skill 驱动（`.agents/skills/gulistrike-progress/SKILL.md`）。本文件是体系入口与人读索引。

> 2026-08-30 已清理历史测试日志、检查结果、性能采样、自动化报告及运行截图；旧文档中指向这些生成附件的链接不再有效，历史归档正文保留。正式文档与 `TerrainGeneration/` 地形资料未删除。详见[清理记录](./Archive/20260830-清理Progress临时测试产物.md)。

## 四类文档

| 目录 | 职责 | 命名规则 |
|---|---|---|
| [RequirementDocument](./RequirementDocument/) | 需求点子（gameplay/UI/技术架构） | `YYYYMMDD-名称.md` |
| [DevelopmentDocumentation](./DevelopmentDocumentation/) | 技术方案与任务清单，与需求文档同名配对 | 同需求文档 |
| [Archive](./Archive/) | 每次开发的变更归档（只增不改） | `YYYYMMDD-解决了什么事.md` |
| [Gameplay](./Gameplay/) | 玩法模块总册（活文档） | `模块名.md` |

**流程**：点子 → 需求文档（A）→ 技术文档+任务清单（B）→ 实施（勾选任务）→ 归档（C）→ 玩法册更新（D）→ 本索引刷新。

## 文档索引

### 需求 → 开发（成对）

| 日期 | 名称 | 需求 | 开发 |
|---|---|---|---|
| 2026-09-01 | 移动命令自由扩散与静态寻路线 | [需求](./RequirementDocument/20260901-移动命令自由扩散与静态寻路线.md) | [开发](./DevelopmentDocumentation/20260901-移动命令自由扩散与静态寻路线.md) |
| 2026-09-01 | 指挥官相机、编队导航与移动射击优化 | [需求](./RequirementDocument/20260901-指挥官相机编队导航与移动射击优化.md) | [开发](./DevelopmentDocumentation/20260901-指挥官相机编队导航与移动射击优化.md) |
| 2026-09-01 | 小兵扫射与指挥官GAS桥接 | [需求](./RequirementDocument/20260901-小兵扫射与指挥官GAS桥接.md) | [开发](./DevelopmentDocumentation/20260901-小兵扫射与指挥官GAS桥接.md) |
| 2026-08-31 | 指挥官精确选兵与快捷提示栏 | [需求](./RequirementDocument/20260831-指挥官精确选兵与快捷提示栏.md) | [开发](./DevelopmentDocumentation/20260831-指挥官精确选兵与快捷提示栏.md) |
| 2026-08-31 | 小兵客户端先行移动拖拽诊断 | [需求](./RequirementDocument/20260831-小兵客户端先行移动拖拽诊断.md) | [开发](./DevelopmentDocumentation/20260831-小兵客户端先行移动拖拽诊断.md) |
| 2026-08-31 | 基地建造玩法探索 v0.1 | [草案](./RequirementDocument/20260831-基地建造玩法探索.md) | —（仅玩法探索，未进入开发） |
| 2026-08-31 | 公共战局框架与三类角色接入 | [需求](./RequirementDocument/20260831-公共战局框架与三类角色接入.md) | [开发](./DevelopmentDocumentation/20260831-公共战局框架与三类角色接入.md) |
| 2026-08-30 | 指挥官脚环优化与兵种面板 | [需求](./RequirementDocument/20260830-指挥官脚环优化与兵种面板.md) | [开发](./DevelopmentDocumentation/20260830-指挥官脚环优化与兵种面板.md) |
| 2026-08-29 | 指挥官 HUD 逻辑接入与批量小兵血条 | [需求](./RequirementDocument/20260829-指挥官HUD逻辑接入与批量小兵血条.md) | [开发](./DevelopmentDocumentation/20260829-指挥官HUD逻辑接入与批量小兵血条.md) |
| 2026-08-29 | 指挥官 UI 与小兵血条视觉设计（纯表现） | [需求](./RequirementDocument/20260829-指挥官UI与小兵血条视觉设计.md) | [开发](./DevelopmentDocumentation/20260829-指挥官UI与小兵血条视觉设计.md) |
| 2026-08-29 | 指挥官小兵表现层两阶段性能优化 | [需求](./RequirementDocument/20260829-指挥官小兵表现层两阶段性能优化.md) | [开发](./DevelopmentDocumentation/20260829-指挥官小兵表现层两阶段性能优化.md) |
| 2026-08-28 | 指挥官 3C、Soldier 数据化与运行时 GM 调参 | [需求](./RequirementDocument/20260828-指挥官3C与运行时GM调参.md) | [开发](./DevelopmentDocumentation/20260828-指挥官3C与运行时GM调参.md) |
| 2026-08-27 | Mass 双端同步架构（动态 25 人控制粒度） | [需求](./RequirementDocument/20260827-Mass双端同步架构草案.md) | [开发](./DevelopmentDocumentation/20260827-Mass双端同步架构草案.md) |
| 2026-08-27 | 飞船与场景模型尺寸归一 | [需求](./RequirementDocument/20260827-飞船与场景模型尺寸归一.md) | [开发](./DevelopmentDocumentation/20260827-飞船与场景模型尺寸归一.md) |
| 2026-08-26 | 5v5 大战场玩法草案 | [需求](./RequirementDocument/20260826-5v5大战场玩法草案.md) | —（策划草案阶段） |
| 2026-08-24 | Mass 框架启用与源码导读（大规模部队技术预研） | —（对话演进出的技术方向，未立需求文档） | [开发](./DevelopmentDocumentation/20260824-Mass框架启用与源码导读.md) |
| 2026-08-21 | 数据管线：Excel 配置飞船数值 | [需求](./RequirementDocument/20260821-数据管线Excel配置.md) | [开发](./DevelopmentDocumentation/20260821-数据管线Excel配置.md) |
| 2026-08-20 | DIY 飞船 | [需求](./RequirementDocument/20260820-DIY飞船.md) | [开发](./DevelopmentDocumentation/20260820-DIY飞船.md) |
| 2026-08-16 | 示例-商店系统 | [需求](./RequirementDocument/20260816-示例-商店系统需求.md) | [开发](./DevelopmentDocumentation/20260816-示例-商店系统需求.md) |

### GAS 学习笔记

| 日期 | 文档 |
|---|---|
| 2026-09-01 | [GAS 基础：ASC、Ability、Tag、Event 与当前指挥官军队技能桥接](./DevelopmentDocumentation/相关读物/GAS/README.md) |

### UE 网络教材

| 日期 | 文档 |
|---|---|
| 2026-08-31 | [UE 网络基础到当前项目实现：公共 Battle、三角色网络链路、10 章源码与练习](./DevelopmentDocumentation/相关读物/UE网络教材/README.md) |
| 2026-08-31 | [理论篇（新增）：网络同步概念理解，Jerish 原作按 UE 5.7 引擎源码改编，附差异速查表](./DevelopmentDocumentation/相关读物/UE网络教材/00-理论篇-网络同步概念理解.md) |

### Mass 精读笔记

| 日期 | 文档 |
|---|---|
| 2026-08-31 | [阅读目录：元素 → Handle → Archetype → Query → Authority，与 UE 网络教材衔接](./DevelopmentDocumentation/相关读物/Mass精读笔记/README.md) |
| 2026-08-31 | [GuLiBattleAuthoritySubsystem：士兵模块启停、选兵与移动、30 Hz 模拟、调参与发布组件协作](./DevelopmentDocumentation/相关读物/Mass精读笔记/GuLiBattleAuthoritySubsystem.md) |

### 归档（倒序）

| 日期 | 事项 |
|---|---|
| 2026-09-01 | [GAS 基础教材与项目桥接导读：从 ASC、Ability、Tag、Event 等基础概念映射到指挥官 PlayerState、团队技能桥接与 Mass 扫射，并修复相关读物目录移动后的主要索引链接；本轮仅静态文档核对](./Archive/20260901-GAS基础教材与项目桥接导读.md) |
| 2026-09-01 | [移动命令自由扩散、单兵部分接受与静态寻路线：六角候选、分帧规划、v6掩码、OwnerOnly终点和无动画1px绿线已实现；Editor/Game构建、全量99项、网络19/19、导航24/24及默认地图20兵无头命令链通过，交互PIE、双客户端和性能待验证](./Archive/20260901-移动命令自由扩散与静态寻路线实现与验证.md) |
| 2026-09-01 | [指挥官相机、编队导航与移动射击：真实Landscape边界、固定5×5终点、每步NavMesh约束、恢复/Blocked与移动射击已实现；Game、DebugGame/Development Editor构建成功，DebugGame全量89/89与Development导航20/20通过；交互PIE/双客户端/性能待验证，Launcher不支持Server target](./Archive/20260901-指挥官相机编队导航与移动射击优化实现与验证.md) |
| 2026-09-01 | [小兵扫射与指挥官GAS桥接：两兵种表驱动、来源原子叠加/替换、float生命与v5、服务器GM；79项自动化和专服双客户端通过，500交战及万人驻停采样，万人完整性能未判定达标](./Archive/20260901-小兵扫射与指挥官GAS桥接实现与验证.md) |
| 2026-08-31 | [选兵验收计数核对：61项通过=60项Commander+1项RuntimeTuning；更正分组口径，无代码变更](./Archive/20260831-选兵验收测试分组计数核对.md) |
| 2026-08-31 | [精确选兵与快捷提示栏及移动诊断：点/框/范围、Shift/Alt、32px指针、v4依赖移动；61项自动化及真实输入通过，移动拖拽已归因但默认算法不改](./Archive/20260831-指挥官精确选兵与快捷提示栏及移动诊断.md) |
| 2026-08-31 | [基地建造玩法探索草案落档：五项已确认方向、四类候选建筑、工程施工与据点攻防建议；仅文档，未开发或试玩](./Archive/20260831-基地建造玩法探索草案落档.md) |
| 2026-08-31 | [网络教材理论篇改编：Jerish UE4 概念文按 5.7 源码逐条核对改编，多播相关性/Spawn拦截/InitialOnly 等 9 处引擎现状收敛成注](./Archive/20260831-网络教材理论篇改编.md) |
| 2026-08-31 | [网络教材与 Mass 精读笔记同步修订：核对 16 篇现有文档、新增阅读目录；仅静态核对，硬跳变仍未通过](./Archive/20260831-网络教材与Mass精读笔记同步修订.md) |
| 2026-08-31 | [公共战局框架与三类角色接入：共享身份与复活、指挥官发布拆分、Ground 占位、飞船预测/换装/射击；50 项测试通过，弱网硬跳变验收未通过](./Archive/20260831-公共战局框架与三类角色接入.md) |
| 2026-08-31 | [UE 网络中文注释与项目教材：33 个源码文件、10 章真实源码讲解、静态词法一致性核对](./Archive/20260831-UE网络中文注释与项目教材.md) |
| 2026-08-31 | [指挥官脚环优化与兵种面板：单面中空环、仅存活者血量与一张兵种卡、中文斜切标题；49 项自动化与 10 项 Slate 输入通过，18 份 CSV 和尾部风险留证](./Archive/20260831-指挥官脚环优化与兵种面板.md) |
| 2026-08-31 | [战斗权威子系统头文件中文注释：补齐接口语义、生命周期、配置单位与调参提交边界，声明和默认值保持不变](./Archive/20260831-战斗权威子系统头文件中文注释.md) |
| 2026-08-30 | [战斗权威子系统中文注释与源码导读：整理 57 处赋值后换行，核对代码词法单元一致，保留既有逻辑](./Archive/20260830-战斗权威子系统中文注释与导读.md) |
| 2026-08-30 | [清理 Progress 临时测试产物：删除 102 个文件与 7 个顶层临时目录，保留正式文档和地形资料](./Archive/20260830-清理Progress临时测试产物.md) |
| 2026-08-29 | **[指挥官 UI 与 HUD 逻辑接入总归档：三轮纯视觉设计（三岛 HUD、六命令图标、四状态血条、纯黑底板、选取提示）→ 原生逻辑接入（Select/Move 输入、协议 v3 Health/MaxHealth、单 ISM 世界血条，44/44 测试与 500 兵性能门禁）→ EyeAdaptationInverse 鲜红修复（实测约 #FF001A）；合并同日五份归档](./Archive/20260829-指挥官UI与HUD逻辑接入总归档.md)** |
| 2026-08-29 | **[指挥官小兵表现层两阶段性能优化：Unit 1000m 硬切、Ring 常显、本地六键表现调参，以及单材质/单 Section/三 LOD Crowd 网格；三轮客户端最差帧 p95 9.1613ms](./Archive/20260829-指挥官小兵表现层两阶段性能优化.md)** |
| 2026-08-28 | **[收集 50 张 RTS 游戏 UI 参考截图存入 D:\UE5.7\UI\RTS UI：Steam 商店 API 全量图库+HUD 联络表批量审核，覆盖 20 个游戏来源，附来源清单](./Archive/20260828-收集50张RTS-UI参考截图.md)** |
| 2026-08-28 | **[Commander 指令术语统一：Request=请求、Command=网络命令、Order=单位指令/移动指令、MoveTarget=移动目标；开发文档与玩法总册完成整改](./Archive/20260828-Commander指令术语统一.md)** |
| 2026-08-28 | **[指挥官 3C、Soldier 数据化与运行时 GM 调参：36m/s 表驱动 Soldier、远距 Landscape 点选、共享目标松散到达、heading-up 小地图、7-key World GM Registry 与 Ship 原子复制](./Archive/20260828-指挥官3C与运行时GM调参.md)** |
| 2026-08-27 | **[Mass 动态 25 人控制组与双端平滑同步：500 名独立士兵、服务端动态编组与权威移动、10Hz 精确姿态帧、客户端 Hermite 插值/短时预测、共享 NavMesh 编队移动及指挥官 UI](./Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md)** |
| 2026-08-27 | **[飞船与场景模型尺寸归一：Dreadnought/无人机/机器人缩放烘焙进资源，2/1/128 实例归一为 Scale 1；当前飞船子根取消 0.3、相机按 5/3 更新并修正 PlayerStart 出生高度](./Archive/20260827-飞船与场景模型尺寸归一.md)** |
| 2026-08-26 | **[动画资产回退：walkcycle 四件套/演示 Actor/Blender Action 全删，用户改为自己在 UE 内制作动画；管线教程保留作参考](./Archive/20260826-动画资产回退-用户改为UE内自制.md)** |
| 2026-08-26 | **[WM01 程序化六足行走动画管线：Blender 脚本三角步态 K 帧（48 帧闭环）→ 后台 GLB 导出 → UE AnimSequence 入库并播放验证，附完整教程](./Archive/20260826-WM01程序化六足行走动画管线.md)**（教程：[DevelopmentDocumentation](./DevelopmentDocumentation/20260826-WM01程序化六足行走动画-Blender到UE管线教程.md)） |
| 2026-08-25 | **[相机避障函数化：Tick 避障段提取为 ResolveCameraArmCollision()，纯搬移零逻辑变更，PIE 门控直通 15000 基线无回归](./Archive/20260825-相机避障函数化-Tick去散落逻辑.md)** |
| 2026-08-25 | **[相机参数进表：新增 Camera sheet（8 列，含新提列 CameraDefaultArmLength），Pitch 自 Tuning 迁入，ApplyCameraRow 表驱动，Fly01 BP 覆盖值清理；PIE 实测表值逐项生效](./Archive/20260825-相机参数进表Camera-sheet与BP覆盖清理.md)** |
| 2026-08-24~25 | **[飞船 3C 与相机避障总归档：滚轮缩放（8000~50000 默认 15000）+ 舰体避障六轮演进（舰心外扫→舰外回扫→端点重叠→凸包资产化→由外向内→间隙 20m）+ 命中过滤（只认舰/地形）与两段式扫掠 + 单写者纪律；含引擎扫掠语义/Live Coding/工具链踩坑实录](./Archive/20260825-飞船3C与相机避障总归档-0824至0825.md)** |
| 2026-08-24 | **[CombatAvatarFly 归位勘误：重巡舰体从 Blender 导出入库（SM_Maelstrom_Hull），无畏舰/重巡全套分驻 CombatAvatarFly-01/02，主控舰体换为无畏舰裸舰体](./Archive/20260824-CombatAvatarFly归位勘误-重巡舰体入库与两舰归位.md)**（勘误早前误把 fly-01/02 当目标舰的记录，已并入上篇总归档） |
| 2026-08-23~24 | **[第一批飞船组件拆分入库（总归档）：A/B 两舰 14 件武器组件 + 舰体入库 ShipComponent，固化 Blender→UE 拆件流水线](./Archive/20260824-第一批飞船组件拆分入库-总归档.md)** |
| 2026-08-22~24 | **[第二轮资产整合（总归档）：VFX/阵营舰/GroundFire/Scifi_Skies 入库 /Game/Assets，222 stub 清理与迁移后遗症修复，含包迁移方法论](./Archive/20260824-第二轮资产整合-总归档.md)** |
| 2026-08-22 | [修复 LVL_Main 按 Play 无飞船可控制（加 GameMode 覆盖 + 删手摆无人船）](./Archive/20260822-修复LVL_Main按Play无飞船可控制.md) |
| 2026-08-22 | [数据管线 v2：Excel 三行元数据驱动，自动生成 C++ 行结构（删 tables.json 与手写 ShipData.h）](./Archive/20260822-数据管线v2-Excel元数据驱动自动生成行结构.md) |
| 2026-08-21~22 | [数据管线开发总归档：Excel→JSON→DataTable（MVP → 通用化 → 校验配置化）](./Archive/20260822-数据管线开发总归档-0821至0822.md) |
| 2026-08-20~21 | [DIY 飞船开发总归档（MVP → 手感调校 → 架构演进）](./Archive/20260821-DIY飞船开发总归档-0820至0821.md) |
| 2026-08-16 | [资产整合-Marketplace 资产包统一归档至 Assets](./Archive/20260816-资产整合-Marketplace资产包统一归档至Assets.md) |
| 2026-08-16 | [搭建进度文档体系](./Archive/20260816-示例-搭建进度文档体系.md) |

### 玩法模块

| 模块 | 文档 |
|---|---|
| 指挥官 | [指挥官](./Gameplay/指挥官.md) |
| 飞船 | [飞船](./Gameplay/飞船.md) |
| 战斗 | [战斗](./Gameplay/战斗.md) |
