# 归档时间线

> 自动生成，请勿手改。运行 `progress_docs.py build` 刷新。

| 日期 | 归档 | 模块 | 验证 | 摘要 |
|---|---|---|---|---|
| 2026-11-12 | [Mass移动与建筑交矿会话结项](../Archive/20261112-Mass移动与建筑交矿会话结项.md) | navigation, performance, building, resources | partial | 按用户指令完成本会话文档结项，清理587个临时文件，保留有效采样和资源，并发布代码、配置、脚本、源表与文档。 |
| 2026-11-12 | [护盾外围交矿与先驱号兵营交付](../Archive/20261112-护盾交矿与先驱号兵营.md) | building, resources, navigation | partial | 护盾发生器负责外围交矿与加工，先驱号兵营保留正常圆形阻挡并支持自产单位从内部出营；源码构建和地图入口已交付。 |
| 2026-11-11 | [统一体型让路与接触传递](../Archive/20261111-统一体型让路与接触传递.md) | commander, performance | partial | 按玩家要求删除让路大小判断，补齐双接触候选，v9定点复测245脱困、小单位可请求大单位让路。 |
| 2026-11-11 | [人墙让路来源修复](../Archive/20261111-人墙让路来源修复.md) | commander, network | partial | 记录两台先驱号被困现场、让路原始命令丢失的修复，以及同位置人群复测和地图交付。 |
| 2026-11-11 | [矿簇旁两点往返修复交付](../Archive/20261111-矿簇旁两点往返修复.md) | commander, performance | partial | 记录玩家PIE故障现场、v6连续选侧和体型拐点范围修复，以及源码构建、运行检查和地图入口。 |
| 2026-11-11 | [Mass权威数据迁移与千单位对比](../Archive/20261111-Mass权威数据迁移与千单位对比.md) | commander, performance | partial | Mass 直接持有权威单位记录，保留原求解和顺序；两版各完成 12 个窗口，500 单位人墙耗时下降超出本轮重复波动。 |
| 2026-11-11 | [Mass移动数据去重与批量发布](../Archive/20261111-Mass移动数据去重与批量发布.md) | commander, performance | partial | 完成移动数据副本清理、缓冲复用及Mass按Chunk写回，两版同场景各24个窗口已采集；2类成本下降超出重复极差，侧绕与让路函数未改。 |
| 2026-11-11 | [圆形避障结项清理与代码同步](../Archive/20261111-圆形避障结项清理与代码同步.md) | commander, navigation, performance | partial | 已按用户指令结项，清除旧避障残留和1683个临时文件约2.60GiB，构建重载通过，232个非资源文件已推送GitHub main并核对。 |
| 2026-11-11 | [受阻让路传递实装与性能对比](../Archive/20261111-受阻让路传递实装与性能对比.md) | commander, navigation, performance | partial | Demo受阻让路和邻居传递实装为0.5秒，两版48窗口完成；对穿成本少9.3%、到达42→48，挡路17→20，仍有往返。 |
| 2026-11-11 | [Demo受阻让路与邻居传递](../Archive/20261111-Demo受阻让路与邻居传递.md) | commander, navigation, performance | partial | Web受阻侧绕即可请求让路，让路者向直接阻挡者逐步传递相同方向，默认4大100小供玩家体验。 |
| 2026-11-11 | [Demo大小群体数量设置](../Archive/20261111-Demo大小群体数量设置.md) | commander, navigation, performance | partial | Web默认展示4个大单位穿过32个小单位，数量可分别设置，保留既有侧绕和让路规则。 |
| 2026-11-11 | [Demo大小让路对照展示](../Archive/20261111-Demo大小让路对照展示.md) | commander, navigation, performance | partial | Web增加默认四单位上下排大小对照，独立命令组与让路次数显示，供用户直接体验大小规则。 |
| 2026-11-11 | [简易让路编译加载交付](../Archive/20261111-简易让路编译加载交付.md) | commander, navigation, performance | partial | 用户批准后完成简易让路源码版Editor构建、重载和参数导入，四份清单回读通过，原型图可供玩家体验。 |
| 2026-11-11 | [简易让路源码与场景接入](../Archive/20261111-简易让路源码与场景接入.md) | commander, navigation, performance | partial | 将Web简易让路和同命令排除接入UE正式源码与Excel管线，原型图入口已保存；本次编译重载待用户确认。 |
| 2026-11-11 | [Demo同命令取消互让](../Archive/20261111-Demo同命令取消互让.md) | commander, navigation, performance | partial | Web让路按命令ID排除同一批下令成员，原侧绕与碰撞保持。 |
| 2026-11-11 | [原主动绕侧性能对比与让路Demo](../Archive/20261111-原主动绕侧性能对比与让路Demo.md) | commander, navigation, performance | partial | 当前原主动绕侧完成24个同输入性能窗口，6个移动场景成本与到达改善；工作到达下降及往返保留，另交付仅Web的简易让路。 |
| 2026-11-11 | [原主动绕侧移植与计算优化](../Archive/20261111-原主动绕侧移植与计算优化.md) | commander, navigation, performance | partial | 按用户指令用Demo原主动绕侧替换限次分支，保留左右多角度比较与滑动；计算复用、Excel迁移和源码构建接入完成。 |
| 2026-11-11 | [指挥官落点参数编号修复](../Archive/20261111-指挥官落点参数编号修复.md) | commander, navigation | partial | 修复命令局部落点编号被当作真实单位ID查询圆形参数、导致普通移动被拒绝；同步补齐移动层，源码编译重载完成。 |
| 2026-11-11 | [圆形避障精简与玩家入口交付](../Archive/20261111-圆形避障精简与玩家入口交付.md) | commander, navigation, performance | partial | 圆形数据与正式移动入口完成；用户否定防御性试版后按Demo限次分支精简，编译并保存玩家入口，历史性能分版本保留。 |
| 2026-11-09 | [肉鸽牌关系图工具交付](../Archive/20261109-肉鸽牌关系图工具交付.md) | combat, ui | partial | 三个席位的肉鸽卡牌可在本地画布编辑并落盘，技术交付覆盖真实浏览器操作和草稿恢复。 |
| 2026-11-09 | [开局网络优化会话完成清理与项目同步](../Archive/20261109-开局网络优化会话完成清理与项目同步.md) | network, performance | partial | 按用户指令结项开局网络优化，清理104个临时文件约292MB，保留带宽验收证据并同步项目非资源变更。 |
| 2026-11-09 | [开局交火网络 1 MB/s 目标验收](../Archive/20261109-开局交火网络1MB每秒验收.md) | network, performance | passed | 协议 44 在原 600 单位场景三间独立房间达到收发合计 869.44–894.31 KB/s，加权下降 68.48%，最高完整约一秒区间 921.36 KB/s。 |
| 2026-11-09 | [游骑兵战斗会话完成清理与项目同步](../Archive/20261109-游骑兵战斗会话完成清理与项目同步.md) | combat, network, ui, vfx | partial | 按用户要求将初始弹/左右Loop与爆炸强化两项开发结项，清理671个临时文件，保留逐发证据并同步项目非资源变更。 |
| 2026-11-09 | [爆炸范围266cm与强化爆炸弹三级接入](../Archive/20261109-爆炸范围对齐与强化爆炸弹交付.md) | combat, network, ui, vfx | partial | 数值标定基础半径266cm并保持火球尺寸，新增12.01三级强化；两次PIE完成伤害、范围、进度和去重对账，原地图与正式Manifest已保存。 |
| 2026-11-09 | [游骑兵左右Loop与公共战斗链路交付](../Archive/20261109-游骑兵左右Loop与公共战斗链路交付.md) | combat, network, ui, vfx | partial | 真实左右Loop通知接入Mass公共链路；修复碰撞中心、长局出生容量及登记遍历，最新构建三次正常PIE进出，六档普攻和独立爆炸弹取得数值证据。 |
| 2026-11-08 | [游骑兵PIE进入与开火实测](../Archive/20261108-游骑兵PIE进入与开火实测.md) | combat, network | passed | 清除双编辑器地图文件占用，正常PIE进入Ground；左右各4发完成真实Fire通知及每发6伤害回执，停火与正常退出通过。 |
| 2026-11-08 | [游骑兵PIE动画待发时序修复](../Archive/20261108-游骑兵PIE动画待发时序修复.md) | combat, network | partial | 用户PIE先因泛化Fire错误暂停、再开局超时；改为正常动画更新触发待发ShotId，编译和保存回读通过，实际修复待复验。 |
| 2026-11-08 | [游骑兵动画触发与独立爆炸弹修复](../Archive/20261108-游骑兵动画触发与独立爆炸弹修复.md) | combat, network, ui, vfx | partial | 按用户纠正改用真实Fire动画通知出弹，修复模型受击中心碰撞，完成独立偏移减速爆炸弹、数据、资产和验证区接入；待玩家验收。 |
| 2026-11-08 | [游骑兵验证场景PIE启动修复](../Archive/20261108-游骑兵验证场景PIE启动修复.md) | combat, network, vfx | partial | 修复新增验证对象后遗漏的资源源哈希认证和导航烘焙，保存重载通过；勘误此前入口就绪结论，待玩家重试PIE。 |
| 2026-11-08 | [游骑兵初始弹与肉鸽卡技术交付](../Archive/20261108-游骑兵初始弹与肉鸽卡技术交付.md) | combat, network, ui, vfx | partial | 完成游骑兵初始弹、攻速/强化/爆破卡及前置关系的原生和数据接入；获准编译、资源重载、正式Manifest与原地图实体回读通过，实际玩法及视觉待玩家验收。 |
| 2026-11-08 | [游骑兵会话清理与代码交付](../Archive/20261108-游骑兵会话清理与代码交付.md) | movement, presentation | partial | 按用户要求清理游骑兵 3C 与动画会话的临时文件，将相关工作标记完成并发布源码与数据变更；资源留在本地。 |
| 2026-11-07 | [游骑兵定向闪现编译与正式接入](../Archive/20261107-游骑兵定向闪现编译与正式接入.md) | movement, presentation | partial | 完成源码版编译加载、Gait 导入、四向闪现动画接线和清单刷新，保存扩展机动区及有效导航缓存。 |
| 2026-11-07 | [游骑兵四向闪现动画与机动区阶段交付](../Archive/20261107-游骑兵四向闪现动画与机动区.md) | movement, presentation | partial | 保存三份惯性拖拽动画、方向混合空间和扩展机动区，保留前闪手调数据；新控制源码尚待获准编译加载。 |
| 2026-11-07 | [游骑兵方向闪现重制准备](../Archive/20261107-游骑兵方向闪现重制准备.md) | presentation | partial | 按用户要求清除三份方向闪现的上肢基础轨道与两条腿部附加曲线，保留腿部基础动作和技能阶段标记。 |
| 2026-11-07 | [游骑兵闪现枪口分离修复](../Archive/20261107-游骑兵闪现枪口分离修复.md) | presentation | partial | 修复闪现初稿把枪管自转骨骼当作独立俯仰关节的问题，保留玩家已手调的腿部曲线。 |
| 2026-11-06 | [游骑兵世界UI分档与敌方变化提示交付](../Archive/20261106-游骑兵世界UI分档与敌方变化提示交付.md) | ui, combat, performance | partial | 游骑兵350/500米UI分档、敌方生命护盾变化提示和全部单位底圈隐藏已正式接入；编译与结构/Map回读完成，用户接手运行及视觉验收。 |
| 2026-11-06 | [游骑兵后台事务与HUD长帧治理及渲染缓冲定位](../Archive/20261106-游骑兵交战长帧治理.md) | performance, network, ui, rendering | partial | 三轮各884帧，后台维护峰值126.875→2.822ms，HUD Tick峰值12.929→5.001ms，>25ms帧39→9；平均帧耗时基本持平。 |
| 2026-11-06 | [蓝方交战镜头Hybrid状态应用性能优化](../Archive/20261106-Hybrid状态应用性能优化.md) | performance, network | partial | 三轮同镜头状态应用2.979091→2.055180 ms/帧，下降31.013%；源码/构建/负载/场景数据完成，玩家视觉验收待反馈。 |
| 2026-11-06 | [直线弹丸三轮交火性能复测](../Archive/20261106-直线弹丸三轮交火性能复测.md) | combat, network, performance | partial | 构建并加载修复后，三个旧版与三个新版交火房间的直线弹丸均值、P95、最大值全部下降，每逻辑步耗时减少32.7%。 |
| 2026-11-06 | [直线弹丸发布与碰撞池热路径优化](../Archive/20261106-直线弹丸发布与碰撞池热路径优化.md) | combat, network, performance | partial | 将弹丸私有编码移到实时发布资格判断之后，复用默认发射器的原协议字节，减少碰撞池复制与回收查找。已完成静态核对并保存对应交火场景入口。 |
| 2026-11-05 | [姿态同步与交互卡顿优化会话收尾](../Archive/20261105-姿态同步优化会话收尾.md) | network, commander, resource, navigation, performance | partial | 按用户要求完成本工作项，保留双端复采证据，清理会话临时文件，并单独交付源码、协议清单和文档。 |
| 2026-11-05 | [姿态通道 100 KB/s 目标验收](../Archive/20261105-姿态通道100KB每秒验收.md) | network, commander, performance | passed | 协议 36 在 600 单位早期战斗窗口将姿态通道从 160.43 降至加权平均 54.94 KB/s，三间独立房间均达标。 |
| 2026-11-05 | [姿态队列分类计数编译与加载](../Archive/20261105-姿态队列分类计数编译与加载.md) | network, commander, resource, navigation, performance | partial | 带队列分类计数的 Editor 构建成功，原地图已打开，当前进程已加载新版性能插件及三个项目模块。 |
| 2026-11-05 | [姿态队列分类计数静态交付](../Archive/20261105-姿态队列分类计数.md) | network, commander, resource, navigation, performance | partial | 性能工具新增三类传输队列的窗口计数，以及静止 Mass 士兵姿态首发与编码前跳过的计数。原地图采集说明已保存。 |
| 2026-11-05 | [姿态优化双端编译与协议35清单](../Archive/20261105-姿态优化双端编译与协议35清单.md) | network, commander, resource, navigation, performance | partial | Editor 和协调器构建成功，四份正式清单通过协议35校验，原地图已恢复并完成600单位入口回读。 |
| 2026-11-05 | [姿态同步与交互卡顿优化静态交付](../Archive/20261105-姿态同步与交互卡顿优化静态交付.md) | network, commander, resource, navigation, performance | partial | 姿态/回执批量传输、无变化状态抑制、矿石归属更新及选择轻量查询已接入；静态检查与原地图实体核对完成，未编译或运行复测。 |
| 2026-11-04 | [游骑兵正式闪现动画与跑步切换](../Archive/20261104-游骑兵正式闪现动画与跑步切换.md) | movement, presentation | partial | 游骑兵地面达到标定走跑中点后进入正式跑步混合空间。新增可编辑闪现全身序列并接入正式角色。 |
| 2026-11-04 | [游骑兵基础速度再次翻倍](../Archive/20261104-游骑兵基础速度再次翻倍.md) | movement, presentation | partial | 地面基础速度由 324 提至 648 cm/s，水平移动与加减速同步翻倍。已正式导表、刷新清单并定位闪现的程序姿势入口。 |
| 2026-11-04 | [游骑兵水平提速与腿甲材质接入](../Archive/20261104-游骑兵水平提速与腿甲材质接入.md) | movement, presentation | partial | 4001/A4B2 的水平移速和加减速已提至 1.5 倍，腿部 Alpha 掩码及五个分档材质已导入、编译和回读。 |
| 2026-11-04 | [游骑兵动画目录整合与旧资源退役](../Archive/20261104-游骑兵动画目录整合与旧资源退役.md) | movement, presentation, network | partial | 游骑兵手调源片段和正式动画统一迁入 GroundMech/Animations。旧目录资源及其引用已清理，源码版 Editor 构建加载及冷启动引用核对通过。 |
| 2026-11-04 | [游骑兵原横移恢复版编译加载](../Archive/20261104-游骑兵原横移恢复版编译加载.md) | movement, input, presentation, network | partial | 原横移版本已编译加载，Mobility 和 Skills 已重新导入。正式表、原动画和相机、运行清单及机动区实体均已回读。 |
| 2026-11-04 | [游骑兵撤回转腿走位并恢复横移](../Archive/20261104-游骑兵撤回转腿走位并恢复横移.md) | movement, input, presentation, network | partial | 按用户要求撤回转腿走位，恢复原横移输入、步态、速度与网络格式。Excel 正式导出及两份清单已逐一比对。 |
| 2026-11-04 | [游骑兵转腿走位编译接入与机动区交付](../Archive/20261104-游骑兵转腿走位编译接入与机动区交付.md) | movement, input, presentation, network | partial | 源码版 Editor 构建加载成功，Mobility 和 Skills 完成 UE 导入，200／600 清单更新。原机动区已保存并核对 10 个实体和正式角色输入。 |
| 2026-11-04 | [游骑兵转腿走位源码与数据导出](../Archive/20261104-游骑兵转腿走位源码与数据导出.md) | movement, input, presentation, network | partial | 按镜头八向转腿的地面、空中、步态与快照代码已接入，Excel 更新并完成正式导出。 |
| 2026-11-04 | [Python工具清理与制作入口合并](../Archive/20261104-Python工具清理与制作入口合并.md) | project, data-pipeline, movement | passed | 从已发布的1124个Python文件中移除185个旧脚本，现有939个。合并游骑兵场景入口，迁移仍在使用的写表和闪现材质代码。 |
| 2026-11-04 | [游骑兵3C结项与临时文件清理](../Archive/20261104-游骑兵3C结项与临时文件清理.md) | movement, input, presentation, camera, network | partial | 按用户指示完成游骑兵3C、指定视口相机与上下身独立转向工作项，清理27个临时文件及2个空目录。 |
| 2026-11-04 | [游骑兵上下身独立转向编译接入与交付](../Archive/20261104-游骑兵上下身独立转向编译接入与交付.md) | movement, input, presentation, camera, network | partial | 用户许可后完成源码版Editor编译加载、Mobility单表导入、上身帧局部接线和200/600清单刷新；原机动区及正式引用已保存读回，待玩家验收。 |
| 2026-11-04 | [游骑兵上下身独立转向源码与源表准备](../Archive/20261104-游骑兵上下身独立转向源码与源表准备.md) | movement, input, presentation, camera, network | partial | 独立炮台水平阻尼、胸舱稳定、上身坐标限位及同步恢复已写源码，Excel新增两字段并标准导出；静态核对和原场景保存完成，原生编译待确认。 |
| 2026-11-04 | [游骑兵当前视口相机参数同步](../Archive/20261104-游骑兵当前视口相机参数同步.md) | camera, presentation | partial | 按当前蓝图预览视口更新Camera.Free，Excel、UE表、蓝图默认值、初始距离和200/600清单已同步保存；原机动区实体核对通过。 |
| 2026-11-03 | [游骑兵3C编译相机冻结与正式接入](../Archive/20261103-游骑兵3C编译相机冻结与正式接入.md) | movement, input, presentation, camera, network | partial | 用户批准后完成源码版Editor构建加载，重新指定的预览相机已冻结；五表、动画标定、两份清单及机动区完成正式接入与结构化核对。 |
| 2026-11-03 | [游骑兵3C源码源表与场景准备](../Archive/20261103-游骑兵3C源码源表与场景准备.md) | movement, input, presentation, camera, network | partial | 常态瞄准、八成横移、双倍转身及独立镜头恢复源码和源表已修改；静态核对及原机动区保存读回完成，未编译加载。 |
| 2026-11-03 | [巡天命名统一](../Archive/20261103-巡天命名统一.md) | ship, ui, data-pipeline, project | partial | 巡天确定为玩家亲自操控的第一代飞船，统一当前5002舰体的源码、源表、文档和UE文本，保留Air共用席位与飞船类别。 |
| 2026-11-03 | [游骑兵命名统一](../Archive/20261103-游骑兵命名统一.md) | combat, ui, data-pipeline, project | partial | 游骑兵确定为玩家亲自操控的第一代机甲，统一源码、源表、文档和资源文本，保留Ground共用席位及4001模型入口。 |
| 2026-11-03 | [GroundHUD会话完成、临时清理与项目代码上传](../Archive/20261103-GroundHUD会话完成清理与代码上传.md) | ui, presentation, combat, performance | partial | 按用户指令关闭本会话HUD、折射及灼烧工作项，清理临时文件并仅同步项目代码；正式资源和最终证据保留。 |
| 2026-11-03 | [Ground图标导入遗漏网络清单修复](../Archive/20261103-Ground图标导入遗漏网络清单修复.md) | ui, presentation, combat, performance | partial | 修复图标导入跳过Mech后处理造成的配置指纹不一致；200/600清单同步后，正常Ground新局已控制机甲并创建HUD，7张图标加载。 |
| 2026-11-03 | [Ground三渲二图标数据管线实装](../Archive/20261103-Ground三渲二图标数据管线实装.md) | ui, presentation, combat, performance | partial | 七张A_v4三渲二图标正式替换，八个用途共用稳定IconId，喷气跳跃与容量共用；Editor构建和资产/场景回读通过，玩家视觉待验收。 |
| 2026-11-03 | [Ground完整爆燃、图标管线与HUD循环](../Archive/20261103-Ground完整爆燃图标管线与HUD循环.md) | ui, combat, presentation, performance | partial | 保留附着灼烧的完整三发射器，八张生图图标由共享数据表接入，移除I详情并加入两处原包局部循环。 |
| 2026-11-03 | [Ground弹道、爆燃、折射边界与HUD修订](../Archive/20261103-Ground弹道爆燃折射边界与HUD修订.md) | combat, network, ui, vfx, performance | partial | 火红弹道、1秒爆燃、首次命中点50米折射与图标HUD正式接入；修复状态同步吞爆燃及半径超标，构建与结构化功能检查通过。 |
| 2026-11-02 | [GroundHUD原包短动画接入](../Archive/20261102-GroundHUD原包短动画接入.md) | ui, presentation, combat, performance | partial | 正式接入HUDuiPRO派生入场/模态/技能/就绪短动画、真实CD环与射击回弹，普通刷新约5Hz，四图集驻留56.25MiB；构建重载和结构化PIE检查完成。 |
| 2026-11-02 | [Mass弹丸与弹道黑色材质回退修复](../Archive/20261102-Mass弹丸与弹道黑色材质回退修复.md) | vfx, performance | partial | 修复Mass材质RGB输入读取Alpha导致的SM6编译失败和默认材质回退，正式材质与制作脚本同步，普通PIE颜色及发光数字回读通过。 |
| 2026-11-02 | [GroundHUD属性合并与仪表轮廓截图修正](../Archive/20261102-GroundHUD属性合并与仪表轮廓截图修正.md) | ui, presentation, combat, performance | partial | 按用户要求将无单位属性并入左下生命面板，缩小字号；补充独立仪表轮廓、双准星暗衬、强化符号与真实建筑地图信息。 |
| 2026-11-02 | [GroundHUD中文字体与仪表细化截图修正](../Archive/20261102-GroundHUD中文字体与仪表细化截图修正.md) | ui, presentation, combat, performance | partial | 第三轮实机截图接入真实中文字重、技能仪表与地形侧壁，修正标题及行位置；发现并修复字体重复导入后的Slate缓存崩溃。 |
| 2026-11-02 | [GroundHUD多宽高比与强化摘要截图修正](../Archive/20261102-GroundHUD多宽高比与强化摘要截图修正.md) | ui, presentation, combat, performance | partial | 第二轮截图修正宽高比锚点、图标、地图裁剪和卡牌布局；真实单击选择验证并修复摘要映射，编辑器原生导表避免强引用对象重建。 |
| 2026-11-02 | [GroundHUD截图核验与可读性修复](../Archive/20261102-GroundHUD截图核验与可读性修复.md) | ui, presentation, combat, performance | partial | 用户明确授权截图校验后发现并修复文字遮挡、边框覆盖和卡牌露角，三次增量构建加载成功；仍未达到参考图一比一。 |
| 2026-11-02 | [游骑兵HUD参考布局接入与导表修复](../Archive/20261102-游骑兵HUD参考布局接入与导表修复.md) | ui, presentation, combat, performance | partial | 三张参考对应的驾驶舱HUD、地图和Ground立体卡框已接入；构建和结构化运行检查通过，修复公共文字表重建崩溃。 |
| 2026-11-02 | [GroundHUD自动创建修复加载与开局检查](../Archive/20261102-GroundHUD自动创建修复加载与开局检查.md) | ui, presentation, combat, performance | partial | 用户批准结束PIE并编译重启；接管事件修复已加载，新开局自动创建唯一Ground HUD，原生布局和真实文字回读通过。 |
| 2026-11-02 | [GroundHUD缺失定位与本局恢复](../Archive/20261102-GroundHUD缺失定位与本局恢复.md) | ui, presentation, combat, performance | partial | 当前Ground PIE角色正确但Widget为0；定位到遗漏Pawn接管通知，源码已修复，本局经正式HUD入口恢复。 |
| 2026-11-02 | [HUD验证靶导致PIE启动拦截修复](../Archive/20261102-HUD验证靶导致PIE启动拦截修复.md) | ui, presentation, combat, performance | passed | 补齐新增HUD靶的部署认证与生产运行清单，解除PIE资源门禁拦截并验证Ground启动和退出。 |
| 2026-11-02 | [游骑兵HUD编译加载与导表](../Archive/20261102-游骑兵HUD编译加载与导表.md) | ui, presentation, combat, performance | partial | 获准完成源码版Editor构建、重启加载、HUD导表与正式入口和场景回读，待玩家验收。 |
| 2026-11-02 | [游骑兵HUD正式接入](../Archive/20261102-游骑兵HUD正式接入.md) | ui, presentation, combat, performance | partial | 记录Ground HUD源码接入、透明图集、直接选牌蓝图和原地图靶场保存阶段。 |
| 2026-10-30 | [HUDuiPRO独立组件动画入库与AE源清理](../Archive/20261030-HUDuiPRO独立组件动画入库与AE源清理.md) | assets, ui | partial | 81个HUDuiPRO独立组件动画完整导出111426帧并保存405个UE对象；按用户指示清理AE工程及原始素材，保留运行序列、重导入源和调用索引。 |
| 2026-10-30 | [HUDuiPRO AE资源导入UE](../Archive/20261030-HUDuiPRO-AE资源导入UE.md) | assets, ui | partial | HUDuiPRO 已派生并保存83张透明静态纹理、三组透明动效及配套媒体链和UI材质；99个新增UE资源及1402帧序列完成结构与引用核对。 |
| 2026-10-30 | [据点会话临时文件清理与代码同步](../Archive/20261030-据点会话临时文件清理与代码同步.md) | building, commander, assets | passed | 按用户要求清理24个据点缓存和过期会话状态文件，共172306字节；103个源文件与交付证据哈希保持，代码范围提交已推送并回读GitHub main。 |
| 2026-10-30 | [全卡牌窗口审计与灼烧两卡映射修正](../Archive/20261030-全卡牌窗口审计与灼烧两卡映射修正.md) | ui, vfx | partial | 对8张正式牌面检查窗口与UV，确认两张灼烧卡存在HTML/UE坐标约定混用，已经Excel管线校正共同映射并保存回读。 |
| 2026-10-30 | [折射子弹卡牌视差强度对齐](../Archive/20261030-折射子弹卡牌视差强度对齐.md) | ui, vfx | partial | 经Excel管线把折射卡全局深度1.25调整为0.10并部署正式材质，最前层系数与灼烧卡同为0.04；其余7张牌面保持。 |
| 2026-10-30 | [肉鸽会话临时文件清理与全项目代码同步](../Archive/20261030-肉鸽会话临时文件清理与全项目代码同步.md) | combat, network, ui, vfx | passed | 清理开发游骑兵肉鸽能力会话的127个临时文件，并按用户最终要求将全项目201个代码文件推送至GitHub main；资源与美术保留本地。 |
| 2026-10-30 | [游骑兵上身摆动代码同步核对](../Archive/20261030-游骑兵上身摆动代码同步核对.md) | combat, animation | passed | 游骑兵上身摆动21项代码脚本文档已推送GitHub main并回读核对，用户要求的完成标记、临时清理和代码同步均已处理。 |
| 2026-10-30 | [游骑兵上身摆动完成清理与代码同步](../Archive/20261030-游骑兵上身摆动完成清理与代码同步.md) | combat, animation | partial | 按用户指示完成上身摆动工作项，归并10份结果并删除13个临时文件22,169字节；本会话代码按独立范围同步GitHub。 |
| 2026-10-30 | [指挥官兵种弹道槽位与生命周期修复](../Archive/20261030-指挥官兵种弹道槽位与生命周期修复.md) | vfx, performance | partial | 修复Mass GPU弹道及CPU灯池的零槽位索引、一次性生命周期和重复分配，正式Commander交火数字回读通过。 |
| 2026-10-30 | [PIE部署校验与F4小卡池数组崩溃修复](../Archive/20261030-PIE部署校验与F4选牌崩溃修复.md) | combat, network, ui | partial | 修复PIE显式部署目录不一致、更新资源签名并保留几何，修复F4两张候选补位数组崩溃；实际Ground对局中选牌UI打开成功。 |
| 2026-10-30 | [灼烧卡牌正式接入、Buff数据层与能力解耦](../Archive/20261030-灼烧卡牌正式接入与Buff数据层.md) | combat, network, ui, vfx | partial | 两张灼烧卡与折射修正版、独立Buff表和持续效果事务已编译接入，原图验证区及两档Manifest保存回读完成，玩法和视觉待玩家。 |
| 2026-10-29 | [据点光柱自发光再次翻倍](../Archive/20261029-据点光柱自发光再次翻倍.md) | building, commander, assets | partial | 按用户指示单独将光柱自发光再翻倍：柱面4.4至8.8、中央亮条12至24；正式材质编译保存与49引用核对通过，其他灯槽和白腰带保持。 |
| 2026-10-29 | [据点白色腰带与双倍发光交付](../Archive/20261029-据点白色腰带与双倍发光交付.md) | building, commander, assets | partial | 用户图示腰部横灯带改白，四类HDR强度设为首版2倍；正式材质编译保存、49引用及局内三色核对完成，60秒自转与原浮动保持。 |
| 2026-10-29 | [据点HDR发光与60秒自转交付](../Archive/20261029-据点HDR发光与60秒自转交付.md) | building, commander, assets | partial | 正式据点材质增强光柱、亮线和灯槽HDR，保留三色及原Bloom；全状态新增60秒自转，49实体转速读回6°/秒，编译保存与授权局内核对完成。 |
| 2026-10-29 | [对称据点正式实装与旧网格退役](../Archive/20261029-对称据点正式实装与旧网格退役.md) | building, commander, assets | partial | 用户放行B_v1后正式接入49据点；4824三角面、上下对称、白蓝红光柱；删除3个旧UE网格，完成源码Editor编译、保存实体及授权局内核对。 |
| 2026-10-29 | [两卡冲击圈连续与单物件刚性视差](../Archive/20261029-两卡冲击圈连续与单物件刚性视差.md) | ui, vfx | partial | 按用户反馈修复两卡局部UV果冻变形、完整环装配与跨层物件归属，定向重绘4层并交付HTML版本04。 |
| 2026-10-29 | [灼烧两卡完整补绘与硬分割装配修复](../Archive/20261029-灼烧两卡完整补绘与硬分割装配修复.md) | ui, vfx | partial | 根据用户缺口反馈重新生成12份完整图层，移除硬回贴和旧轮廓裁切，更新两卡HTML版本03；美术效果待用户验收。 |
| 2026-10-29 | [六层卡牌可见内容保留与元素完整验收口径](../Archive/20261029-六层卡牌可见内容保留与元素完整验收口径.md) | ui, vfx | partial | 修正原图可见内容被生成补绘覆盖的问题，并按用户最新明确改为元素完整、分层正确验收，无需像素级还原。 |
| 2026-10-29 | [灼烧两卡用户参考与六层HTML交付](../Archive/20261029-灼烧两卡用户参考与六层HTML交付.md) | combat, ui, vfx | partial | 按用户两张最终参考及六层分组完成十二层补绘、燃烧弹修正和交互HTML，技术核对通过，视觉待用户审核。 |
| 2026-10-29 | [爆燃冲击波向外扩散参考v6](../Archive/20261029-爆燃冲击波向外扩散参考v6.md) | combat, ui, vfx | partial | 按用户方向标注补充冲击波向外扩散的表现，v6与完整输入提示词已归档，视觉待用户审核。 |
| 2026-10-29 | [据点B_v1腰环比例精修与最终核对](../Archive/20261029-据点B_v1腰环比例精修与最终核对.md) | building, commander, assets | partial | 据点B_v1交付前按A_v2补足腰环厚度、包覆护板及灯槽贴合，最终4824三角面；保存后重开验证两套光柱材质白蓝红驱动，成品待用户审核。 |
| 2026-10-29 | [据点Blender还原与光柱切色B_v1](../Archive/20261029-据点Blender还原与光柱切色B_v1.md) | building, commander, assets | partial | 用户放行A_v2进入Blender还原，完成4568三角面的上下对称成品与中央光柱白蓝红切换；保存重开核对及实际成品对照已交付，B审核和UE替换待放行。 |
| 2026-10-29 | [据点保留外形简化参考A_v2](../Archive/20261029-据点保留外形简化参考A_v2.md) | building, commander, assets | partial | 用户认可A_v1形状并要求减少结构复杂度；已编辑生成A_v2主效果与三视图，保留外形、简化装甲及内部机构，尚无新网格或面数统计。 |
| 2026-10-29 | [上下对称科幻据点参考A_v1](../Archive/20261029-上下对称科幻据点参考A_v1.md) | building, commander, assets | partial | 用户否决现有平底据点造型，要求上下对称且更科幻；已生成A_v1效果与三视图设计板，保存提示词、哈希及待审核记录，尚未制作新网格。 |
| 2026-10-29 | [据点局内净空与平底模型核对](../Archive/20261029-据点局内净空与平底模型核对.md) | building, commander, assets | partial | 用户授权本会话读图后，R1C3局内低机位显示完整平底与离地间隙；源网格确认平底封口，补齐旧评审对象游戏隐藏并保存地图，玩家观感待确认。 |
| 2026-10-29 | [据点全状态持续悬浮修正](../Archive/20261029-据点全状态持续悬浮修正.md) | building, commander, assets | partial | 用户确认全部据点始终浮空，删除中立落地及起落逻辑，统一离地4.5～5.5米；源码Editor已编译加载，49个预览保存读回。 |
| 2026-10-29 | [弹道照明正式应用与直接接入规则](../Archive/20261029-弹道照明正式应用与直接接入规则.md) | vfx, performance | partial | 用户要求直接应用并删除开关及旧代码；正式ID8/38/58、代码清理、技能更新及Editor构建重载通过。 |
| 2026-10-29 | [PIE增量修复编译与烘焙校验交付](../Archive/20261029-PIE增量修复编译与烘焙校验交付.md) | combat, network, ui, vfx, performance | partial | 用户授权Editor编译重载通过；23部署严格重验证及生产运行清单刷新完成，矿区保持，资源与导航拦截已解除。 |
| 2026-10-29 | [爆燃冲击波放大出画框参考v5](../Archive/20261029-爆燃冲击波放大出画框参考v5.md) | combat, ui, vfx | partial | 按用户要求扩大躯干中部冲击波并允许超出画框，v5及完整输入提示词归档，视觉待用户审核。 |
| 2026-10-29 | [PIE烘焙拦截定位与场景修正](../Archive/20261029-PIE烘焙拦截定位与场景修正.md) | combat, network, ui, vfx, performance | partial | 定位肉鸽新增部署及静态障碍造成资源与导航过期；导航已保存并修正三处靶位，严格增量重验证源码待编译。 |
| 2026-10-29 | [爆燃冲击波上移至躯干中部参考v4](../Archive/20261029-爆燃冲击波上移至躯干中部参考v4.md) | combat, ui, vfx | partial | 根据用户反馈将脚边冲击波改为躯干中部单环，v4与输入／提示词已归档，视觉待用户审核。 |
| 2026-10-29 | [爆燃卡牌追加冲击波参考v3](../Archive/20261029-爆燃卡牌追加冲击波参考v3.md) | combat, ui, vfx | partial | 按用户要求在爆燃v2上追加单圈冲击波，内置生图v3与完整输入／提示词已保存，待用户审核。 |
| 2026-10-29 | [灼烧爆裂子弹爆燃参考v2](../Archive/20261029-灼烧爆裂子弹爆燃参考v2.md) | combat, ui, vfx | partial | 根据用户要求将简单爆炸修订为持续灼烧中的爆燃，内置生图v2已保存，视觉待用户审核。 |
| 2026-10-28 | [灼烧子弹按用户绿线基准修订v3](../Archive/20261028-灼烧子弹按用户绿线基准修订v3.md) | combat, ui, vfx | partial | 冻结用户最新单一绿线标注，以其为唯一编辑输入生成灼烧v3，保存原图、提示词和结果，透视待用户审核。 |
| 2026-10-28 | [旧游骑兵清理与灼烧参考透视修订](../Archive/20261028-旧游骑兵清理与灼烧参考透视修订.md) | combat, network, vfx | partial | 用户要求保留插画旧造型并清理旧运行资源；31资产及目录入口删除，当前A4B2保留，灼烧透视v2已生成待审核。 |
| 2026-10-28 | [灼烧卡牌与折射正背面修正版参考初稿](../Archive/20261028-灼烧卡牌与折射正背面修正版参考初稿.md) | combat, network, ui, vfx | partial | 按已批准计划先交三张内置生图参考，输入、完整提示词、输出和技术文件检查已归档，美术待用户审核。 |
| 2026-10-28 | [游骑兵肉鸽与折射子弹编译与接入](../Archive/20261028-游骑兵肉鸽与折射子弹编译与接入.md) | combat, network, ui, vfx | partial | 获准的Editor与Coordinator构建成功，新模块、四表、StringTable和两份Manifest已接入；原图15实体保存回读，效果待玩家验收。 |
| 2026-10-28 | [游骑兵肉鸽与折射子弹源码及场景交付](../Archive/20261028-游骑兵肉鸽与折射子弹源码及场景交付.md) | combat, network, ui, vfx | partial | 写入个人Ground构筑、依赖图、F4路由、即时折射结算及文案；源表导出和静态检查通过，原图15实体保存读回，原生构建待授权。 |
| 2026-10-28 | [游骑兵与Mass弹道照明候选交付](../Archive/20261028-游骑兵与Mass弹道照明候选交付.md) | vfx, performance | partial | Ground橙黄、Mass GPU细尾迹及6槽CPU照明候选已编译保存；原地图默认停用入口冷读取通过，正式引用待玩家审核。 |
| 2026-10-28 | [据点尺寸与浮动编译及加载](../Archive/20261028-据点尺寸与浮动编译及加载.md) | building, commander, assets | partial | 获用户许可后完成源码Editor构建及空闲重启，新原生尺寸接口和浮动参数已加载；对应Map保存与实体核对通过，玩家效果待验。 |
| 2026-10-28 | [据点尺寸与浮动源码及场景交付](../Archive/20261028-据点尺寸与浮动源码及场景交付.md) | building, commander, assets | partial | 按护盾最终三轴尺寸3倍实现据点表现与8秒轻微浮动，49个预览和护盾参照已保存读回；原生编译及玩家效果待验。 |
| 2026-10-28 | [游骑兵上身摆动编译与接入](../Archive/20261028-游骑兵上身摆动编译与接入.md) | combat, animation | partial | 按用户授权完成源码Editor构建重启、Gait四字段导入、动画图一个连接接入及双清单同步，原图三实体保存核对，效果待玩家验收。 |
| 2026-10-28 | [游骑兵上身摆动源码与场景交付](../Archive/20261028-游骑兵上身摆动源码与场景交付.md) | combat, animation | partial | 完成Ground逐发胸舱摆动源码、Excel四字段、限定安装脚本和原图射击靶；静态检查通过，原生编译加载与资产接线待许可。 |
| 2026-10-28 | [重防号Q水平射程编译与加载](../Archive/20261028-重防号Q水平射程编译与加载.md) | commander, combat | partial | 用户批准后完成源码版GuLiStrikeEditor构建与空闲重启，新进程加载Q水平射程修改，原图8个实体保存回读通过。 |
| 2026-10-28 | [重防号Q水平射程源码与场景交付](../Archive/20261028-重防号Q水平射程源码与场景交付.md) | commander, combat | partial | Q改为水平距离判定，保留基础240米与普攻120米；静态检查和原型图8个实体保存读回完成，尚未编译加载。 |
| 2026-10-28 | [闪现与GroundMech会话清理及代码同步](../Archive/20261028-闪现与GroundMech会话清理及代码同步.md) | combat, network, input | passed | 清理73个临时文件约30.17 MiB，本会话156项代码/脚本/文本变更已推送GitHub main并核对；正式资源及其他任务改动保留。 |
| 2026-10-28 | [肉鸽卡牌会话临时文件清理与代码同步](../Archive/20261028-肉鸽卡牌会话临时文件清理与代码同步.md) | ui, assets | passed | 清理37项会话临时内容约27MB，保留正式六层及源资源，60个代码数据文档文件已正常推送并核对GitHub main。 |
| 2026-10-27 | [肉鸽卡牌美术资源目录与统一部署交付](../Archive/20261027-肉鸽卡牌美术资源目录与统一部署交付.md) | ui, assets | partial | 六套牌面纳入统一Excel和四张UE目录表，兼容导出、实际部署、重复执行、参数更新及共享评审地图已验证，待用户视觉核验。 |
| 2026-10-27 | [GroundMech功能目录与组件职责整理交付](../Archive/20261027-GroundMech功能目录与组件职责整理交付.md) | combat, network, input | partial | Ground源码按功能归档并提取闪现/镜头组件；原生构建加载、配置与原地图9实体核对完成，玩法与协议保持，待玩家回归。 |
| 2026-10-27 | [游骑兵单体能量替身瞬移与铺屏镜头交付](../Archive/20261027-游骑兵单体能量替身瞬移与铺屏镜头交付.md) | combat, network, vfx, input | partial | Ground闪现更新为32米落点瞬移与单个移动机甲能量替身，新增专用收近构图；已编译加载、数据与镜头数值核验、原地图9实体保存完成，视觉待玩家。 |
| 2026-10-27 | [游骑兵闪现3C与连续恢复交付](../Archive/20261027-游骑兵闪现3C与连续恢复交付.md) | combat, network, vfx, input | partial | A参考通过后完成全程镜头保持/FOV、8槽能量材质渐变、三组线束、半蹲及瞬间朝向，原生编译与原地图交付通过。 |
| 2026-10-27 | [游骑兵闪现3C参考A_v1](../Archive/20261027-游骑兵闪现3C参考A_v1.md) | combat, network, vfx, input | partial | 玩家反馈670/s修订版仍未达到目标；按新增四项3C要求生成全身能量化材质和半蹲后摆姿态两张参考，待A审核。 |
| 2026-10-27 | [游骑兵闪现拖尾适配与前摇缩短](../Archive/20261027-游骑兵闪现拖尾适配与前摇缩短.md) | combat, network, vfx, input | partial | 根据玩家首版视觉未通过反馈，调整项目拖尾生成率、速度继承与基础缩放，导入0.02秒前摇并保存原地图入口。 |
| 2026-10-27 | [游骑兵闪现GAS与绿色能量拖尾交付](../Archive/20261027-游骑兵闪现GAS与绿色能量拖尾交付.md) | combat, network, vfx, input | partial | Ground闪现GA、Hybrid冷却GE投影、绿色拖尾与Excel迁移已编译加载，三表/两份清单/原地图实体核对通过。 |
| 2026-10-27 | [四批外部资源分类迁移与清单维护](../Archive/20261027-四批外部资源分类迁移与清单维护.md) | assets, art, vfx | partial | 四包807文件已分类迁入/Game/Assets，建立机器与人工清单；804包被Registry识别，803包可在当前编辑器解析，源包缺图、HDRIBackdrop与PhysX限制单列并保留原文件。 |
| 2026-10-26 | [重防号待机调整为0点8米与5秒循环](../Archive/20261026-重防号待机调整为0点8米与5秒循环.md) | commander, presentation | partial | 按用户继续调参，将WM01改为上下各0.8米、5秒循环、双轴各3.2度且相差90度；静态检查、重新编译加载和原图说明保存回读通过。 |
| 2026-10-26 | [重防号待机悬浮编译加载](../Archive/20261026-重防号待机悬浮编译加载.md) | commander, presentation | partial | 重防号待机改动获准后完成源码版Editor编译、正常退出与重启加载；BuildId一致、13样本及配置回读成功，玩家视觉待验。 |
| 2026-10-26 | [重防号待机悬浮源码与场景交付](../Archive/20261026-重防号待机悬浮源码与场景交付.md) | commander, presentation | partial | 重防号±1米、3秒完整循环及1.2度正交摇摆已写入配置与源码；原地图13样本说明已保存并回读，编译及玩家视觉待后续。 |
| 2026-10-26 | [开局席位选择UI编译加载](../Archive/20261026-开局席位选择UI编译加载.md) | ui, network | partial | 按用户授权完成选席位UI的源码版Editor构建和空闲编辑器重启，新开局选项及原图入口说明回读通过。 |
| 2026-10-26 | [开局席位选择UI源码交付](../Archive/20261026-开局席位选择UI源码交付.md) | ui, network | partial | 已实现指挥官、游骑兵和飞船的PIE开局选择窗口，完成静态检查及原图说明实体保存；编译加载待用户确认。 |
| 2026-10-26 | [地面低空分层PIE启动拦截修复](../Archive/20261026-地面低空分层PIE启动拦截修复.md) | commander, network | partial | 修复移动层元数据误改变矿区烘焙哈希及核验圆环重载后恢复碰撞的问题，原地图资源与全部导航检查恢复通过。 |
| 2026-10-26 | [地面与低空移动层实施交付](../Archive/20261026-地面与低空移动层实施交付.md) | commander, network | partial | 地面与低空分层已实施，重防号配表为低空30米；源码构建、兵种表、出生清单及14个原图核验实体完成交付。 |
| 2026-10-26 | [游骑兵镜头方向闪现与会话完成归档](../Archive/20261026-游骑兵镜头方向闪现与会话完成归档.md) | art, combat | partial | 闪现锁定按键瞬间镜头水平朝向，完成编译、资产与场景数据核对，清理121个临时文件，按用户指令完成本会话。 |
| 2026-10-26 | [游骑兵闪现时长调整为1.1秒](../Archive/20261026-游骑兵闪现时长调整为1点1秒.md) | art, combat | partial | 按用户明确数值将闪现Begin/Loop/End改为0.1/0.8/0.2秒，总计1.1秒，已导入UE并更新双网络清单。 |
| 2026-10-26 | [游骑兵闪现三阶段与镜头阻尼](../Archive/20261026-游骑兵闪现三阶段与镜头阻尼.md) | art, combat | partial | 闪现改为Begin0.4s、Loop0.5s、End0.6s，总计1.5s；Loop连续扫掠位移、沿途固定残影，镜头采用临界阻尼追赶。 |
| 2026-10-26 | [审核样机预览导致地图反复未保存修复](../Archive/20261026-审核样机预览导致地图反复未保存修复.md) | art, combat | passed | 定位BiZhiMao审核样机每帧ISM预览写入触发地图dirty，隔离自动预览修改并保留正常作者编辑；编译重开后预览运行且地图保持已保存。 |
| 2026-10-26 | [游骑兵Shift闪现与原命中特效调整](../Archive/20261026-游骑兵Shift闪现与原命中特效调整.md) | art, combat | partial | Shift闪现、双臂后展与固定残影已接入；撤下误制命中造型，恢复原6发射器，仅尺寸翻倍、橙黄、0.2秒淡出。修复PIE合同清单不一致，待玩家验收。 |
| 2026-10-26 | [游骑兵闪现与命中特效参考A_v1](../Archive/20261026-游骑兵闪现与命中特效参考A_v1.md) | art, combat | partial | 按用户先生图审核要求，生成双臂后展闪现与橙黄色命中特效两张A_v1，原图、提示词及哈希已保存，尚未实施UE候选。 |
| 2026-10-26 | [游骑兵固定世界曲线残留弹道](../Archive/20261026-游骑兵固定世界曲线残留弹道.md) | art, combat | partial | 按用户尾迹不位移要求将A3短跟随Sprite换为A4固定世界曲线，逐段显露并延迟淡出；正式表与保存资产重载通过，待玩家验收。 |
| 2026-10-25 | [游骑兵整臂制退与单曲线弹道接入](../Archive/20261025-游骑兵整臂制退与单曲线弹道接入.md) | art, combat | partial | 按用户直接制作授权接入整臂制退、单曲线尾迹、双倍枪焰及弹头尺寸，发射频率4/8/12与弹速24000；源码构建、七表导入和冷加载通过，待玩家视觉验收。 |
| 2026-10-25 | [游骑兵单曲线尾迹参考A_v3](../Archive/20261025-游骑兵单曲线尾迹参考A_v3.md) | art, combat | partial | 用户要求一条曲线，A_v2改为修改后再审；A_v3单曲线特写已生成，其他已确认目标保持。 |
| 2026-10-25 | [游骑兵能量尾迹参考A_v2](../Archive/20261025-游骑兵能量尾迹参考A_v2.md) | art, combat | partial | 用户确认两种射速都翻倍、要求A_v1修改，并提供紫色分叉能量尾迹；A_v2已生成，等待参考审核。 |
| 2026-10-25 | [游骑兵开火反馈参考A_v1](../Archive/20261025-游骑兵开火反馈参考A_v1.md) | art, combat | partial | 用户要求整臂后坐和两倍枪焰/弹头/曲线拖尾；已按现有模型生成A_v1分镜，射速含义待确认，未改正式资源。 |
| 2026-10-25 | [地图设计器清理道路避让与资源重置](../Archive/20261025-地图设计器清理道路避让与资源重置.md) | map-authoring, map, resources, navigation | partial | 备份最新页面编辑后清理185个旧道路区域并清空红蓝密度，保留地形/身份/基线；资源涂绘原样保存，道路专属强制校验改为可选。 |
| 2026-10-25 | [游骑兵后退步态与空中推进](../Archive/20261025-游骑兵后退步态与空中推进.md) | art, combat | partial | 后退两动作扩大轨迹并按机械腿约束重标定；空中采用机体方向WASD，仅有效喷气前进获864上限，其余324；五表和网络清单同步并完成冷启动资源检查。 |
| 2026-10-25 | [游骑兵脚部跳变与机械腿约束](../Archive/20261025-游骑兵脚部跳变与机械腿约束.md) | art, combat | partial | 用户反馈前进挑地和转向反折，授权PIE调试；定位锁脚瞬时释放和结构约束缺失，完成连续过渡、同轴刚性腿及Excel约束，编译并保存原地图。 |
| 2026-10-24 | [游骑兵固定方向与厚重步态](../Archive/20261024-游骑兵固定方向与厚重步态.md) | art, combat | partial | 机体参照WASD、非右键双枪回正、扩大步幅并重标定、局部移动转弯和脚底接触修订已实施；编译及冷加载通过，原地图保存交付。 |
| 2026-10-24 | [地图设计器固定负500至500米线性高程色阶](../Archive/20261024-地图设计器固定千米高程色阶.md) | map-authoring, map | partial | 高程色阶固定−500米至500米，二维与三维按同一高度公式取色，海平面0米处于中点。 |
| 2026-10-24 | [地图设计器统一蓝低红高色阶与海平面说明](../Archive/20261024-地图设计器蓝红高程色阶.md) | map-authoring, map | partial | 二维与三维共用蓝到红的高程映射，增加图例及海平面0米说明，前端构建通过。 |
| 2026-10-24 | [单颗矿石导航完成与清理](../Archive/20261024-单颗矿石导航完成与清理.md) | resource, navigation, commander, performance | partial | 按用户明确指令完成工作项收尾，保留编译与性能证据，清理20个临时文件约580.27MiB，并准备提交当前项目变更。 |
| 2026-10-24 | [单颗矿石导航编译与帧率实测](../Archive/20261024-单颗矿石导航编译与帧率实测.md) | resource, navigation, commander, performance | partial | 用户追加授权后源码版Editor构建通过；12个受控PIE窗口未见持续平均降帧，单颗解除0.14–0.16ms，234颗分4帧；渲染同步长帧单独记录。 |
| 2026-10-24 | [单颗矿石异步导航实现与场景交付](../Archive/20261024-单颗矿石异步导航实现与场景交付.md) | resource, navigation, commander, performance | partial | 单颗碰撞/避障/导航解除、200ms合批与共享静态矿石索引已实现；静态检查和原图33实体保存回读完成，编译与玩家效果未验证。 |
| 2026-10-24 | [游骑兵限位稳定与转向同步](../Archive/20261024-游骑兵限位稳定与转向同步.md) | art, combat | partial | 改为先限角再生成瞄准旋转，开放空中右键转向，W和右键共用转身曲线；源码构建、冷加载与场景回读完成。 |
| 2026-10-24 | [游骑兵双枪臂150度限位](../Archive/20261024-游骑兵双枪臂150度限位.md) | art, combat | partial | 修复只限制枪口而未限制枪臂水平朝向的遗漏，双臂最终姿态各限±75°；编译、冷加载和地图实体核对完成。 |
| 2026-10-24 | [游骑兵W跟随镜头、两倍转身与移动降速](../Archive/20261024-游骑兵W跟随镜头与移动降速.md) | combat, network, input | partial | 已按反馈接入W镜头朝向、转身及动画加倍、移动与加减速降至30%；编译、冷加载、数据和地图实体核对通过，效果待用户。 |
| 2026-10-24 | [GuLi 独立地图设计器与当前图工程交付](../Archive/20261024-GuLi独立地图设计器交付.md) | map-authoring, map, resources, navigation | partial | 交付编辑器外地图软件和当前完整工程，源码版构建与只读导入预检查通过，正式地图保持原样等待用户设计。 |
| 2026-10-24 | [游骑兵数据驱动机动、脚部IK与右键越肩控制](../Archive/20261024-游骑兵数据驱动机动IK与右键控制.md) | combat, network, input | partial | 已实施四张机甲配置表、双腿IK、速度匹配、根曲线转身和右键镜头；编译、冷加载资产及场景实体核对通过，待玩家验收。 |
| 2026-10-24 | [游骑兵动画冷加载丢节点修复](../Archive/20261024-游骑兵动画冷加载丢节点修复.md) | combat, art | partial | 修复PIE全部动画缺失和枪口不跟随的冷加载故障，补正之前检查重建图掩盖问题的结论；完整重启后的直接资产/姿势检查通过，等待玩家复验。 |
| 2026-10-24 | [游骑兵动画修复与越肩视角](../Archive/20261024-游骑兵动画修复与越肩视角.md) | art, combat | partial | 修复混合空间空采样和单侧开火断线，实现越肩横移、独立双枪后坐和当前帧瞄准；原生及蓝图编译、姿势数据和场景读取通过，玩家效果待验收。 |
| 2026-10-24 | [游骑兵PIE观察者与试玩入口更正](../Archive/20261024-游骑兵PIE观察者与试玩入口更正.md) | art, combat | partial | FireReview旧入口未创建Hybrid会话，玩家成为Observer；改用受支持的CommanderMassPrototype和Ground席位后，实际占有新版机甲及新动画实例。 |
| 2026-10-23 | [游骑兵B2正式UE导入与资源核对](../Archive/20261023-游骑兵B2正式UE导入与资源核对.md) | art, combat | partial | 按用户明确授权将当前B2导入并接入正式Ground，14段动作适配新骨架，29新资源硬引用与蓝图编译核对通过，FireReview地图保存完成。 |
| 2026-10-23 | [游骑兵 A_v4 定稿与 Blender 成品候选](../Archive/20261023-游骑兵A_v4定稿与Blender成品候选.md) | art, combat | partial | 用户批准两张 A_v4 为建模定稿；制作可编辑 Blender B_v1、红蓝材质、刚性骨架和待机适配，保存真实网格三视图供用户审核。 |
| 2026-10-23 | [游骑兵原包接入准备与折腿参考 A_v4](../Archive/20261023-游骑兵原包接入准备与折腿参考A_v4.md) | art, combat | partial | 保存 MechaDestroyer 原包、整机及双枪适配候选和原生构建证据；参考迭代至仅改腿折角的 A_v4，尚未正式切换 Ground 或开始风格化网格制作。 |
| 2026-10-23 | [行进响应计时与导航收尾](../Archive/20261023-行进响应计时与导航收尾.md) | commander, navigation, performance, network, ui | passed | 新增全部选中单位起步延迟显示，源码版Editor构建成功并重开原地图；清理本会话临时文件，按用户明确指令将相关工作项登记完成/通过。 |
| 2026-10-23 | [工程车同类互通源码版编译](../Archive/20261023-工程车同类互通源码版编译.md) | commander, navigation, performance, network | partial | 用户后续明确要求编译，工程车同类互通源码版Editor构建成功，BuildId一致；已重开原地图并更新保存验证说明，未启动新版PIE。 |
| 2026-10-23 | [工程车同类取消让路避障与碰撞](../Archive/20261023-工程车同类取消让路避障与碰撞.md) | commander, navigation, performance, network | partial | 按共用工程车类别取消彼此让路、预测与软避障及移动碰撞；保留与其他单位和环境的交互。静态检查及原图场景交付完成，新源码尚未编译运行。 |
| 2026-10-23 | [矿车作业衔接与重防号转向修复](../Archive/20261023-矿车作业衔接与重防号转向修复.md) | commander, navigation, performance, network | partial | 根据旧构建PIE现场定位矿车绕矿、厂外卸货目标、工程路线衔接和任务结束问题，并修正重防号机械部件插值；源码版Editor编译成功，原图入口保存读回，新版本效果待玩家验证。 |
| 2026-10-23 | [工程车姿态平滑补齐与验证边界](../Archive/20261023-工程车姿态平滑补齐与验证边界.md) | commander, performance, network | partial | 矿车和建造车接入独立模型姿态平滑，实际提交节拍修正已编译成功；按用户要求停止继续运行验证，最终效果待玩家核验。 |
| 2026-10-23 | [满速让路、本机平滑与工程车血条及攻击目标](../Archive/20261023-满速让路与本机平滑及工程车血条目标.md) | commander, navigation, performance, network | partial | 触发即满速让路、本机位置及动画平滑、工程车选中血条和敌方攻击目标已编译并通过部分原图PIE实体采样；原图入口已保存，保留单位化、方格站位和既有性能网络优化。 |
| 2026-10-23 | [UE共享寻路恢复、工程车Mass与旧执行清理](../Archive/20261023-UE共享寻路恢复与工程车Mass及旧执行清理.md) | commander, navigation, performance, network | partial | 恢复UE共享NavMesh及旧连续移动，保留单位化与方格站位；工程车接入Mass，删除旧格子/Crowd执行，源码编译和原图PIE已运行。密集穿行仍慢，性能无回退及完整业务恢复未全部验收。 |
| 2026-10-22 | [PIE进入后据点状态误判修复](../Archive/20261022-PIE进入后据点状态误判修复.md) | commander, navigation, performance, network | partial | 修复据点赠送建筑从轻量对象视图读取能力而误判缺失的问题，保留单项读取优化；重新编译加载，实际PIE运行待玩家复核。 |
| 2026-10-22 | [3米格PIE启动修复](../Archive/20261022-3米格PIE启动修复.md) | commander, navigation, performance, network | partial | 修复出生围挡对格、旧NavMesh启动检查及普通600人清单/服务版本；编译与启动前数据检查通过，实际PIE待玩家确认。 |
| 2026-10-22 | [3米整数格地面导航编译与地图迁移](../Archive/20261022-3米整数格地面导航编译与地图迁移.md) | commander, navigation, performance, network | partial | 源码版Editor构建通过，原指挥官地图完成300厘米格迁移与10个玩家入口实体交付；未运行玩法或性能测试。 |
| 2026-10-22 | [8月归档清单](../Archive/8月归档清单.md) | project | not_applicable | 2026年8月24篇归档按原路径转存ZIP；本页保留原ID、文件、状态、字节数及SHA-256定位清单。 |
| 2026-10-21 | [9月归档清单](../Archive/9月归档清单.md) | project | not_applicable | 2026年9月250篇归档已按原路径转存ZIP；本页提供原ID、文件、状态、字节数和SHA-256定位清单。 |
| 2026-10-21 | [近期性能优化整合归档（10月18–21日）](../Archive/20261021-近期性能优化整合归档.md) | performance, network, commander, combat, navigation, ui, tools, presentation | partial | 将本轮7份需求、18份开发文档和25篇增量归档收束为三个入口，原文按字节保存于ZIP；响应目标与玩家验收仍未完成。 |
| 2026-10-16 | [CS-P2P整合归档](../Archive/20261016-CS-P2P整合归档.md) | network, commander, combat | partial | 本会话归档与交付收尾完成，保全93份来源及历次独有事实；清理实际范围、平台拒绝及GitHub交付另记。 |
| 2026-10-14 | [Artifacts 与 ArtSource 运行时安全清理](../Archive/20261014-Artifacts与ArtSource运行时安全清理.md) | tools | partial | 根据用户授权清理两个目录的非运行时文件，删除 18,868 个文件、1,573 个空目录，逻辑大小 64.597 GiB，仅保留 105 份被文档引用的 Markdown 说明。运行资源未改动，安全性依据静态引用核对，未运行游戏。 |
| 2026-10-11 | [Mass会话GitHub交付与临时文件清理受阻](../Archive/20261011-Mass会话GitHub交付与临时文件清理受阻.md) | commander, navigation, presentation, performance | partial | Mass优化、原地图与报告共104个文件已上传GitHub main，远端提交核对一致；87个临时文件约85.4 MB的删除被自动审批策略拦截，未删除任何文件。 |
| 2026-10-11 | [Mass优化固定应用与开关移除](../Archive/20261011-Mass优化固定应用与开关移除.md) | commander, navigation, presentation, performance | partial | 按用户最新指示将已采用的Mass优化固定应用，删除本轮运行时开关与旧预测回退；重新编译13.06秒成功、8份BuildId一致、8项自动化通过，地图入口同步移除旧切换操作。 |
| 2026-10-11 | [Mass避障简化、转向提速与网络固定对照](../Archive/20261011-Mass避障简化转向提速与网络对照.md) | commander, navigation, presentation, performance | partial | 完整组合在200及600+500同进程场景各三组配对通过，默认采用六邻居预测、共享索引、保守零缓存与三倍转向；编译、8项自动化、原地图入口及退出恢复完成，网络积压仍在。 |
| 2026-10-11 | [会话变更编译、地图收尾与临时文件清理](../Archive/20261011-会话变更编译地图收尾与临时文件清理.md) | commander, navigation, combat, presentation, performance | partial | 用户授权后源码Editor构建退出0，8份BuildId一致；原地图遗留对象与说明清理并保存读回，66个临时文件约268 MB已删除。未追加PIE或效果验收。 |
| 2026-10-11 | [地面避障容量耗尽导致专服退出的诊断与调整](../Archive/20261011-地面避障容量耗尽导致专服退出的诊断与调整.md) | commander, navigation, performance | partial | 专服新增Mass障碍时耗尽共享Crowd的1024个注册槽，触发致命断言；配置及原生默认提高至8192，未重跑验证。 |
| 2026-10-10 | [遗留发射路径清理与地图保存阻塞恢复](../Archive/20261010-遗留发射路径清理与地图保存阻塞恢复.md) | ship, combat, network, performance | partial | 遗留发射实现、绑定资产和文本已清理；关闭任务启动的额外编辑器后，原地图保存及客户端PIE入口恢复。 |
| 2026-10-10 | [未通过性能候选回退与CPU及GPU瓶颈核对](../Archive/20261010-未通过性能候选回退与CPU及GPU瓶颈核对.md) | presentation, combat, ui, performance | partial | 按用户决定移除四项未通过的新候选和共用重构，保留网络观测及此前已采用优化；源码构建成功、地图入口更新，原始CPU/GPU计时补成不重复的游戏线程账目。 |
| 2026-10-10 | [组合P95同局切换与车辆负载混杂定位](../Archive/20261010-组合P95同局切换与车辆负载混杂定位.md) | presentation, combat, ui, performance | partial | 新增18段同局ABA/BAB/AAA诊断，确认200单位之外的车辆模型变换阶段峰值在全关时也出现；目标查询仍变快，剩余Slate和线程调度原因未锁定，四项默认关闭。 |
| 2026-10-10 | [快照性能优化实施、网络采样与采用判定](../Archive/20261010-快照性能优化实施网络采样与采用判定.md) | presentation, combat, ui, performance | partial | 完成原生网络采样、四项候选与源码build4，保留211份完整捕获及198份有效正式窗口；四项新候选均未通过完整默认采用门槛，旧优化保持开启。原地图入口已保存读回，设置恢复且编辑器关闭。 |
| 2026-10-10 | [PIE性能基线、多线程方案与会话临时文件清理](../Archive/20261010-PIE性能基线多线程方案与会话清理.md) | presentation, combat, ui, performance | partial | 新增性能基线与快照多线程需求/开发文档，保存18份配对统计、屏外计数和技术/视觉证据；核验后清理2377个临时文件约21.466 GiB。并行实现与大规模屏外收益尚未验证。 |
| 2026-10-10 | [网络指标HUD仅编译交付](../Archive/20261010-网络指标HUD仅编译交付.md) | commander, ui, performance | partial | 用户授权仅编译；网络HUD源码Editor构建成功，8份模块BuildId一致，保存地图并关闭占用DLL的编辑器后未重新打开。 |
| 2026-10-10 | [客户端表现专属技能与枪口批量化交付](../Archive/20261010-客户端表现专属技能与枪口批量化交付.md) | presentation, combat, vfx, performance | partial | 新建项目客户端表现技能，ID52三档CPU批量资源及默认2正式接入。14项相关技术检查和18份CPU窗口完成，200单位26.4至34.0FPS；压力真实枪口因上游积压未接纳，收益未证实，玩家视觉待验。 |
| 2026-10-10 | [左上角网络指标与飞行事件积压显示](../Archive/20261010-左上角网络指标与飞行事件积压显示.md) | commander, ui, performance | partial | 现有HUD增加每秒客户端连接流量、可靠待确认和飞行事件队列数据；240行文本表及原地图说明入口已保存读回，用户决定稍后统一编译。 |
| 2026-10-10 | [约200单位移动交火与场景UI性能截帧](../Archive/20261010-约200单位移动交火与场景UI性能截帧.md) | performance, ui, combat, vfx, commander | partial | 约200单位移动交火补采30秒，整帧40.61ms、GPU16.31ms；Client1场景UI约1.6ms CPU、描边截帧2.22ms GPU。最差GT69.69ms中战斗表现集中更新26.32ms，枪口Niagara为主要长帧来源。 |
| 2026-10-10 | [Slate分窗Prepass布局与绘制补采](../Archive/20261010-Slate分窗Prepass布局与绘制补采.md) | performance, ui | partial | 源码版Live Coding加载编辑器诊断后完成双客户端30秒分窗补采。编辑器主窗口扣除Client1视口为2.141ms，Client1与Client2视口UI为1.053与1.138ms，Slate合计4.791ms；编辑器占Slate约45%，占本次GT约8%。 |
| 2026-10-10 | [车辆CharacterMovement与飞船僚机移动开销拆分](../Archive/20261010-车辆CharacterMovement与飞船僚机移动开销拆分.md) | performance, combat, wingman, presentation | partial | 剩余飞行物与表现管线记录；旧独立投射路径描述已清理。 |
| 2026-10-10 | [Slate物理窗口与客户端游戏UI耗时拆分](../Archive/20261010-Slate物理窗口与客户端游戏UI耗时拆分.md) | performance, ui | partial | 复用Client1实战30秒Trace并按902完整帧拆分绘制。编辑器主窗口扣除Client1视口为1.138ms，Client1/2游戏UI为0.965/0.690ms；1.962ms布局Prepass缺少分窗计时，不能认定大部分Slate耗时属于编辑器。 |
| 2026-10-10 | [服务器StateTree条件查询与集中调度瓶颈分析](../Archive/20261010-服务器StateTree条件查询与集中调度瓶颈分析.md) | performance, commander | partial | 复用实战30秒Trace与CSV，Mass StateTree摊销1.558ms/引擎帧，执行帧平均5.447ms、P95 7.034ms；条件检查占已记录内部成本61.64%，发现重复事实读取和推进组线性查找。 |
| 2026-10-10 | [Client1实战PIE截帧与游戏线程瓶颈分析](../Archive/20261010-Client1实战PIE截帧与游戏线程瓶颈分析.md) | performance, commander, combat, presentation, ui, vfx | partial | 解除用户现有PIE调试暂停并采集Client1所在进程30秒；平均30.11FPS，GT33.18ms、GPU12.97ms。主要成本为同进程服务器/双客户端Tick与Slate，慢帧包含StateTree峰值和渲染资源分配等待。 |
| 2026-10-10 | [三项客户端优化正式接入与PIE验收交付](../Archive/20261010-三项客户端优化正式接入与PIE验收交付.md) | presentation, vfx, performance, combat, commander, ui, wingman | partial | 全部新变更经用户直接应用授权正式接入，53行Effects及真实僚机Profile/预警保存读回；源码构建和相关双客户端技术检查通过，地图14实体已保存。待玩家PIE验收，FPS对照暂缓。 |
| 2026-10-10 | [三项客户端优化手动PIE审核入口交付](../Archive/20261010-三项客户端优化手动PIE审核入口交付.md) | presentation, vfx, performance, combat, commander, ui | partial | 现有地图新增14个审核实体并保存回读，提供单次手动PIE候选覆盖；双客户端批量命中及真实对地入口、退出还原已短启动核对。完整资源认证、功能、性能与用户视觉验收仍待完成。 |
| 2026-10-09 | [600移动单位500弹丸PIE截帧耗时分析](../Archive/20261009-600移动单位500弹丸PIE截帧耗时分析.md) | performance, commander, combat, ship, network, ui, vfx | partial | 正式新特效/逻辑已回读，600移动单位与500弹丸压力复现15.55FPS；真实普通/P95/最慢帧定位命中特效、可靠事件处理和Ship集中生命周期，GPU Ribbon单帧9.253ms。追加修复收益尚未验证。 |
| 2026-10-09 | [四项PIE全部优化应用与整版对照](../Archive/20261009-四项PIE全部优化应用与整版对照.md) | performance, commander, navigation, combat, resources, building, ui, ship, network, vfx | partial | 全部追加优化正式应用，源码构建与21/1/3回归通过，普通双客户端约32–43FPS；同压力入口整版14.54→15.53FPS，接纳负载不同且P95未改善。保存28个场景实体，勘正旧16/8节点测量。 |
| 2026-10-09 | [四项特效视觉确认与正式引用切换](../Archive/20261009-四项特效视觉确认与正式引用切换.md) | vfx, combat, resources, building, performance | passed | 用户认可采矿7.5cm、建造12cm不透明激光与原模块枪口/命中独立副本；ID36/45/5/52已经Excel管线正式切换，52行目录、比例、蓝图绑定及Cook依赖回读通过。 |
| 2026-10-09 | [四项PIE性能优化实施与对照交付](../Archive/20261009-四项PIE性能优化实施与对照交付.md) | performance, commander, navigation, ui, combat, resources, building, vfx, presentation, network, ship | partial | 四项全阶段确定性改造已实施并通过源码Editor构建，60段最终对照完成；避障达标、弹丸平均下降16.36%但P95未达标。激光1.5倍宽Opaque候选及Map已保存，用户视觉和正式引用切换待完成。 |
| 2026-10-09 | [非Mass飞行弹丸六项优化分析与开发文档交付](../Archive/20261009-非Mass飞行弹丸六项优化分析交付.md) | combat, presentation, vfx, network, performance | partial | 完成空Actor变换、查询缓存、活跃数组、Niagara提交、表现裁剪和服务器碰撞组织六项源码分析，交付分阶段开发方案及证据；未实施优化，未产生优化后性能结果。 |
| 2026-10-09 | [PIE耗时与避障候选查询分析](../Archive/20261009-PIE耗时与避障候选查询分析.md) | performance, commander, navigation, ui, combat | partial | 当前同进程专用服务器和两个客户端PIE共681帧基线确认游戏线程瓶颈；候选查询约2.62ms，占预测处理器87%。完成源码拆解与开发方案，优化和新代码效果尚未实施验证。 |
| 2026-10-09 | [会话文档同步、临时清理与GitHub推送](../Archive/20261009-会话文档同步临时清理与GitHub推送.md) | assets, art, data, rendering, ui, commander, ground_mech, ship | passed | 同步本会话当前文档与历史阶段，清理30个临时备份、缓存、日志及草稿，保留已放行源文件与审核证据；主变更已推送GitHub main，1151个LFS对象上传完成。 |
| 2026-10-09 | [扫荡者橙区正式导入与自动改色](../Archive/20261009-扫荡者橙区正式导入与自动改色.md) | assets, data, rendering, commander | partial | 用户放行扫荡者原橙区B_v1；正式UV遮罩和本地自动改色已接入，Excel与三表保存回读、双方真实Q召唤及原三LOD属性核对通过，两类占位改色关闭。 |
| 2026-10-09 | [扫荡者原橙区队色Blender候选交付](../Archive/20261009-扫荡者原橙区队色Blender候选交付.md) | assets, rendering, commander | partial | 按用户取消新参考图并选择原橙区的决定，交付扫荡者蓝红共享网格B候选和精确UV遮罩，三LOD保存回读及固定区对照通过，Blender已打开、B待审核。 |
| 2026-10-08 | [全项目统一色库与美术技能接入](../Archive/20261008-全项目统一色库与美术技能接入.md) | art, rendering, assets, vfx | passed | 将用户四张色卡确认为全项目22色色库并接入美术skill，允许跨图组合并保留功能色和已审资产例外；本轮仅修改skill和文档。 |
| 2026-10-08 | [十四模型配色 Blender 成品 B_v1 交付](../Archive/20261008-十四模型配色Blender成品B_v1交付.md) | art, assets, rendering | partial | 记录用户明确开始制作后的十四模型二十八蓝红实际候选、112张原生四视图、参考比对及源网格保持检查；全部 B 待审核。 |
| 2026-10-08 | [十四模型配色参考 A_v3 与三视图交付](../Archive/20261008-十四模型配色参考A_v3交付.md) | art, assets, rendering | partial | 原色卡的十四模型二十八张蓝红参考已交付，含重防号、整件配色前哨、护盾队色顶灯和空军基地顶甲；停在具体版本 A 审核。 |
| 2026-10-08 | [十三模型配色参考 A_v2 与三视图交付](../Archive/20261008-十三模型配色参考A_v2交付.md) | art, assets, rendering | partial | 从用户四张色卡制作十三模型二十六张蓝红参考图板，保存图板、分区、源图与画廊；停在具体模型 A 审核。 |
| 2026-10-08 | [场景UI源码与18组红蓝模型对照交付](../Archive/20261008-场景UI源码与红蓝模型对照交付.md) | commander, ui, rendering, art, assets, building | partial | 单容器Slate UI与本地配色源码交付，18组模型独立对照已保存预览，Mass地图17个相关实体重载回读通过；未编译及运行验收。 |
| 2026-10-07 | [Mass脚环20cm正式材质与环境影响核对](../Archive/20261007-Mass脚环20cm正式材质与环境影响核对.md) | commander, ui | partial | 正式脚环材质已保存为20cm世界径向宽度并核对四种半径的编辑器渲染，保留RGBA与外半径；实际材质仍启用场景雾化。 |
| 2026-10-07 | [SSF会话临时文件清理与全项目上传范围](../Archive/20261007-SSF会话临时文件清理与全项目上传范围.md) | art, assets, tooling | passed | 按用户要求清理SSF会话3140个临时文件约3.20GB，保留正式资源及995个源文件和证据路径，上传范围为所有项目变更。 |
| 2026-10-07 | [SSF建筑蓝红正式资源B_v2审核通过](../Archive/20261007-SSF建筑蓝红正式资源B_v2审核通过.md) | art, assets, rendering | passed | 用户明确通过当前SSF蓝红正式B_v2，48新增资源及三档LOD完成本轮资产验收，暂不接入游戏。 |
| 2026-10-07 | [SSF建筑蓝红正式资源B_v2入库](../Archive/20261007-SSF建筑蓝红正式资源B_v2入库.md) | art, assets, rendering | partial | 用户放行当前B_v2后将蓝红两队48资源正式入库，独立重载、36LOD回读、378动画姿态和84UE图完成，不接入游戏。 |
| 2026-10-07 | [SSF建筑蓝红阵营配色Blender审核B_v2](../Archive/20261007-SSF建筑蓝红阵营配色Blender审核B_v2.md) | art, assets, rendering | partial | 十二个蓝红建筑配色副本已在实际Blender打开，75张原生渲染和独立回读完成；B_v2待用户审核，UE保持B_v1。 |
| 2026-10-06 | [SSF建筑B_v1正式资源入库且暂不接入玩法](../Archive/20261006-SSF建筑正式资源入库且暂不接入玩法.md) | art, assets, rendering | partial | 当前可见B_v1获用户正式存储放行，105资源、三档LOD、22动画与实际UE预览交付；保留原包，不接入玩法。 |
| 2026-10-05 | [SSF建筑Blender成品B_v1与预算例外](../Archive/20261005-SSF建筑Blender成品B_v1与预算例外.md) | art, assets, rendering | partial | A_v7依用户制作指令放行；实际B_v1十类资产、三档LOD、22完整动作与回读交付，24档本体超额及用户B待审，正式UE未导入。 |
| 2026-10-05 | [SSF线稿与三档明暗可见性修正A_v7](../Archive/20261005-SSF线稿与三档明暗可见性修正A_v7.md) | art, assets, rendering | partial | 核对A_v6已接入线稿和三档但可见性偏弱，保持全部配色与结构修正为完整A_v7，附实际拆分和同机位前后对照，当前待审核A。 |
| 2026-10-05 | [SSF军工厂与战略中心局部提亮A_v6](../Archive/20261005-SSF军工厂与战略中心局部提亮A_v6.md) | art, assets, rendering | partial | 附图核对为A_v3，仅提亮军工厂与战略中心四项深色，其余八资产和两座浅色保持；交付A_v6完整参考，成品和正式导入未开始。 |
| 2026-10-05 | [SSF建筑仅深色提亮参考A_v5](../Archive/20261005-SSF建筑仅深色提亮参考A_v5.md) | art, assets, rendering | partial | 按深色调浅浅色别动的最新澄清，仅提亮原深色，原浅色和三档明暗保持；交付全套A_v5，成品建模和正式导入未开始。 |
| 2026-10-05 | [SSF建筑浅色化参考A_v4](../Archive/20261005-SSF建筑浅色化参考A_v4.md) | art, assets, rendering | partial | 保留六套独立色系，按用户反馈同步提亮深色、框架、线稿和三档阴影，交付完整A_v4参考；成品建模及正式导入未开始。 |
| 2026-10-05 | [SSF建筑六套独立配色参考A_v3](../Archive/20261005-SSF建筑六套独立配色参考A_v3.md) | art, assets, rendering | partial | 按每座建筑不同色系的最新反馈，从三张用户色卡组合六套配色并交付A_v3全套效果图和三视图；成品建模及正式导入未开始。 |
| 2026-10-05 | [SSF建筑参考A与结构基线交付](../Archive/20261005-SSF建筑参考A与结构基线交付.md) | art, assets, rendering | partial | 六座SSF建筑及四件配套的参考A_v2、原始几何与22动画基线已交付；A待用户审核，成品建模及正式导入尚未开始。 |
| 2026-10-05 | [指挥官三档LOD审核放行与正式资源切换](../Archive/20261005-指挥官三档LOD审核放行与正式资源切换.md) | commander, art, assets, rendering, building | partial | 六兵种具体成品B已通过，76个正式资源组和三档引用已切换；获准源码Editor构建重开及真实彼之矛审核Actor保存，运行验收未执行。 |
| 2026-10-05 | [指挥官三档LOD纠正与候选资源交付](../Archive/20261005-指挥官三档LOD纠正与候选资源交付.md) | commander, art, assets, rendering | partial | 六兵种三档候选、完整资源组、真实预览和审核Map已保存；规范v1.3及相关历史说明勘误，正式切换待本版本B放行。 |
| 2026-10-05 | [彼之矛三档LOD进一步减面](../Archive/20261005-彼之矛三档LOD进一步减面.md) | commander, art, assets | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-05 | [彼之矛顶点动画制作与首轮源码交付](../Archive/20261005-彼之矛顶点动画制作与首轮源码交付.md) | commander, building, art, assets | partial | 彼之矛固定朝向建造源码和源表已实现，无运行骨骼VAT资源及真实预览已交付，原型图静态审核区已保存；编译与原生数据安装待答复。 |
| 2026-10-04 | [ControlRig机甲B-v4正式UE导入](../Archive/20261004-ControlRig机甲B_v4正式UE导入.md) | art, assets, rendering | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-04 | [ControlRig 机甲 B-v4 减少线稿保留轮廓](../Archive/20261004-ControlRig机甲B_v4减少线稿保留轮廓.md) | art, assets, rendering | partial | 按用户要求减少内部线稿，保留基础轮廓和三档；B-v4已显示在当前Blender，四图/三动作及同机位比较留档，B待审核，原面数预算仍超标。 |
| 2026-10-04 | [ControlRig 机甲 B-v3 实际线稿与三档明暗](../Archive/20261004-ControlRig机甲B_v3实际线稿与三档明暗.md) | art, assets, rendering | partial | 按用户实时线稿反馈修订B-v3，实际材质接入结构线/轮廓与三档明暗，当前Blender双3D预览可旋转；四图、真实三动作和预算差额已交付，B待审核。 |
| 2026-10-04 | [ControlRig 机甲 A 放行与实际 Blender B-v2 候选](../Archive/20261004-ControlRig机甲A放行与BlenderB_v2候选.md) | art, assets, rendering | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-04 | [ControlRig 机甲按用户色板修订参考 A-v2](../Archive/20261004-ControlRig机甲四色参考A_v2.md) | art, assets, rendering | partial | 按用户四色色板修订 ControlRig 机甲参考，交付并冻结原生2K效果与三视图，保持结构、骨架、姿态及源基线，A-v2待审核。 |
| 2026-10-04 | [ControlRig 机甲实际源采集与参考 A-v1 交付](../Archive/20261004-ControlRig机甲参考A_v1交付.md) | art, assets, rendering | partial | 恢复源资产只读查询，确认 ControlRig 四足单炮机甲的152骨骼与高面数基线，交付并冻结一致的八张2K参考和灰模，停在用户审核A。 |
| 2026-10-04 | [先驱号会话临时文件重试清理完成](../Archive/20261004-先驱号会话临时文件重试清理完成.md) | commander, art, assets | passed | 用户明确要求重试后，完成既有清单55个临时文件及两个空缓存目录的删除，70个冻结审核文件哈希仍全部一致。 |
| 2026-10-04 | [先驱号初代开发通过与 GitHub 交付](../Archive/20261004-先驱号初代开发通过与GitHub交付.md) | commander, combat, art, assets, rendering, data, network, performance | passed | 用户通过先驱号初代开发并授权清理与 GitHub 上传；记录当前批准基线、临时文件排除和已编译共用依赖的交付范围。 |
| 2026-10-04 | [绿色引导线玩家验收通过](../Archive/20261004-绿色引导线玩家验收通过.md) | commander, ui | passed | 绿色引导线遮挡修复编译加载及PIE地图占用解除后，用户回复通过；据此完成本次玩家验收，开发状态置done/passed。 |
| 2026-10-04 | [原型Map双编辑器文件占用解除](../Archive/20261004-原型Map双编辑器文件占用解除.md) | commander, ui | partial | 玩家PIE被导航保存门禁取消，查明任务重开实例锁住Map；正常退出空闲干净的重复实例后文件锁释放，保留玩家编辑器及新DLL。 |
| 2026-10-04 | [绿色引导线地形遮挡修复编译加载](../Archive/20261004-绿色引导线地形遮挡修复编译加载.md) | commander, ui | partial | 用户回复编译后，源码Editor目标构建退出0，8份BuildId一致，新DLL及覆盖材质配置已加载；跨坡实体回读通过，实际显示待玩家确认。 |
| 2026-10-04 | [绿色引导线地形遮挡修复静态交付](../Archive/20261004-绿色引导线地形遮挡修复静态交付.md) | commander, ui | partial | 玩家截图确认坡地遮挡后改接禁用深度测试的独立材质；静态检查及原型Map跨坡说明、观察实体保存回读通过，新原生代码尚未编译。 |
| 2026-10-04 | [飞行物启停同步与实际时间预算实施验收](../Archive/20261004-飞行物启停同步与实际时间预算实施验收.md) | commander, combat, network, ship, wingman, performance | partial | 实际时间连接预算和历史混合来源飞行启停通道已编译重载，500枚峰值、补建、池回收及17项回归通过；完整性能目标保留待验证。 |
| 2026-10-03 | [当前PIE网络预算与全场单位延迟诊断](../Archive/20261003-当前PIE网络预算与全场单位延迟诊断.md) | commander, network, combat, performance | partial | 当前专服加双客户端PIE采样确认状态姿态发送积压，地面弹丸快照占53.4%下行；120Hz预算截断与35Hz实际运行高度吻合，未实施修复或单变量复测。 |
| 2026-10-03 | [绿色引导线源码Editor编译加载](../Archive/20261003-绿色引导线源码Editor编译加载.md) | commander, ui | partial | 用户授权后源码Editor目标构建退出0，8份BuildId一致；重开编辑器确认新GuLiStrike DLL及原型Map的5个验收实体，运行效果待玩家确认。 |
| 2026-10-03 | [绿色引导线模型中心实时连接交付](../Archive/20261003-绿色引导线模型中心实时连接交付.md) | commander, ui | partial | 活动绿线新增模型中心逐帧跟随与缺失表现恢复，静态检查及原型Map的5个实体保存回读通过；编译和玩家效果验收尚未执行。 |
| 2026-10-03 | [先驱号步态五倍减速与一米改令](../Archive/20261003-先驱号步态五倍减速与一米改令.md) | commander, art, rendering | partial | 按用户反馈将先驱号四向六足 VAT 降至原速 20%，移动目标复用半径从实际 25 米改为 1 米；正式资产与配置已保存，当前会话未停止，运行采样约 0.2 倍。 |
| 2026-10-03 | [先驱号委托 PIE 验收与运行修复](../Archive/20261003-先驱号委托PIE验收与运行修复.md) | commander, combat, art, assets, rendering, data, network | passed | 用户明确委托助手代验；本地权威 PIE 核心 13 项通过，地图启动、基础速度、初生待机和墙内生成四个问题修复后重新编译加载，场景保存与烘焙有效。 |
| 2026-10-03 | [先驱号 Editor 编译与正式接线场景交付](../Archive/20261003-先驱号Editor编译与正式接线场景交付.md) | commander, combat, art, assets, data, network | partial | 用户另行授权编译重载，Editor 构建与新类型加载成功；正式 VAT、六张表及技能头像引用完成磁盘回读，指定地图最终保存核对通过，Mass 运行没有骨骼依赖。 |
| 2026-10-03 | [先驱号验收区域准备与资源目录回读](../Archive/20261003-先驱号验收区域准备与资源目录回读.md) | commander, art, assets, combat | partial | 原型图七个部署点、四个阻挡物与说明已保存并实体回读；明确新玩法尚未加载，补齐实际 Skeleton 与 Physics 资源目录映射。 |
| 2026-10-03 | [先驱号 B-v1 批准与 UE 美术 VAT 副本导入](../Archive/20261003-先驱号B-v1批准与UE美术VAT副本导入.md) | art, assets, commander, rendering, data | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-03 | [RSG 六足机器人 Blender B-v1 成品交付](../Archive/20261003-RSG六足机器人BlenderB-v1成品交付.md) | art, assets, rendering | partial | 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。 |
| 2026-10-03 | [RSG 六足机器人 A-v3 背环与头球配色](../Archive/20261003-RSG六足机器人A-v3背环与头球配色.md) | art, assets, rendering | partial | 按用户指定将背环改天蓝、头球改浅黄，保留八个天蓝小分件，同步 A-v3 四张 2048px 参考；源结构与旧版本保持，待审核 A-v3。 |
| 2026-10-03 | [RSG 六足机器人 A-v2 天蓝点缀](../Archive/20261003-RSG六足机器人A-v2天蓝点缀.md) | art, assets, rendering | partial | 按用户“加一点天蓝色”加入八个成对分件的少量天蓝点缀，同步四张 2048px A-v2 参考；源结构/相机及 A-v1 保持，等待审核 A-v2。 |
| 2026-10-03 | [RSG 六足机器人审核 A-v1 参考交付](../Archive/20261003-RSG六足机器人审核A-v1参考交付.md) | art, assets, rendering | partial | 交付源模型约束的 A-v1 效果图与三视图（均 2048px）、灰模对照、配色/活动件说明和审图页；用户审核 A 待决定，未开始重制或正式 UE 导入。 |
| 2026-10-02 | [海岛据点导航会话验收与清理上传](../Archive/20261002-海岛据点导航会话验收与清理上传.md) | commander, navigation, resources, art, assets, network | passed | 用户确认本会话全部文档通过，关闭6个工作项并归档验证数据；清理临时输出和可重建导入缓冲，保留海岛工程与游戏资源。 |
| 2026-10-02 | [连通认证暂关与R2C2落地区修复](../Archive/20261002-连通认证暂关与R2C2落地区修复.md) | commander, navigation, network | passed | 按用户要求暂关全图连通认证，R2C2 移至同格合格平地、资源及导航重建保存；PIE 核对停止任务与37人旧路线保持通过。 |
| 2026-10-02 | [不可达移动拒绝编译加载与PIE门禁记录](../Archive/20261002-不可达移动拒绝编译加载与PIE门禁记录.md) | commander, navigation, network | passed | 用户授权的 Editor 构建、BuildId 核对和 UE 重启加载通过；普通 PIE 被原地图资源门禁拒绝，运行效果未执行。 |
| 2026-10-02 | [不可达新移动指令拒绝源码与场景交付](../Archive/20261002-不可达新移动指令拒绝源码与场景交付.md) | commander, navigation, network | passed | 增加新令完整可达预检，拒绝时保留原任务并撤掉本次目标线；静态检查和地图场景保存通过，未编译或运行游戏。 |
| 2026-10-02 | [坡面导航编译重建与残留断区](../Archive/20261002-坡面导航编译重建与残留断区.md) | commander, navigation | passed | 用户授权后完成源码版Editor构建并重建新坡面预设；参数回读通过，全图20个区域使严格认证失败，19个孤立区域与旧标记高度失效已留档。 |
| 2026-10-02 | [坡面导航预设接入与场景准备](../Archive/20261002-坡面导航预设接入与场景准备.md) | commander, navigation | passed | 已接入20厘米台阶与坡度过滤预设，更新Prepare/Validate及旧缩放入口，完成静态检查并保存原三处验证标记和Commander导航显示；本次编译与导航重建待授权。 |
| 2026-10-02 | [坡面细碎导航断带生成参数分析](../Archive/20261002-坡面细碎导航断带生成参数分析.md) | commander, navigation | passed | 只读核对发现导航网格分辨率、7厘米台阶限制和44度坡度许可不匹配；UE5.7已有坡度边缘过滤选项，建议配合网格精度或台阶高度调整。尚未重建或确认截图中每条断带的具体成因。 |
| 2026-10-02 | [导航连通性校验编译与模块加载](../Archive/20261002-导航连通性校验编译与模块加载.md) | commander, navigation | passed | 用户授权后完成源码版 GuLiStrikeEditor 编译与 BuildId 核对，重新打开指挥官地图并确认新接口、严格地图配置及三个验证点；尚未执行导航验收。 |
| 2026-10-02 | [导航连通性烘焙校验实现与场景交付](../Archive/20261002-导航连通性烘焙校验实现与场景交付.md) | commander, navigation | passed | 已实现 CommanderSoldier 两点有向可达查询与全图强连通烘焙校验，静态检查通过，真实指挥官地图保存三个验证点；本阶段未编译或运行新代码。 |
| 2026-10-02 | [山脚高台导航连通性确认](../Archive/20261002-山脚高台导航连通性确认.md) | commander, navigation | passed | 当前四个选中单位到高台及反向查询均为部分路径且原生搜索未耗尽，两端内部各自连通；确认当前CommanderSoldier缺少山脚到该高台的可走连接。 |
| 2026-10-01 | [Mass编译后高处部分路径诊断](../Archive/20261001-Mass编译后高处部分路径诊断.md) | commander, navigation | passed | 用户编译后自行开启PIE，新高处目标的10次CommanderSoldier查询均仅有部分路径；135停滞约168秒仍Normal，确认部分末端处理有遗漏。 |
| 2026-10-01 | [Mass陡坡绕行原生编译](../Archive/20261001-Mass陡坡绕行原生编译.md) | commander, navigation | passed | 用户授权后完成GuLiStrikeEditor Win64 Development构建，退出码0，项目与源码版引擎BuildId一致；编辑器加载及玩家效果未验证。 |
| 2026-10-01 | [Mass陡坡绕行源码与静态核对](../Archive/20261001-Mass陡坡绕行源码与静态核对.md) | commander, navigation | passed | 完成共享移动末段导航验证与受预算约束的停滞重寻路源码；静态核对通过，当前用户PIE和旧模块保持运行。 |
| 2026-10-01 | [指挥官1800米海岛与49据点交付](../Archive/20261001-指挥官1800米海岛与49据点交付.md) | commander, resources, art, assets | passed | 1800米扩岸海岛与49据点已保存重开；独立Gaea源、原生Editor编译、200蓝簇40红簇6240节点、500出生槽及104编辑器路径核对通过，浮动与联机效果待玩家验收。 |
| 2026-10-01 | [战斗海岛原生Landscape导入UE](../Archive/20261001-战斗海岛原生Landscape导入UE.md) | art, assets | passed | Gaea海岛已导入独立2300米UE关卡，保存重开及16位高度、六类权重逐像素核对通过，并留存实际UE预览。 |
| 2026-10-01 | [重防号战斗射程优化验收通过](../Archive/20261001-重防号战斗射程优化验收通过.md) | commander, combat | passed | 用户在重防号机枪120米、Q导弹240米交付后明确回复通过，工作项玩家整体验收完成。 |
| 2026-10-01 | [重防号战斗射程优化交付](../Archive/20261001-重防号战斗射程优化交付.md) | commander, combat | partial | 按用户要求将WM01机枪30→120米、Q导弹96→240米；源表、三张UE数据表、主动目录和11个场景实体已保存回读。 |
| 2026-10-01 | [机枪10度随机弹道审核通过](../Archive/20261001-机枪10度随机弹道审核通过.md) | commander, combat | passed | 用户在两兵种机枪10度随机弹道交付后明确回复审核通过，工作项整体玩家验收完成。 |
| 2026-10-01 | [指挥官关卡环境对齐游骑兵演示关卡](../Archive/20261001-指挥官关卡环境对齐游骑兵演示关卡.md) | art, rendering, assets | partial | 指挥官关卡的灯光、天空球云材质、雾与后期已按源图对齐，保存重载无参数差异，地形及玩法对象保持。 |
| 2026-10-01 | [游骑兵地表材质迁移至指挥官关卡](../Archive/20261001-游骑兵地表材质迁移至指挥官关卡.md) | art, rendering, assets | partial | 源地表材质通过关闭草簇的独立实例应用到指挥官地形，全范围自动层填充、保存重载及编辑器画面核对通过。 |
