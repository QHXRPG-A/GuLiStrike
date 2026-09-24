# 归档时间线

> 自动生成，请勿手改。运行 `progress_docs.py build` 刷新。

| 日期 | 归档 | 模块 | 验证 | 摘要 |
|---|---|---|---|---|
| 2026-09-24 | [Mass随机站位玩家验收](../Archive/20260924-Mass随机站位玩家验收.md) | commander, network, performance | passed | 用户在即时移动、随机唯一站位及模型包围盒间距修复后明确确认符合预期，要求收尾。 |
| 2026-09-24 | [Mass即时移动与随机站位交付](../Archive/20260924-Mass即时移动与随机站位交付.md) | commander, network, performance | partial | 实现即时移动和共享NavMesh路线，按最新截图反馈改为整单随机唯一站位及模型包围盒间距；编译及100人、混合50人动态数据核对通过，未确认全部到达和完整回归。 |
| 2026-09-24 | [Mass三阶段耗时与百人响应实测](../Archive/20260924-Mass三阶段耗时与百人响应实测.md) | commander, network, performance | partial | 当前三段计算累计CPU占比为终点检查43.2%、匹配1.3%、路径及连接55.5%；两条100单位命令都未达200ms全部完成。 |
| 2026-09-23 | [Mass首条绿线延迟现场诊断](../Archive/20260923-Mass首条绿线延迟现场诊断.md) | commander, network, performance | partial | 玩家首条绿线约1秒的反馈指向服务端首批规划等待；现场查询次数预算饱和，客户端未观察到绿线积压，导航全局失效会扩大工作量。 |
| 2026-09-23 | [Mass验证场景PIE阻塞修复](../Archive/20260923-Mass验证场景PIE阻塞修复.md) | commander, network, performance | partial | 移除不兼容的专项部署并移开占用出生位的墙体，默认500个出生和导航前置检查通过；修正此前人口档位交付结论。 |
| 2026-09-23 | [Mass分帧寻路与基础避障编译交付](../Archive/20260923-Mass分帧寻路与基础避障编译交付.md) | commander, network, performance | partial | 用户授权后修正编译错误，源码版Editor构建退出0，8份BuildId一致；运行效果待玩家验收。 |
| 2026-09-23 | [Mass分帧寻路与基础避障源码交付](../Archive/20260923-Mass分帧寻路与基础避障源码交付.md) | commander, network, performance | partial | 交付统一预算分帧规划、25人增量提交、终点事件与持久绿线、基础环境避障；原型图500人部署及障碍场景已保存回读，尚未编译或运行验收。 |
| 2026-09-23 | [Mass协议19运行监控与残余阻塞](../Archive/20260923-Mass协议19运行监控与残余阻塞.md) | commander, movement, network, performance | partial | 协议19用户PIE中名册最终收敛，但位置发送仍有4.8/5.4秒样本间隔及服务器单步跳点，不能宣布平滑问题解决。 |
| 2026-09-23 | [Mass按连接预算分批同步编译交付](../Archive/20260923-Mass按连接预算分批同步编译交付.md) | commander, movement, network, performance | partial | 获准源码Editor构建退出0并重启，协议19与8份BuildId核对通过，原图验收说明已保存重载读回；运行效果待玩家验证。 |
| 2026-09-23 | [Mass按连接预算分批同步源码实现](../Archive/20260923-Mass按连接预算分批同步源码实现.md) | commander, movement, network, performance | partial | 协议19按连接合并并分批发送名册和终点，位置先预算准入再提交编码历史；代码静态审查完成，编译和运行效果未验证。 |
| 2026-09-23 | [Mass复制流量与同帧突发定位](../Archive/20260923-Mass复制流量与同帧突发定位.md) | commander, network, movement, performance | partial | 新局600单位移动时，30秒记录47次姿态RPC被跳过；名册和移动终点同帧突发耗用余额，120Hz计时上限将每帧补充限制到2083.25字节。 |
| 2026-09-23 | [Mass姿态RPC发送预算饱和诊断](../Archive/20260923-Mass姿态RPC发送预算饱和诊断.md) | commander, network, movement, performance | partial | 当前PIE的35秒窗口确认4217次姿态RPC因连接发送预算饱和在UE发送入口被跳过；每连接有效限额250000字节每秒。 |
| 2026-09-23 | [Mass姿态缺帧与三倍追赶现场诊断](../Archive/20260923-Mass姿态缺帧与三倍追赶现场诊断.md) | commander, movement, network, performance | partial | 三倍限速有效但玩家平滑体验未通过；常规帧率下抓到470ms样本缺口和随后三帧满速纠偏，另有权威导航跳变。 |
| 2026-09-23 | [客户端连续纠偏与性能HUD交付](../Archive/20260923-客户端连续纠偏与性能HUD交付.md) | commander, movement, network, ui, performance | partial | 保留普通位置历史并以最多3倍标准速度纠偏，新增FPS/RTT；源码Editor编译加载与资产读回完成，用户自行验收。 |
| 2026-09-23 | [电源模式与Mass残余跳步诊断](../Archive/20260923-电源模式与Mass残余跳步诊断.md) | commander, network, performance | partial | 用户切换节能至平衡后持续卡顿缓解，但部分部队仍偶发跳步；本局17次权威位置突变，需继续修复位置连续性。 |
| 2026-09-23 | [客户端Mass硬校正修复编译交付](../Archive/20260923-客户端Mass硬校正修复编译交付.md) | commander, network, performance | partial | 用户授权后完成源码Editor构建，退出0且8份BuildId一致；冷启动加载新DLL与原图入口，效果由玩家验证。 |
| 2026-09-23 | [客户端Mass硬校正最小修复](../Archive/20260923-客户端Mass硬校正最小修复.md) | commander, network, performance | partial | 仅改客户端硬校正判定，用相邻权威样本时间与速度预算排除正常插值滞后；旧轨迹重算0误触发，静态检查通过，尚未编译。 |
| 2026-09-23 | [客户端Mass行走顿挫分析](../Archive/20260923-客户端Mass行走顿挫分析.md) | commander, network, performance, navigation | partial | 已有双客户端PIE只读采样确认硬校正把插值延迟误判为位置错误，正常行进单位反复跳动停留；另有低帧率降频和服务器大步跳变，本轮仅分析。 |
| 2026-09-23 | [红方移动修复源码Editor构建](../Archive/20260923-红方移动修复源码Editor构建.md) | commander, navigation, performance | partial | 红方移动的导航引用刷新及诊断修复已通过源码Editor编译，8份BuildId一致；用户关闭旧局后要求直接编译，未重启或运行新PIE。 |
| 2026-09-23 | [红方移动超时诊断与导航引用修复](../Archive/20260923-红方移动超时诊断与导航引用修复.md) | commander, navigation, performance | partial | 玩家双客户端PIE确认红方选择正常，新移动命令5秒超时后回Idle；现场15个位置均有完整导航路径，补动态导航引用刷新及失败阶段日志，尚未编译加载。 |
| 2026-09-23 | [1.8km战场与建造车自主接单](../Archive/20260923-1.8km战场与建造车自主接单.md) | map, navigation, commander, building, performance | partial | 2.3km地形与1.8km战场保持81据点身份、256组件和6240矿节点；建造车本据点优先、途中换单、施工暂停接单及抵达抢位已编译，场景和资产保存回读通过。 |
| 2026-09-23 | [3.2km地形与9×9据点及Mass出生修复](../Archive/20260923-3.2km地形与9x9据点及Mass出生修复.md) | performance, navigation, commander, map | partial | 原型图保存为3.2km地形、2.7km战场、81据点，256组件和资源总量保持；共享出生布局并避让完整初始军队，500槽编辑器预检通过。 |
| 2026-09-22 | [FlightNav边界复核与收紧](../Archive/20260922-FlightNav边界复核与收紧.md) | performance, navigation | partial | 用户指出FlightNav大于地图后，复核相对母舰的编队与恢复距离，去除重复余量，收紧至5.246km并减少下方空间；重新烘焙、保存及实体核对通过。 |
| 2026-09-22 | [4.2km地图与7×7据点重布局交付](../Archive/20260922-4.2km地图与7x7据点重布局交付.md) | performance, navigation | partial | Commander原型图重建为4.2km、256组件和7×7据点，资源及地空导航已烘焙保存，最新源码Editor重启回读通过；新图玩家效果和内存收益待验证。 |
| 2026-09-22 | [工程车四点返厂简化与32辆专项验证](../Archive/20260922-工程车四点返厂简化与32辆专项验证.md) | commander, resources, building, navigation, performance | partial | 工程车互相忽略碰撞与避让，矿车直接寻路到厂内四个无占用状态的卸货点；新模块与现有StateTree已加载，32辆配置下16辆矿车完成34次卸货。此次为返厂专项，完整功能与30分钟性能验收仍待完成。 |
| 2026-09-22 | [卡牌窗口焦点原生接口补齐](../Archive/20260922-卡牌窗口焦点原生接口补齐.md) | ui, presentation | partial | 根据追加授权增加最小只读窗口焦点接口，补齐卡牌失焦回正与后台点击拦截。 |
| 2026-09-22 | [三张视差3D卡牌演示场景交付](../Archive/20260922-三张视差3D卡牌演示场景交付.md) | ui, presentation, vfx | partial | 交付纯蓝图三牌入场、压动、锁定翻面、二次确认闪光、退场与重播场景；悬停按用户要求增加到 ±12°。 |
| 2026-09-22 | [三维动态卡牌与视差卡牌资产迁移](../Archive/20260922-三维动态卡牌与视差卡牌资产迁移.md) | ui, assets | partial | Reward Cards 3D 与 Parallax Card Material 经 UE5.7 引用安全重定位后迁入 /Game/Assets/card，共登记并静态验证 104 个资产和 2 张示例地图。 |
| 2026-09-21 | [外部特效与蓝图资源包迁移](../Archive/20260921-外部特效与蓝图资源包迁移.md) | vfx, assets | partial | 7 个外部 UE 资源包经 UE5.7 路径重定位后迁入 Content/Assets/ExternalPacks，共登记 1678 个资产、19 张地图，并补齐 57 个世界分区支持包。 |
| 2026-09-21 | [指挥官StateTree条件属性分类修复](../Archive/20260921-指挥官StateTree条件属性分类修复.md) | commander | partial | 修复Actor和Mass条件参数误用Context分类导致的无效类型显示；源码Editor构建成功，8个字段反射用途正确，三棵既有树编译保存且图结构不变。 |
| 2026-09-21 | [指挥官MassStateTree类型登记修复](../Archive/20260921-指挥官MassStateTree类型登记修复.md) | commander | partial | 玩家反馈首次分配Mass树实例时GuLiUnitTaskSubsystem类型信息缺失；补齐三个项目Subsystem的每World Mass类型登记，源码Editor增量构建成功，运行复查待玩家。 |
| 2026-09-21 | [指挥官StateTree迁移与场景交付](../Archive/20260921-指挥官StateTree迁移与场景交付.md) | commander, resources, building | partial | Commander Actor/Mass接入三棵共享StateTree，退役特殊任务目录及源表，内部业务阶段迁入树；原生构建、资产和原型地图保存读回完成，运行效果待玩家。 |
| 2026-09-21 | [地面机甲辅助瞄准编译与配置落地](../Archive/20260921-地面机甲辅助瞄准编译与配置落地.md) | ground-mech, combat, input, data | partial | 用户追加授权编译后，源码Editor构建通过且8份BuildId一致；机甲Skills表已导入保存并回读机枪开启/100cm，FireReview待玩家确认效果。 |
| 2026-09-21 | [地面机甲辅助瞄准代码与源表静态交付](../Archive/20260921-地面机甲辅助瞄准代码与源表静态交付.md) | ground-mech, combat, input, data | partial | 完成辅助射向代码及机甲Excel机枪行开关/100cm半径配置，保存并读回既有FireReview；未编译、未导入新行结构、未运行测试。 |
| 2026-09-21 | [FireReview导航阻塞与资产校验修复](../Archive/20260921-FireReview导航阻塞与资产校验修复.md) | ground-mech, commander, combat, presentation | partial | 修复动态装饰网格持续更新导航引起的0单位问题，原双端PIE恢复16台；修复飞行导航校验器适用范围，地图保存重载及资产验证通过。 |
| 2026-09-21 | [FireReview双阵营停火靶场与敌方描边静态交付](../Archive/20260921-FireReview双阵营靶场静态交付.md) | ground-mech, commander, combat, presentation | partial | 两队各8台真实Mass部队的部署已保存并重载核对，停火与阵营描边代码编译通过，实际效果交由玩家确认。 |
| 2026-09-21 | [统一特效目录与ID迁移静态验收](../Archive/20260921-统一特效目录与ID迁移静态验收.md) | combat, vfx | passed | 39 个特效定义及 41 条使用记录迁入 Excel 目录；资源、类型、Cook 依赖、导入幂等性、5 项自动化检查和源码版 Editor/Game 构建通过。 |
| 2026-09-21 | [地面机甲动画空战与下落调参验收](../Archive/20260921-地面机甲动画空战与下落调参验收.md) | ground-mech, animation, combat, network, input, ui, vfx | partial | 完成两倍下落导入、Dedicated飞行及输入验证，修复Mass顶面起飞侧碰误判并复验，登记用户对空处开火与当前连续表现的确认。 |
| 2026-09-21 | [机枪统一受击补全实机验收](../Archive/20260921-机枪统一受击补全实机验收.md) | combat, vfx | passed | 补齐僚机、机甲、指挥官的命中矩阵和原生回合重开；5项既有测试复跑通过，恢复原编辑器环境。 |
| 2026-09-21 | [地面机甲动画与空中战斗阶段实现](../Archive/20260921-地面机甲动画与空中战斗阶段实现.md) | ground-mech, animation, combat | partial | 完成可编辑动画蓝图、22段项目动画、空中WASD、即时双喷与正式开火；下落倍率待编辑器可用后完成导入验证。 |
| 2026-09-21 | [地面机甲火箭跳实现与实机反馈调整](../Archive/20260921-地面机甲火箭跳实现与实机反馈调整.md) | combat, network, input, ui, vfx | partial | GAS与Excel火箭跳已接入，未满常显、两倍容量条及1.5倍空中转向通过采样；0.25推力解释和部分实机验收待完成。 |
| 2026-09-21 | [机枪统一受击实现与验证边界](../Archive/20260921-机枪统一受击实现与验证边界.md) | combat, vfx | partial | 三类机枪统一接入原版NS_Flash_1，保存共享配置与重建脚本；双目标构建、5项现有自动化及指挥官/机甲多人实测通过，完整PIE矩阵仍待补齐。 |
| 2026-09-20 | [指挥官头像分组与导航重定位勘误](../Archive/20260920-指挥官头像分组与导航重定位勘误.md) | commander, ui, network | partial | 头像由逐单位实例改为同兵种每25名一组，移除实例号和头像长提示；导航修复重定位改用离散位移事件，Game Development构建通过。 |
| 2026-09-20 | [星际指挥官界面与游戏文本表](../Archive/20260920-星际指挥官界面与游戏文本表.md) | commander, ui | partial | 落地指挥官图标化HUD、可靠头像选择、185条Excel文本与32张参考审计；源码双目标和最后29项回归通过，保留既有失败及人工验收边界。 |
| 2026-09-20 | [机甲Mass碰撞复审修复与验证](../Archive/20260920-机甲Mass碰撞复审修复与验证.md) | movement, navigation, network, performance | partial | 完成原始姿态解耦、物理子步与连续碰撞、支撑回放校正和障碍注册边界修复；79项自动化及源码双目标构建通过，500单位400步无丢步，联机视觉终验保留未验证。 |
| 2026-09-20 | [指挥官任务与远端碰撞改动集成复验](../Archive/20260920-指挥官任务与远端碰撞改动集成复验.md) | commander, building, resources | passed | GitHub推送前合入远端581170eb，保留双方Mass改动，解决索引和编号冲突，源码双目标及任务、碰撞、业务、双客户端回归通过。 |
| 2026-09-20 | [指挥官部队操作与特殊任务系统实施](../Archive/20260920-指挥官部队操作与特殊任务系统.md) | commander, building, resources | passed | 完成按Excel定义的特殊任务、统一队列与指挥官操作，关联自动化、真实业务World、双客户端及源码双目标构建通过。 |
| 2026-09-20 | [玩家机枪开火与Excel升级候选](../Archive/20260920-玩家机枪开火与Excel升级候选.md) | combat, network, input, vfx | partial | 完成玩家机枪原生逻辑、Excel两表和逐发动画特效候选，源码构建及技术验证通过；正式接入等待用户B视觉审核。 |
| 2026-09-19 | [机甲与武器整批同步UE及总结](../Archive/20260919-机甲与武器整批同步UE及总结.md) | art, assets, rendering | partial | 按全部同步的明确授权补齐五项，八个成品共十网格全部进入源码UE5.7；展示关卡、十一张UE图、用途与保存回读已归档。 |
| 2026-09-19 | [轻型机甲与Spider同步UE](../Archive/20260919-轻型机甲与Spider同步UE.md) | art, assets, rendering, input, network | partial | 按明确授权将轻型v9和完整网格Spider同步源码UE5.7，更新玩家模型及Demo参照，修复动画默认值缓存并完成相关实机检查。 |
| 2026-09-19 | [轻型机甲排气口浮空修复](../Archive/20260919-轻型机甲排气口浮空修复.md) | art, assets, rendering | partial | 修复v8成对排气口浮空，内收框体并新增对称贴壳底座，完成保存接触回读及实际前后对比。 |
| 2026-09-19 | [轻型机甲弧面头甲与散热结构细化](../Archive/20260919-轻型机甲弧面头甲与散热结构细化.md) | art, assets, rendering | partial | 按用户参考恢复赭金色并重做弧面封舱甲、前双排气孔和肩侧散热舱，完成实际8视图与保存回读，尚未同步UE。 |
| 2026-09-19 | [机甲整件分色与黑蓝配色修订](../Archive/20260919-机甲整件分色与黑蓝配色修订.md) | art, assets, rendering | partial | 按用户反馈撤回条纹改为完整部件分色，Spider四片腿甲暗红；轻型蓝白对调后再次将白改黑，现行v7为黑舱甲和蓝腿甲。 |
| 2026-09-19 | [机甲线稿三渲二与对称配色修订](../Archive/20260919-机甲线稿三渲二与对称配色修订.md) | art, assets, rendering | partial | 五项机甲/武器补齐线稿和三档明暗，修正轻型背部取色；Spider增加镜像砖红、轻型增加镜像蓝白，完整原网格和绑定保留。 |
| 2026-09-19 | [SpiderMech恢复原网格并整理色块](../Archive/20260919-SpiderMech恢复原网格并整理色块.md) | art, assets, rendering | partial | 用户取消SpiderMech减面后恢复839778面原网格，仅整理连续色块和材质风格；几何、UV、权重、法线保存回读一致，v2成品待视觉审核。 |
| 2026-09-19 | [轻型装甲地面玩家接入与根位移修复](../Archive/20260919-轻型装甲地面玩家接入与根位移修复.md) | art, combat, network, input | passed | 封闭式轻型装甲完成Ground、原动画、增强输入和Demo副本接入；修复动画循环回跳，实机及联机检查、18项既有回归和源码版双目标构建通过。 |
| 2026-09-19 | [源模型机甲制作与SpiderMech减面](../Archive/20260919-源模型机甲制作与SpiderMech减面.md) | art, assets, rendering | partial | 基于 10 个实际源网格完成八个 Blender 成品候选，SpiderMech 839778→19656 三角面，轻型机甲封舱，红蓝降低饱和度。 |
| 2026-09-19 | [机甲与武器参考降低红蓝饱和度](../Archive/20260919-机甲与武器参考降低红蓝饱和度.md) | art, assets, rendering | partial | 根据用户“红蓝饱和度过高”的反馈，更新 Mecha_01 v2 和武器板 v4，保留色系并降低饱和度。 |
| 2026-09-19 | [两组机甲风格参考、封舱与低模方向](../Archive/20260919-两组机甲风格参考与低模方向.md) | art, assets, rendering | partial | 完成两组机甲六张效果参考和三视图，保留原色系与着色逻辑；轻型机甲封舱无驾驶员，SpiderMech 提交低模方向。 |
| 2026-09-19 | [战争机器爆炸追加四倍缩放](../Archive/20260919-战争机器爆炸追加四倍缩放.md) | commander, combat, vfx | passed | 用户反馈爆炸偏小，已将爆炸有效缩放0.4调至1.6，四倍播放对照与19次实际Q爆炸参数读回通过。 |
| 2026-09-19 | [战争机器导弹范围与特效调整](../Archive/20260919-战争机器导弹范围与特效调整.md) | commander, combat, data, vfx | passed | 战争机器单弹与预警半径改为2米，Q区域半径8米，导弹尾焰和指定2A爆炸采用两倍有效缩放，双客户端检查通过。 |
| 2026-09-19 | [撤回严格净距并适配战争机器模型大小](../Archive/20260919-撤回严格净距并适配战争机器模型大小.md) | commander, navigation, data, performance | partial | 按用户指示撤回严格净距算法，恢复原Mass避让，只接入战争机器625厘米半径及相应位置间距；源码双目标构建通过，既有回归89通过和2项原有失败。 |
| 2026-09-19 | [Mass体型净距运行时接入与验证记录](../Archive/20260919-Mass体型净距运行时接入与验证记录.md) | commander, navigation, data, performance | partial | 四类单位共享占位、服务器移动约束、生产重试与客户端预测限幅已接入；正常源码构建成功，相关回归89通过和2项既有失败，主要场景及500单位压力观察零净距违规。 |
| 2026-09-18 | [指挥官原生碰撞与轻量间距约束探索](../Archive/20260918-指挥官原生碰撞与轻量间距约束探索.md) | commander, navigation, data | passed | 结合项目与源码版UE5.7确认单开模型碰撞或Crowd解算开关不能修复Mass穿模，建议预测避让加二维成对约束，工程车保留胶囊扫掠。 |
| 2026-09-18 | [指挥官体型净距源表与接入分析完成](../Archive/20260918-指挥官体型净距源表与接入分析完成.md) | commander, navigation, data | passed | 指挥官Excel新增米制模型宽度/净距两列并填写四兵种，完成源表保护校验与Mass/Crowd接入分析，运行时未修改。 |
| 2026-09-18 | [本会话临时文件清理](../Archive/20260918-本会话临时文件清理.md) | art, rendering, commander | passed | 按用户要求移除598个可明确归属的临时文件约568.2MiB，最终视频与审核页哈希保持；正式资源、必要证据、源文件和回退备份保留。 |
| 2026-09-18 | [玩法光照对齐与战争机器拥挤诊断](../Archive/20260918-玩法光照对齐与战争机器拥挤诊断.md) | rendering, art, commander, navigation | partial | 指挥官与空战地图曝光/后处理对齐Demo实际参考并保存读回，战争机器拥挤定位为12.5米模型与3.6米槽位及1.5米半径不匹配；尚未修改间距代码。 |
| 2026-09-18 | [建筑World测试数据与环境更新复跑](../Archive/20260918-建筑World测试数据与环境更新复跑.md) | building, economy, navigation, network | passed | 按用户授权更新既有建筑World测试目录和生命周期环境，独立测试及167项原定回归全部通过；保留退出阶段7条警告，不修改正式建造校验。 |
| 2026-09-18 | [Q圆面打击与赠品清场选兵恢复实现](../Archive/20260918-Q圆面打击与赠品清场选兵恢复实现.md) | combat, commander, building | failed | 射速/射程/散布源表及赠品清场、可靠选兵恢复已实现，源码双目标和导入回读通过；既有回归123通过8失败，LogUtils未定位，新行为仅部分观察。 |
| 2026-09-18 | [松树林原Demo地图全资源适配交付候选](../Archive/20260918-松树林原Demo地图全资源适配交付候选.md) | art, assets, rendering | partial | 直接复制供应商原地图，在原路径适配全包环境资源并完成保存读回、九机位与默认设置补拍；用户整图终验、动态性能和崩溃根因尚待确认。 |
| 2026-09-18 | [松树林v1审核通过与原图原位重构授权](../Archive/20260918-松树林v1审核通过与原图原位重构授权.md) | art, assets, rendering | partial | 用户批准实际v1并要求全量原位适配、用复制的原Demo_Map验收；规范v1.2登记松树林植被例外。 |
| 2026-09-18 | [松树林风格适配v1候选](../Archive/20260918-松树林风格适配v1候选.md) | art, assets, rendering | partial | 在原同场景对照外新增三种松树和草坪的三档材质与疏密候选，原包保留，交付九机位截图与保存内容核对。 |
| 2026-09-17 | [松树林与三单位独立试摆](../Archive/20260917-松树林与三单位独立试摆.md) | art, assets, rendering | partial | 源码UE5.7完成5棵松树、4块岩石、2293簇草与Ship及两兵种同场景候选，真实截图及保存重载读回完成。 |
| 2026-09-17 | [地面机枪5Hz弹丸与僚机弹效复用](../Archive/20260917-地面机枪5Hz弹丸与僚机弹效复用.md) | combat, building, vfx, network | partial | 两种地面机枪由即时伤害改为120m/s、5秒池化碰撞弹丸，地面独立5Hz模拟与同步，复用僚机弹效并采用18m长度及我黄敌红；构建和数据回读通过，既有回归50通过5失败。 |
| 2026-09-17 | [战争机器Q导弹通用预警与切图断言修复](../Archive/20260917-战争机器Q导弹通用预警与切图断言修复.md) | commander, combat, ui, vfx, network | partial | 战争机器Q、独立技能Excel和通用预警已正式接入并完成PIE验证；修复桥接Tick内切图断言，源码构建通过，现有测试19通过2失败。 |
| 2026-09-17 | [自然资源包参考与植被贴片工艺](../Archive/20260917-自然资源包参考与植被贴片工艺.md) | art, assets, rendering | partial | 归档两张原图、34款规格和完整参考板；按用户要求将逐叶逐瓣几何改为v3图集卡片方案，提交可查看技术样板，未越过A/B门禁。 |
| 2026-09-17 | [地面玩法定稿与弹速基线核查](../Archive/20260917-地面玩法定稿与弹速基线核查.md) | combat, building | partial | 记录地面单台高成长高机动机甲、经验升级、弹幕肉鸽与塔防方向；源表与代码显示地面机枪即时扣血、WM01导弹60m/s，尚未实施弹速调整。 |
| 2026-09-17 | [Ship组件制作临时资源回收清理](../Archive/20260917-Ship组件制作临时资源回收清理.md) | art, assets, ship | passed | 按用户指示将847个本会话临时文件及16个空目录移入回收站，共596.32MiB；1245个保留文件哈希一致，正式UE资源未改动。 |
| 2026-09-17 | [Ship全组件表面交付与UE正式替换](../Archive/20260917-Ship全组件表面交付与UE正式替换.md) | art, assets, ship | partial | 完成最后四件与第二批交付，13件正式UE网格替换，14蓝图原引用保留；碰撞回退恢复及保存回读通过，7炮637姿态通过。 |
| 2026-09-17 | [Ship剩余四组件原型与火焰标识参考A](../Archive/20260917-Ship剩余四组件原型与火焰标识参考A.md) | art, assets, ship | partial | 冻结最后四件原型，提交底置双联炮v1、高射速炮v2、燃烧弹舱v1、导弹舱v1效果及三视图；燃烧弹舱两端加入白色火焰参考标志。 |
| 2026-09-17 | [Ship第三批材质B通过与静态FBX交付](../Archive/20260917-Ship第三批材质B通过与静态FBX交付.md) | art, assets, ship | passed | 用户通过第三批实际材质v3，完成三套静态FBX、四份可编辑Blender、九张2K贴图及预览交付，原模型保留和既有回读通过。 |
| 2026-09-17 | [Ship第三批支援组件原模型材质成品审核B](../Archive/20260917-Ship第三批支援组件原模型材质成品审核B.md) | art, assets, ship | partial | A通过后完成三件原模型材质v3与实际Blender审核资料，无人机标识按用户反馈移至薄壁外侧；B待用户。 |
| 2026-09-17 | [Ship干扰装置深蓝白黄参考修订](../Archive/20260917-Ship干扰装置深蓝白黄参考修订.md) | art, assets, ship | partial | 用户指定干扰装置深蓝白黄，完成v3效果图及三视图，取代紫色候选；CIWS与护盾、无人机参考保持。 |
| 2026-09-17 | [Ship干扰与护盾参考配色区分](../Archive/20260917-Ship干扰与护盾参考配色区分.md) | art, assets, ship | partial | 按用户要求区分电子干扰装置与护盾发生器的功能色，提交紫色v2和绿色v3参考，CIWS已审青蓝保持。 |
| 2026-09-17 | [Ship第三批支援组件原型与参考审核A](../Archive/20260917-Ship第三批支援组件原型与参考审核A.md) | art, assets, ship | partial | 完成无人机发射舱、电子干扰装置、护盾发生器原型冻结和参考图板，加入用户指定的无人机飞出标识，提交v2/v1/v2审核A。 |
| 2026-09-17 | [Ship第二批原模型材质制作与成品审核B](../Archive/20260917-Ship第二批原模型材质制作与成品审核B.md) | art, assets, ship | partial | 三件参考A通过后完成原模型材质v2、九张2K贴图、实际视图与俯仰视频，原几何和机械检查通过，提交成品B。 |
| 2026-09-17 | [Ship第二批原型冻结与参考审核A候选](../Archive/20260917-Ship第二批原型冻结与参考审核A候选.md) | art, assets, ship | partial | 完成自动炮、三联炮、单管炮原型只读冻结和十五张中性视图，提交v2/v1/v2效果图及三视图供用户审核A。 |
| 2026-09-17 | [Ship三组件材质审核B通过与FBX交付](../Archive/20260917-Ship三组件材质审核B通过与FBX交付.md) | art, assets, ship | passed | 用户通过三件v4材质审核B；完成两套骨骼FBX、一套静态FBX、可编辑源、贴图和预览交付，原几何保留且回读通过。 |
| 2026-09-17 | [Ship三组件原网格材质与连接分色修正](../Archive/20260917-Ship三组件原网格材质与连接分色修正.md) | art, assets, ship | partial | 用户将范围改为保留原模型、只改材质与三渲二框线；v4完成原几何材质候选并修正连接处误分色，实际渲染与运动材料已可审核。 |
| 2026-09-17 | [Ship三组件原型冻结与参考审核A材料](../Archive/20260917-Ship三组件原型冻结与参考审核A材料.md) | art, assets, ship | partial | 冻结三件原型与机械接口，完成双联炮v1、CIWS v1和Thor v3参考设计、15张原型视图及审核总览；A/B和新模型制作尚未完成。 |
| 2026-09-17 | [进度面板分类筛选与文档分类元数据交付](../Archive/20260917-进度面板分类筛选与文档分类元数据.md) | project | passed | 273 篇归档/需求/开发文档回填 art/gameplay/performance 分类元数据，工具链校验下发，进度面板三个视图上线可多选分类筛选；tsc/lint/build 与浏览器实测全部通过。 |
| 2026-09-17 | [美术规范建立与制作技能接入](../Archive/20260917-美术规范建立与制作技能接入.md) | art, rendering, assets, vfx | passed | 建立独立美术规范v1.0并接入三个制作技能和Progress维护，保留兵种UI原图与战争机器全部参考，清理58个已核对的会话临时产物。 |
| 2026-09-17 | [扫荡者去线稿与爆炸场景明暗调整](../Archive/20260917-扫荡者去线稿与爆炸场景明暗调整.md) | commander, combat, rendering | passed | 扫荡者移除内外线稿并重导入，战争机器线稿保留；地面摧毁爆炸渲染缩至0.6，僚机配置不变；指挥官关卡修正压黑后处理，已保存并通过独立进程读回。 |
| 2026-09-17 | [Ship玩家蓝图补保存与独立读回](../Archive/20260917-Ship玩家蓝图补保存与独立读回.md) | ship, rendering | passed | 用户关闭重复UE窗口后Ship玩家蓝图保存成功；独立进程重新加载并生成实例，确认新船体和描边引用、四套UV、原碰撞与55个挂点保留。 |
| 2026-09-17 | [三渲二模型与指定爆炸接入阶段记录](../Archive/20260917-三渲二模型与指定爆炸接入阶段记录.md) | ship, commander, combat, rendering | partial | 两台手工单位增加内线、轮廓与三档明暗并接入数据表；三类指定爆炸副本已保存配置。Ship新资产已导入，玩家蓝图保存被重复UE进程占用阻塞。 |
| 2026-09-16 | [扫荡者UE导入与战争机器参考重建](../Archive/20260916-扫荡者UE导入与战争机器参考重建.md) | commander, rendering | partial | 扫荡者已导入UE并完成引擎渲染读回；战争机器完成本轮Blender参考重建和FBX包装，游戏LOD与实战接入仍未完成。 |
| 2026-09-16 | [僚机动漫爆炸样板制作与接入](../Archive/20260916-僚机动漫爆炸样板制作与接入.md) | wingman, combat, vfx | partial | 四发射器低模爆炸样板已接入僚机对地轰炸；视觉配置和编译读回通过，完整性能与联机验收待续。 |
| 2026-09-16 | [Ship内部线稿烘焙遮罩](../Archive/20260916-Ship内部线稿烘焙遮罩.md) | ship, assets, art | passed | 将Ship装甲内部实体曲线改为4K灰度遮罩，保留独立外轮廓壳和舰体几何，总三角面降到16,402并更新当前Blender。 |
| 2026-09-16 | [Ship风格样板追加线稿](../Archive/20260916-Ship风格样板追加线稿.md) | ship, assets, art | passed | 按用户反馈为Ship风格样板增加独立深蓝外轮廓和装甲结构线，保存无描边副本并更新Blender与多角度预览。 |
| 2026-09-16 | [Ship动漫低模风格Blender样板交付](../Archive/20260916-Ship动漫低模风格Blender样板交付.md) | ship, assets, art | passed | 保留当前Dreadnought主形状，完成13,314三角面、六色与三档明暗的独立Blender样板，并在当前Blender中显示供用户评审。 |
| 2026-09-16 | [GPU渲染降耗实施与三组对照](../Archive/20260916-GPU渲染降耗实施与三组对照.md) | performance, commander, wingman | partial | 单位低反射材质和FXAA、体积雾、VSM、Lumen配置已落地，三组27份客户端采样决定保留VRS，最终GPU下降17.2%至31.2%。 |
| 2026-09-16 | [客户端CPU修复后六轮复测验收](../Archive/20260916-客户端CPU修复后六轮复测验收.md) | commander, performance, network, ui | passed | Detour阻塞修复后独立重跑1200单位单客和双客各三轮，九份客户端均通过实际移动人数与既有P95门槛，服务端零丢步；源码双目标与42项回归再次通过。 |
| 2026-09-16 | [据点矿厂落点与Detour避让卡顿修复](../Archive/20260916-据点矿厂落点与Detour避让卡顿修复.md) | building, navigation, commander, performance | passed | 修复奖励矿厂坡道断言与 Detour 零半径查询热点；25 据点奖励、53 项回归和六轮 1200 人持续移动通过，保存源码引擎补丁。 |
| 2026-09-16 | [客户端CPU增量维护与10Hz刷新实施](../Archive/20260916-客户端CPU增量维护与10Hz刷新实施.md) | commander, performance, network, ui | failed | 名册索引、实例池增量维护、原生ISM更新、Mass批次和UI缓存已实施，源码双目标与42项自动化通过；前后六轮实测移动负载不足，整体性能验收未通过。 |
| 2026-09-15 | [导航预烘焙与 PIE 启动优化验收](../Archive/20260915-导航预烘焙与PIE启动优化.md) | navigation, commander, resources, performance | passed | 地面及空中导航自动判新、重烘焙保存和 Cook 门禁完成；500 士兵双客户端主端启动三轮中位数由 124.080 秒降至 22.443 秒，动态等待由 116.635 秒降至 12.007 秒。 |
| 2026-09-15 | [工程车动态避让与建筑矿体导航修复](../Archive/20260915-工程车动态避让与建筑矿体导航修复.md) | commander, resources, building, navigation | partial | 两种工程车接入共享Crowd，建筑与矿体恢复真实导航占位；修正默认部署和采矿落点容差，双目标及44项既有测试通过，双端常规观察完成。 |
| 2026-09-15 | [指挥官旧姿态协议残留清理](../Archive/20260915-指挥官旧姿态协议残留清理.md) | commander, network, performance | passed | 清除v8姿态类型反射、旧命名/锚点/静默整理和测试直通入口；v9格式不变，源码双目标、65项回归及1200单位双客户端联调通过。 |
| 2026-09-15 | [指挥官姿态压测移动人数口径勘误](../Archive/20260915-指挥官姿态压测移动人数口径勘误.md) | commander, network, performance | passed | 补齐六轮共同30秒内逐帧移动人数平均与最低值，撤回最大差异1人的过强表述；1人差异只适用于P05，六轮共同窗口均实际执行300个固定步。 |
| 2026-09-15 | [指挥官姿态协议9压缩与六轮对比](../Archive/20260915-指挥官姿态协议9压缩与六轮对比.md) | commander, network, performance | passed | 正式姿态协议升级9，完成指定精度、预测差分和字段增量；六轮1200人同机比较下行减少64.15%，CPU变化混合，65项回归和源码版双目标通过。 |
| 2026-09-15 | [阵营独立通道与点击据点即时运输修复](../Archive/20260915-阵营独立通道与点击据点即时运输修复.md) | commander, building, economy, map, data, network, vfx | partial | 红蓝有效节点独立成树，工程车点击己方据点模型原地运输；源码双目标与33项既有测试通过，双端点击、载货改路、拥堵、基地回退和部分重连已观察。 |
| 2026-09-15 | [指挥官移动下行数据占比实测](../Archive/20260915-指挥官移动下行数据占比实测.md) | commander, network, performance | partial | 同一1200人双客移动场景补采网络事件，按共同30秒窗口测得每客187.43KB/s，姿态RPC占89.72%；连接累计字节与解析结果交叉核对。 |
| 2026-09-15 | [客户端CPU-GPU剖析与压测相机勘误](../Archive/20260915-客户端CPU-GPU剖析与压测相机勘误.md) | commander, network, performance | partial | 纠正旧压测相机未锁定的对照条件；重采1200人一客及双客CPU/GPU时间线，确认同机双客主要受渲染链影响，定位实例更新和小地图绘制耗时。 |
| 2026-09-15 | [指挥官10Hz循环与移动容量实测](../Archive/20260915-指挥官10Hz循环与移动容量实测.md) | commander, network, performance | partial | 10Hz权威步及每步单兵移动落地，Editor/Game和61项既有回归通过；纯服务端4000档、在线1200档双客30FPS取得有效结果，并定位名册和姿态复制瓶颈。 |
| 2026-09-15 | [空战原型关卡开局安装僚机仓](../Archive/20260915-空战原型关卡开局安装僚机仓.md) | ship, wingman | passed | 空战原型关卡通过专用GameMode提交08开局构筑；冷启动和自动重生均为两仓、一能力、25架僚机，构筑记录不重复增长。 |
| 2026-09-15 | [Ship组件能力与指挥官技能去GAS重构](../Archive/20260915-Ship组件能力与指挥官技能去GAS重构.md) | ship, commander, wingman, combat, network | passed | 移除Ship和指挥官GAS，建立构筑规则/装配能力及兵种Q/全局战术技能；双目标构建、42项自动测试、两种PIE网络模式和冷启动审计通过。 |
| 2026-09-14 | [空中通道联机与特效增量验收](../Archive/20260914-空中通道联机与特效增量验收.md) | commander, building, economy, network, vfx | partial | 补齐空中行程三个时刻、载货断路、全部已过节点失效回基地、出口拥堵和两种重连观察；最终双目标构建与BuildId通过，整体边界和600单位性能仍未通过完整验收。 |
| 2026-09-14 | [指挥官建筑闭环与空中通道首轮实现](../Archive/20260914-指挥官建筑闭环与空中通道首轮实现.md) | commander, building, economy, map, data, network, vfx | partial | 完成建筑数据与组件闭环、建造车、端点占领和赠品、维护生产及空中运输；双车施工、换主结算、供盾与运输分流有实测，旧建筑测试与600单位性能未通过。 |
| 2026-09-14 | [僚机直线激光炮弹池与最终数值](../Archive/20260914-僚机直线激光炮弹池与最终数值.md) | combat, wingman, vfx | partial | 僚机对空机枪接入30Hz数据槽位炮弹池，最终800米每秒、长30米、内芯宽0.5米；源码双目标与实机通过，既有检查19/22通过。 |
| 2026-09-14 | [法术场统一入口与武器ID引用](../Archive/20260914-法术场统一入口与武器ID引用.md) | combat, commander, wingman, data | passed | 将两条武器爆炸配置归回SpellFields，次级武器通过数字法术场ID引用；保持原数值，完成源码编译、导入回读和3项既有自动化。 |
| 2026-09-14 | [次级单位武器独立Excel重构](../Archive/20260914-次级单位武器独立Excel重构.md) | combat, commander, wingman, data | partial | 次级单位武器统一迁入SecondaryWeapons工作簿；源引擎Editor/Game构建及7表原生回读通过，现有回归20/24通过，4项失败保留。 |
| 2026-09-14 | [飞船组件文档资源引用与覆盖盘点](../Archive/20260914-飞船组件文档资源引用与覆盖盘点.md) | ship, assets | passed | 外部组件方案文档14处模型资源引用改为现有组件蓝图，盘点14个蓝图中有11个被引用，另3个尚未写入方案。 |
| 2026-09-14 | [单位铁锈残骸与僚机物理坠落](../Archive/20260914-单位铁锈残骸与僚机物理坠落.md) | combat, vfx, commander, wingman | partial | 单位死亡显示铁锈报废外观；僚机复制原速度进行Chaos坠落，Landscape碰撞当帧销毁。源码双目标与现有5项回归通过。 |
| 2026-09-14 | [冲击波外径上限与重复缩放修正](../Archive/20260914-冲击波外径上限与重复缩放修正.md) | combat, vfx, wingman | partial | 单位销毁冲击波最大外径限制为主体峰值2.5倍，消除Aerial与Big_17折射网格重复应用Owner.Scale；主体与伤害不变。 |
| 2026-09-13 | [僚机空爆随机二选一与销毁特效五倍](../Archive/20260913-僚机空爆随机二选一与销毁特效五倍.md) | combat, vfx, wingman | partial | 僚机销毁改为Aerial 3/4随机二选一并保留冲击波；共享模型比例与整体5倍规则不变，源码双目标和现有5项回归通过。 |
| 2026-09-13 | [Soldiers统一单位与全局法术场及矿车调度](../Archive/20260913-Soldiers统一单位与全局法术场及矿车调度.md) | data, resources, combat, commander, ship, vfx, network | partial | 矿车纳入Soldiers，共用独立法术场Excel，新增节点分配Manager；三倍速度、并行进厂上传、瞬时转向及原始五倍激光完成。 |
| 2026-09-13 | [激光采矿与矿车进出厂及受击接入](../Archive/20260913-激光采矿与矿车进出厂及受击接入.md) | resources, economy, combat, vfx, network | partial | 保留原车网格的18米矿车接入双54米绿色激光与真实扣矿，完成整车进厂一秒上传、后侧掉头、正向出厂和命令延后；组合通用真实HP及受击反馈。 |
| 2026-09-13 | [单位受击血条与模型尺寸爆炸缩放](../Archive/20260913-单位受击血条与模型尺寸爆炸缩放.md) | combat, vfx, ui, commander, wingman, ship | partial | 共链受击血条显示3秒后渐隐，摧毁爆炸以最小兵种观感为基准按实际模型尺寸缩放；僚机Big_17主体5倍、冲击波保留3.1倍，随机朝向与3秒回池不变。 |
| 2026-09-13 | [僚机Big17爆炸范围缩放修正](../Archive/20260913-僚机Big17爆炸范围缩放修正.md) | wingman, combat, vfx | passed | 将僚机Big_17爆炸从1倍修正为3.1倍，使主火球约80米直径匹配40米AOE半径；双客户端PIE已验证缩放、随机Yaw和3秒回池。 |
| 2026-09-13 | [僚机对地轰炸Big17爆炸特效](../Archive/20260913-僚机对地轰炸Big17爆炸特效.md) | wingman, combat, vfx, network | passed | 僚机对地轰炸命中改用指定Big_17的项目副本；每次按复制效果种子随机Yaw，并以3秒为表现硬清理上限，Commander引用保持不变。 |
| 2026-09-13 | [单位受击白光与摧毁爆炸](../Archive/20260913-单位受击白光与摧毁爆炸.md) | combat, vfx, commander, wingman, ship, resources | partial | 实现0.5秒白色发光衰减及NPC随机爆炸和冲击波；单位透明度不变，源码构建和PIE表现入口观察完成。 |
| 2026-09-12 | [ShipComponent部件蓝图与占位清理](../Archive/20260912-ShipComponent部件蓝图与占位清理.md) | ship, assets | passed | 14个正式部件蓝图替换旧占位目录，82个Socket完整保留；冷启动配置校验、源码Editor构建及既有2项部件验收通过。 |
| 2026-09-12 | [空战测试关卡远端地面单位](../Archive/20260912-空战测试关卡远端地面单位.md) | ship, commander, level | passed | 空战原型关卡远端新增4个持久地面部署点，普通PIE自动生成红蓝各24个真实Mass单位，源码构建与双客户端检查通过。 |
| 2026-09-12 | [通用敌方描边与普通PIE僚机同步修复](../Archive/20260912-通用敌方描边与普通PIE僚机同步修复.md) | wingman, ship, presentation, network | partial | 敌方Ship与僚机使用通用红色轮廓；修复普通PIE姿态断流、模型时钟和全灭残留，两次独立开局90秒/60秒采样无异常模型速度帧。 |
| 2026-09-12 | [Client1僚机冻结与可靠快照风暴修复](../Archive/20260912-Client1僚机冻结与可靠快照风暴修复.md) | wingman, networking, presentation, combat | partial | 非致死伤害不再触发整组可靠Bootstrap，真实成员切片保留未变Remote的Pawn与插值历史；Client1从24/24远端全停恢复为25/25持续移动。 |
| 2026-09-12 | [僚机视觉插值与远端稳定显示](../Archive/20260912-僚机视觉插值与远端稳定显示.md) | wingman, networking, presentation | partial | 僚机逻辑根仍以30Hz确定推进，Owner模型改为逐渲染帧插值；Remote回看改为0.2秒且陈旧姿态保持可见，双客户端样本25/25全程可见。 |
| 2026-09-12 | [矿车 Dock、队伍私有代理与 500 人姿态流修复](../Archive/20260912-矿车Dock私有代理与500人姿态流修复.md) | economy, commander, navigation, network | passed | 矿车改为停靠工厂障碍外 DockPoint，队伍私有状态改由 OwnerOnly PlayerState 聚合代理复制，客户端矿厂表现已恢复；500 人姿态流完成稳定相位、降频和自适应插值，弱网下实测 56920/59818 B/s（平均/P95）。 |
| 2026-09-11 | [资源经济、动态障碍与 Commander 适配层解耦](../Archive/20260911-资源经济障碍与Commander适配层解耦.md) | architecture, economy, commander, navigation | passed | 将团队经济账本、动态障碍发布和 Commander 资源交互从资源世界拆成三个通用边界，既有玩法与测试用例保持不变，范围自动化、双客户端 PIE 及三类 Target 均通过。 |
| 2026-09-11 | [红蓝矿棋盘与自动采矿闭环](../Archive/20260911-红蓝矿棋盘与自动采矿闭环.md) | economy, map, commander, navigation, network | passed | 5×5 Territory、240 个固定矿簇、服务器权威采运加工、三秒人工接管、建筑蓝矿事务和整簇动态导航恢复已落地；专项 6/6、相关回归 60/60、双客户端 PIE 与 Cooked 启动通过。 |
| 2026-09-11 | [Ship 僚机对空攻击盘旋冷却改造](../Archive/20260911-Ship僚机对空攻击盘旋冷却改造.md) | wingman, ship, combat, network | passed | 对空旧随机往返缠斗已替换为攻击—返舰盘旋冷却循环；v14持续命中链以单次开始记录驱动，专项12/12、双客户端PIE、数据部署和Editor/Game构建通过。 |
| 2026-09-11 | [飞船骨骼部件接入与Socket保留](../Archive/20260911-飞船骨骼部件接入与Socket保留.md) | ship, assets, combat | passed | 现有装配系统支持静态和骨骼部件，4个旧部件蓝图配置保留；75个Socket重载核对一致，Editor/Game源码构建及2项定向验收通过。 |
| 2026-09-11 | [指挥官传送配置归并与范围扩展](../Archive/20260911-指挥官传送配置归并与范围扩展.md) | commander, data, vfx | partial | 传送配置并入SpellFields，四档扩大为40/100/200/500米，光柱升至500米并增加柔边渐变；Editor/Game源码构建通过。 |
| 2026-09-11 | [指挥官双点传送技能实施与验收](../Archive/20260911-指挥官双点传送技能验收.md) | commander, ship, wingman, combat, network, ui, vfx | passed | 法术场统一管理Mass、Actor和僚机名单，蓝色半透保留原单位与镜头；记录构建、边界、联机和视觉证据。 |
| 2026-09-10 | [双矿单位矿模型 UE 导入交付](../Archive/20260910-双矿单位矿模型UE导入交付.md) | economy, art | passed | 向源码版UE5.7导入并保存24个蓝红单位矿静态网格和4个材质，尺寸、枢轴、面数、顶点色、材质槽与凸包碰撞核对通过，已观察引擎内蓝红显示。 |
| 2026-09-10 | [地图资源密度涂绘与确定性导出](../Archive/20260910-地图资源密度涂绘与确定性导出.md) | map-authoring, resource, outpost, data-pipeline | partial | GuLiMapAuthoring 0.2.0 已加入红蓝矿稀疏密度笔刷、Territory 归属统计、原子编辑接口和三份确定性导出；源码版构建、BuildId 核对及 10/10 自动化通过。 |
| 2026-09-10 | [双矿单位矿模型与 Blender 审核交付](../Archive/20260910-双矿单位矿模型与Blender审核交付.md) | economy, art | passed | 交付蓝红各四家族三状态共24个可编辑单位矿模型、原生制作脚本、12张正式预览与双矿静态拼装，技术核验通过，等待用户美术审核。 |
| 2026-09-10 | [游戏内 GM 分页浮层面板](../Archive/20260910-游戏内GM分页浮层面板.md) | ui, commander, combat, network | partial | 已交付非 Shipping F10/Esc 右侧 GM 分页浮层、类型化权限模型和角色输入恢复；Editor/Game Development、BuildId 与 33 项自动化通过，人工 PIE 待补。 |
| 2026-09-10 | [僚机飞行双尾焰与拖尾接入](../Archive/20260910-僚机飞行双尾焰与拖尾接入.md) | wingman, ship, vfx | partial | 完成双发动机蓝白尾焰和空间拖尾，Niagara与材质编译通过，Standalone PIE 25架僚机挂接并激活，源码版Editor与Game构建通过，用户确认符合预期。 |
| 2026-09-10 | [Ship僚机取消固定6秒轰炸进场](../Archive/20260910-Ship僚机取消固定6秒轰炸进场.md) | ship, wingman, combat, navigation | passed | 定位并修复双倍攻击速度下固定6秒进场点越过FlightNav顶部、导致所有地面轰炸被拒绝的问题；删除固定航段后源码构建、Attack 12/12、完整Wingman 96/96及同图PIE复验通过。 |
| 2026-09-10 | [Ship僚机战斗表现、HUD与相机调整实施](../Archive/20260910-Ship僚机战斗表现HUD与相机调整.md) | ship, wingman, combat, ui, vfx | partial | 完成Ship屏幕状态HUD、信息面板可读性、Dreadnought与僚机调速及僚机机动性增强、僚机模型/导弹/爆炸表现和摄像机全走廊防穿地形；源码构建及定向验证通过，交互式多分辨率/联机性能矩阵待补。 |
| 2026-09-09 | [Ship僚机俯冲轰炸门槛简化](../Archive/20260909-Ship僚机俯冲轰炸门槛简化.md) | wingman, ship, combat, network | passed | 取消对地轰炸进入/维持Dive的精确姿态和航线门槛；源码构建与11项自动化通过，Standalone PIE 24秒采样导弹366增至567且Lineup持续为0。 |
| 2026-09-09 | [资源加工厂 UE 正式资源接入与验收](../Archive/20260909-资源加工厂UE正式资源接入与验收.md) | building, art | passed | 完成门物理资产、正式控制蓝图和演示地图；真实 PIE 验证反向、重复调用、完成事件及通行，交付 UE 截图和视频。 |
| 2026-09-09 | [Ship 僚机客户端 Pawn 与逐架 StateTree 重构实施](../Archive/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md) | wingman, ship, combat, network, ai | partial | 完成僚机客户端Pawn、逐架UE StateTree、Actor表现池和客户端姿态转发边界；持续飞行、对地、性能、协议与构建通过，空战每成员两轮开火留作后续问题。 |
| 2026-09-09 | [Ship 僚机全员参战与索敌范围显示](../Archive/20260909-Ship僚机全员参战与索敌范围显示.md) | wingman, ship, combat | passed | 修正逐成员目标已分配但部分僚机无法建立对地攻击轮的问题；25/25成员已在同图PIE进入攻击轮，本地Ship新增实际获取半径的青色三维线框。 |
| 2026-09-09 | [资源加工厂 Blender 模型与门动画阶段交付](../Archive/20260909-资源加工厂Blender模型与门动画阶段交付.md) | building, art | partial | 完成并打开 Blender 资源加工厂与门开合预览；基础 UE 导入后按用户要求暂停其余 UE 工作。 |
| 2026-09-08 | [2026-09-08 完成了 Ship 僚机空地统一匈牙利自动选敌](../Archive/20260908-Ship僚机空地统一匈牙利自动选敌.md) | wingman, ship, combat, network | passed | 每艘Ship现以服务器多轮匈牙利算法统一分配空地目标，逐成员执行与v12开火授权已接通；27项定向自动化、300秒PIE长测、源码版Editor/Game构建和BuildId门禁通过。 |
| 2026-09-08 | [2026-09-08 修复了 Ship 僚机在 FlightNav 边界停住不动](../Archive/20260908-Ship僚机FlightNav边界停滞恢复.md) | wingman, ship, navigation, network | passed | 僚机在最后合法点刹停后可瞬时对准冻结逃逸方向；源码版构建、攻击与Relay专项、主动对空250.63秒和对地85.83秒逐架采样均通过，最长停滞0秒。 |
| 2026-09-08 | [2026-09-08 完成了 Ship 空中部队原型关卡与三倍航速](../Archive/20260908-Ship空中部队原型关卡与三倍航速.md) | ship, wingman, level, navigation, data-pipeline | partial | 新建可由玩家驾驶带25架僚机飞船的独立空战原型地图，完成地图专属 FlightNav 与 Cook 配置，并把 Dreadnought 空装配有效最大航速从900提高到2700cm/s。 |
| 2026-09-07 | [2026-09-07 勘误：Ship僚机最终构建恢复源码版 UE5.7 门禁](../Archive/20260907-Ship僚机源码版构建门禁勘误.md) | wingman, ship, build, network | partial | 更正先前误用 Launcher UE5.7 的最终构建证据；已用 D:\UnrealEngine-5.7 重编 Editor/Game 目标、统一全部项目插件 BuildId，并由源码版命令程序通过7项攻击专项。 |
| 2026-09-07 | [2026-09-07 解决了：地图标记删除后视口绘制空指针崩溃](../Archive/20260907-地图标记删除崩溃空指针修复.md) | map-authoring, commander, data-pipeline, assets | partial | 用户在正式地图 /Game/Maps/LVLCommanderMassPrototype 的“地图标注”面板中选中 Generic 标记，点击“删除”后出现 Fatal error。截图中的业务键为 Generic1E058BA042E28910A9D69D870503F146。本次没有在正式地图中重试删除，也没有改写该地图、恢复自动保存或操作其他任务打开的编辑器 |
| 2026-09-07 | [2026-09-07 解决了：地图标注项目插件首版实现与验证](../Archive/20260907-地图标注插件首版实现与验证.md) | map-authoring, network, data-pipeline, assets | partial | 以下源码路径均相对 Plugins/GuLiMapAuthoring/；两个模块都是 Editor，依赖方向只有 Editor → Core，没有 GuLiStrike 玩法依赖 |
| 2026-09-07 | [2026-09-07 解决了：地图标注插件维护 Skill 建立](../Archive/20260907-地图标注插件维护Skill.md) | map-authoring, ui, network, data-pipeline, assets | not_run | 创建项目内自动发现的 $guli-map-authoring，用于后续回答和执行以下任务：地图标注面板使用、配置类型和 Property Bag 字段、多个命名区域及四种几何、JSON/CSV 输出和 Python API、插件故障诊断、源码维护、原生扩展、构建测试与交付归档 |
| 2026-09-07 | [2026-09-07 解决了：WM01 副本参考骨架不同步和物理包围盒放大约百倍](../Archive/20260907-WM01骨架同步与物理包围盒修复.md) | commander, network, combat, assets | partial | CRWM01、FourFRobot、商城源资产、关卡、Crowd、运行时开火配置、导弹和伤害逻辑均未由本次修改。没有新增自动化测试文件或扩展战斗测试 |
| 2026-09-07 | [2026-09-07 解决了：Ship僚机按目标执行俯冲轰炸或前向机枪盘旋](../Archive/20260907-Ship僚机俯冲轰炸与对空机枪落地.md) | wingman, commander, ship, ui, network | passed | 下列源码路径均相对项目根目录，表格中的目录与文件名一起组成准确路径 |
| 2026-09-07 | [2026-09-07 解决了：Ship 僚机以三维往返缠斗替换目标点盘旋](../Archive/20260907-Ship僚机三维往返缠斗与随机转向.md) | wingman, commander, ship, network, data-pipeline | partial | 证据：聚合统计、状态时间线、专项自动化、部署回读、斜视画面、俯视画面 |
| 2026-09-06 | [2026-09-06 解决了：指挥官独立武器表现与服务器法术场运行层](../Archive/20260906-指挥官武器特效与独立法术场.md) | building, wingman, commander, ship, network | not_run | 服务器取得 UGuLiCombatEffectRuntimeSubsystem 后，普通 Actor/蓝图和 Commander 均可调用 |
| 2026-09-06 | [2026-09-06 阶段记录：双机甲骨骼副本与绑定问题核查](../Archive/20260906-双机甲骨骼副本与绑定问题核查.md) | commander, network, data-pipeline, assets | partial | 需用户确认是否允许在项目副本上局部重绑 WM01 武器，并校准骨骼绑定尺度/机械轴心。原 Marketplace 内容保持只读；既有有效腿部蒙皮尽可能保留，参考外观保持不变。批准后继续完整 Rig/Socket 制作与原方案规定的摆姿验收 |
| 2026-09-06 | [双机甲尺度修复与 Control Rig 草稿](../Archive/20260906-双机甲尺度修复与ControlRig草稿.md) | commander, ui, data-pipeline, combat, assets | partial | WM01 关节全局平移 ×1000、全局尺度归一为 1，重建逆绑定矩阵。顶点位置未改。FourF 保留原绑定，通过 NodeMappingContainer 桥接 Rig 别名与原变形骨名 |
| 2026-09-06 | [2026-09-06 解决了：为两兵种添加可供用户精调的 Socket](../Archive/20260906-双机甲可调Socket创建.md) | commander, data-pipeline, vfx, combat, assets | partial | 通过源码 UE5.7.4、现有端口 12029 的 VibeUE SkeletonService.addsocket(..., addtoskeleton=False) 创建挂点。只保存两个明确修改的 SKM 资产，未改原骨、蒙皮、独立 Skeleton 或 Control Rig 图 |
| 2026-09-05 | [2026-09-05 解决了：将据点巨构参考落为可编辑的 Blender 首版模型](../Archive/20260905-据点混凝土巨构模型首版.md) | building, network, data-pipeline, assets | not_run | 2026-09-05 解决了：将据点巨构参考落为可编辑的 Blender 首版模型 |
| 2026-09-05 | [2026-09-05 解决了：300m 混凝土巨构导入 UE 并替换据点占位资源](../Archive/20260905-据点巨构替换占位资源.md) | building, ui, network, data-pipeline, combat | passed | 以上涉及 8 个 UE 资产包；原 MOutpostPlaceholder 保留。模型源 .blend 和导出 FBX/GLB 沿用上一个建模任务成果 |
| 2026-09-05 | [僚机 FlightNav 弧线验证与局部恢复](../Archive/20260905-僚机FlightNav弧线验证与局部恢复.md) | wingman, commander, network, assets | partial | 初始版本有两个连续阶段：僚机先成组向母舰内侧掉落，随后某些 Flight 不再移动；Candidate 长时间没有新接纳后，租约进入 Stale/Unavailable，旧表现超时策略又把存活僚机隐藏，所以最终看起来像“全部消失” |
| 2026-09-04 | [2026-09-04 解决了：飞船 HUD 贴屏、中央准星样式单一且无法由技能接管](../Archive/20260904-飞船世界空间环绕HUD与技能准星.md) | ship, ui, network, combat, assets | partial | 2026-09-04 解决了：飞船 HUD 贴屏、中央准星样式单一且无法由技能接管 |
| 2026-09-04 | [2026-09-04 解决了：指挥官相机越过起伏地面时上下跟随并改变平移速度](../Archive/20260904-指挥官相机稳定巡航.md) | commander, ship, network, assets | partial | 只有完成上述矩阵后，才能把配对开发文档状态从“实施中”改为“已完成” |
| 2026-09-04 | [2026-09-04 解决了：指挥官大规模移动的NavMesh、手工分离与Mass预测避让持续负载过高](../Archive/20260904-指挥官大规模移动导航与避让降载.md) | commander, network, combat, assets | partial | 2026-09-04 解决了：指挥官大规模移动的NavMesh、手工分离与Mass预测避让持续负载过高 |
| 2026-09-04 | [2026-09-04 解决了：僚机跟随船头旋转、编队过远与运行期租约无恢复重试](../Archive/20260904-僚机世界空间近距编队与租约恢复.md) | wingman, commander, ship, network, assets | partial | 2026-09-04 解决了：僚机跟随船头旋转、编队过远与运行期租约无恢复重试 |
| 2026-09-04 | [Ship UI v1：NEONCTRL 局内静态 UI](../Archive/20260904-ShipUIv1-NEONCTRL静态UI.md) | commander, ship, ui, data-pipeline, assets | partial | 完成了局内 HUD 的 NEONCTRL 风格落地：新建纯静态 /Game/Ship/UI/Widgets/WBPShipHUD，并在不改变原控件树、输入岛或 Native 查找合同的前提下原位换肤 /Game/Commander/UI/Widgets/WBPCommanderHUD |
| 2026-09-03 | [2026-09-03 解决了：飞船 GAS、僚机体系与三维导航实现与既有验收（总归档）](../Archive/20260903-飞船GAS僚机体系与三维导航实现与既有验收总归档.md) | wingman, commander, ship, network, combat | passed | Runtime 模块不依赖 GuLiStrike、GAS、Team 或 Wingman 类型；业务层只通过公共导航数据和查询接口消费 |
| 2026-09-03 | [2026-09-03 解决了：完成指挥官与地面战争机器最小建造系统](../Archive/20260903-指挥官与地面战争机器最小建造系统.md) | building, commander, ui, network, combat | partial | 2026-09-03 解决了：完成指挥官与地面战争机器最小建造系统 |
| 2026-09-03 | [2026-09-03 解决了：Ship UI v1 Figma 玩家实机成品稿](../Archive/20260903-ShipUIv1-Figma玩家实机稿.md) | commander, ship, ui, network, assets | passed | 2026-09-03 解决了：Ship UI v1 Figma 玩家实机成品稿 |
| 2026-09-01 | [2026-08-31～09-01 解决了：指挥官选兵移动导航与小兵 GAS 扫射（总归档）](../Archive/20260901-指挥官选兵移动导航与小兵GAS扫射总归档-0831至0901.md) | commander, network, combat, assets, learning | partial | 旧实现确实仍在绘制压扁圆柱：前一轮主要优化兵模，按当时要求保留脚环常显。旧材质双面、半透明、关闭深度测试，圆盘内部通过两个 SphereMask 相减隐藏，透明区域和重复表面仍有渲染成本 |
| 2026-08-31 | [2026-08-31 解决了：UE4 网络概念教程改编为 UE 5.7 源码版（教材新增理论篇）](../Archive/20260831-网络教材理论篇改编.md) | network, assets, learning | not_run | 用户提供知乎文章全文（Jerish《关于网络同步的理解与思考[概念理解]》，基于 UE4），要求改编为 UE5 版本存放于 UE网络教材/，保留原文行文风格与通用部分。改编原则：章节结构、问答体例、示例（NPC 宝藏多播、宝箱属性回调、Gate 粒子、ClientRestart 骑乘 NPC）与口语化行文保留原文；所有涉及引擎行为的论断逐条到 C:\Program Files\Epic Ga… |
| 2026-08-31 | [2026-08-31｜UE 网络教材与 Mass 精读笔记同步修订](../Archive/20260831-网络教材与Mass精读笔记同步修订.md) | commander, ship, network, learning | partial | 本轮共写入 19 份 Markdown：16 份既有教材/笔记、1 份新增 Mass README、Progress 索引和本归档。没有写入源码、配置、蓝图、地图、旧归档或范围外文档，也没有创建临时脚本、备份及散落报告 |
| 2026-08-31 | [2026-08-31 解决了：将基地建造玩法探索草案保存到项目并建立索引](../Archive/20260831-基地建造玩法探索草案落档.md) | building, assets | partial | 2026-08-31 解决了：将基地建造玩法探索草案保存到项目并建立索引 |
| 2026-08-31 | [2026-08-31 公共战局框架与三类角色接入](../Archive/20260831-公共战局框架与三类角色接入.md) | commander, ship, ui, network, assets | partial | 源码本轮修改/新增共 45 个 .h/.cpp，蓝图资产修改 4 个。没有新增地图、输入资产、测试文件、演示 Actor 资产或插件；没有更改全局默认地图。保存了工作区原有改动，不回退历史草稿之外的用户内容；不修改旧归档 |
| 2026-08-31 | [2026-08-31 解决了：补齐项目网络链路中文注释并编写实战教材](../Archive/20260831-UE网络中文注释与项目教材.md) | commander, network, learning | partial | 基于实施时工作区保存内存基线，保留此前已有的 C++、UI、协议及其他未提交变更。覆盖 33 个源文件，仅补充/翻译中文注释并整理 64 处赋值运算符后的悬空换行；参数仍允许分行 |
| 2026-08-30 | [2026-08-30 解决了：战斗权威子系统缺少中文阅读说明及赋值后换行影响阅读](../Archive/20260830-战斗权威子系统中文注释与导读.md) | commander, network, combat, assets, learning | passed | 2026-08-30 解决了：战斗权威子系统缺少中文阅读说明及赋值后换行影响阅读 |
| 2026-08-29 | [指挥官小兵表现层两阶段性能优化](../Archive/20260829-指挥官小兵表现层两阶段性能优化.md) | commander, ship, network, data-pipeline, combat | passed | 首次通过 MCP 的 GameThread TaskGraph 回调同步执行 Material Merge 时，MaterialBaking 为等待纹理流送再次泵 GameThread TaskGraph，触发 TaskGraph recursion guard；改用 CoreTicker 仍存在 MaterialBaking 内部 nested CoreTicker 的重入风险。最终命令通… |
| 2026-08-29 | [2026-08-29 解决了：指挥官 UI 视觉三轮设计与 HUD 逻辑接入（总归档）](../Archive/20260829-指挥官UI与HUD逻辑接入总归档.md) | commander, ui, assets | passed | 2026-08-29 解决了：指挥官 UI 视觉三轮设计与 HUD 逻辑接入（总归档） |
| 2026-08-28 | [2026-08-28 归档：指挥官 3C、Soldier 数据化与运行时 GM 调参](../Archive/20260828-指挥官3C与运行时GM调参.md) | commander, ship, ui, network, data-pipeline | partial | 本轮把 Commander 原型从“代码常量 + 终点恢复方阵 + 固定朝向小地图”推进为“单兵种表驱动 + 共享目标松散到达 + heading-up 小地图”，并补上当前 World 会话级 gs.GM. 调参系统 |
| 2026-08-28 | [2026-08-28 解决了：Commander 的 Order 中文术语统一](../Archive/20260828-Commander指令术语统一.md) | commander, network, assets, learning | not_run | 2026-08-28 解决了：Commander 的 Order 中文术语统一 |
| 2026-08-27 | [2026-08-27 解决了：飞船与场景模型尺寸归一](../Archive/20260827-飞船与场景模型尺寸归一.md) | ship, network, data-pipeline, assets | not_run | 场景中三组已人工标定的实例缩放已烘焙到模型资源，关卡 Actor Scale 全部归一为 1；当前玩家飞船仅在子蓝图取消根组件 0.3 倍缩放，父蓝图保持不变。运行时飞船使用缩小为原资源 0.5 的 Dreadnought 舰体，因此最终尺寸为改动前的 5/3，并同步更新舰体偏移、相机参数和出生高度 |
| 2026-08-27 | [爆炸特效统一归拢至 /Game/Assets/VFX/Explosions](../Archive/20260827-爆炸特效统一归拢至AssetsVFXExplosions.md) | ship, ui, data-pipeline, vfx, assets | partial | 日期：2026-08-27 执行方式：UnrealMCPython 实时通道 + 独立编辑器进程 -ExecutePythonScript |
| 2026-08-27 | [2026-08-27 总归档：Mass 动态 25 人控制组与双端平滑同步](../Archive/20260827-Mass动态25人控制组与双端平滑同步-总归档.md) | commander, ui, network, data-pipeline, assets | partial | 本轮把早期“永久 25 人 CommandGroup + 5Hz 组摘要直驱客户端”的原型，迁移为“500 名永久独立 Soldier + 每次选择临时生成最多 25 人 ControlCohort + 每次命令生成独立 OrderFormation”的服务端权威架构 |
| 2026-08-25 | [2026-08-24~25 总归档：飞船 3C 与相机避障体系（滚轮缩放 / 舰体避障六轮演进 / 命中过滤）](../Archive/20260825-飞船3C与相机避障总归档-0824至0825.md) | ship, data-pipeline, assets | partial | 2026-08-24~25 总归档：飞船 3C 与相机避障体系（滚轮缩放 / 舰体避障六轮演进 / 命中过滤） |
| 2026-08-25 | [2026-08-25 解决了：相机避障逻辑提取为 ResolveCameraArmCollision()，Tick 去散落逻辑](../Archive/20260825-相机避障函数化-Tick去散落逻辑.md) | ship | passed | 应用户要求"相机避障封装成函数、Tick 不留散落逻辑"。这是此前"避障下放专职组件"评估的轻量替代：不动组件归属、不改数据流，只做函数化收拢——DesiredArmLength/HullBoundingRadius/各 Camera 参数仍为飞船成员，将来若真做组件化，这个函数就是现成的搬迁单元。Tick 现在读作：避障 → 偏航惯性 → 自动转向 → 压弯 → 清输入，五段一目了然 |
| 2026-08-25 | [2026-08-25 解决了：相机/避障参数进表（新增 Camera sheet）+ 蓝图覆盖值清理](../Archive/20260825-相机参数进表Camera-sheet与BP覆盖清理.md) | ship, network, data-pipeline, assets | partial | ApplyCameraRow() 语义与 ApplyTuningRow() 一致：无表/无预设/无行 → 保持类默认 + Warning；表值覆盖后把 SpringArm-TargetArmLength 同步为 CameraDefaultArmLength（随后 DesiredArmLength 从臂长起步的初始化自然吃到表值） |
| 2026-08-24 | [2026-08-22~24 第二轮资产整合（总归档）](../Archive/20260824-第二轮资产整合-总归档.md) | ship, vfx, combat, assets | partial | /Game 顶层从 15 个目录收敛至 6 个（Assets / GuLiStrike / LevelPrototyping / Maps / TripoModels） |
| 2026-08-24 | [2026-08-24 第一批飞船组件拆分入库（总归档）](../Archive/20260824-第一批飞船组件拆分入库-总归档.md) | commander, ship, data-pipeline, assets | partial | A 阵营无畏舰（Dreadnought Vengeance）：SMDreadnoughtHull（舰体 16,312 三角形）+ 6 武器件——SMSCTwinBarrelTurret ×1,240 / BottomTwin ×1,240 / SingleBarrel ×1,130 / CIWS ×654 / ShieldGenerator ×312 / MissileBay ×368 |
| 2026-08-24 | [2026-08-24 解决了：CombatAvatarFly 归位勘误——重巡舰体从 Blender 入库，无畏舰/重巡分驻 01/02 文件夹](../Archive/20260824-CombatAvatarFly归位勘误-重巡舰体入库与两舰归位.md) | commander, ship, data-pipeline, assets | not_run | 2026-08-24 解决了：CombatAvatarFly 归位勘误——重巡舰体从 Blender 入库，无畏舰/重巡分驻 01/02 文件夹 |
| 2026-08-22 | [2026-08-21~22 解决了：Excel→JSON→DataTable 数据管线（MVP → 通用化 → 校验配置化）](../Archive/20260822-数据管线开发总归档-0821至0822.md) | ship, data-pipeline, assets | partial | 管线形态：Excel →（系统 Python + openpyxl 校验导出）→ JSON（入库）→（编辑器 Python 经 UnrealMCPython TCP 12029）→ DataTable 资产 → 运行时 InstallPart/BeginPlay 查行应用 |
| 2026-08-22 | [2026-08-22 解决了：数据管线 v2 —— Excel 三行元数据驱动，自动生成 C++ 行结构](../Archive/20260822-数据管线v2-Excel元数据驱动自动生成行结构.md) | commander, ship, data-pipeline, assets | not_run | 用户把表格式改成三行元数据（第 1 行列名 / 第 2 行类型 intfloatboolstrsoftclass / 第 3 行必要性 NecessaryOptional，数据从第 4 行起；每表必有标准三列 id/name/Note），要求消灭手写的 GuLiStrikeShipData.h 与 Tools/DataPipeline/tables.json |
| 2026-08-22 | [2026-08-22 解决了：LVL_Main 按 Play 不生成可控飞船](../Archive/20260822-修复LVL_Main按Play无飞船可控制.md) | ship, vfx, assets | not_run | 诊断（先定位链条断点，再动手） |
| 2026-08-21 | [2026-08-20 ~ 08-21 解决了：DIY 飞船两天开发总归档（MVP → 手感调校 → 架构演进）](../Archive/20260821-DIY飞船开发总归档-0820至0821.md) | ship, network, assets | not_run | ① MVP 落地（0820 上午）：用户定稿装配模型（主体=Character / 部件=组件 / 槽位=socket / 兼容=名字集合）。修复 virgon 9 个空材质槽（槽名即原始材质名的确定性映射）；用"每材质组几何质心"交叉验证舰体朝向（+X=艏部，bridge 与 bridgewindow 质心重合证明映射成立）；发现 fly-01"散件"实为材质实例，部件网格改用 Parag… |
| 2026-08-16 | [2026-08-16 解决了：Marketplace 资产包统一整合至 Content/Assets](../Archive/20260816-资产整合-Marketplace资产包统一归档至Assets.md) | vfx, assets | passed | 8 个资产包、共 4,482 个资产全部迁入 /Game/Assets/，迁移后逐包资产计数与迁移前注册表基线精确一致，加载抽查（BP / 材质 / 动画 / ParagonSample 地图）全部通过，无缺失引用告警 |
