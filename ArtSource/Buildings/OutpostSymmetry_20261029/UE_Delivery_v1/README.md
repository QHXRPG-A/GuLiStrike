# 上下对称悬浮据点 B_v1：UE正式交付

用户授权：“导入至UE并实装，删掉之前的据点模型”。[B放行与源哈希](approval_B.json)，对应可编辑[批准版本冻结源](Sources/Outpost_Symmetric_B_v1_Approved.blend)。工作.blend在导出后又发生保存，已从原有.blend1按批准哈希核对并冻结导入版本；[工作副本](../Production_B_v1/Outpost_Symmetric_B_v1.blend)当前内容保留，未覆盖。

本页保存初次正式接入的阶段事实。后续已完成[HDR发光与60秒自转](../GlowSpin_v1/README.md)，当前材质及局内图见[白腰带与双倍发光](../WhiteBelt_x2_v1/README.md)。

- 正式ModelId **2003**：`/Game/GuLiStrike/Buildings/OutpostSymmetric/Meshes/SM_OutpostSymmetric`。
- **4,824三角面**，单主体网格、一个材质槽；53分件保留在Blender。源尺寸约**17.88×20.23×29.30米**，运行时仍以护盾建筑最终三轴尺寸各3倍解析。
- 全部状态底部净空**4.5～5.5米**，8秒一周期；地面玩法锚点继续使用既有规则。
- `M_OutpostSymmetric_Toon`重建三档明暗和UV0结构线；`M_OutpostSymmetric_Outline`使用背面轮廓附加绘制，不复制导出网格。UV1.x记录八种源材质类别。
- 中央光柱中立白、己方蓝、敌方红；CPD8/16/17沿用既有阵营接口，2003专属CPD18由本地阵营系统管理。外壳和固定灯保持既定配色。
- 旧占位塔、早期立方体、原始塔共3个UE网格及旧评审重定向器已删除。相关地图、目录缓存、复制表和网络配置引用已更新；源制作历史保留。

## 当前玩家入口

`/Game/Maps/LVL_CommanderMassPrototype`。49个编辑器据点预览均已替换、保存并隐藏于游戏；R1C4己方、R4C4中立、R7C4敌方提供对照。R1C4右侧40米护盾模型仅用于编辑器尺寸参照。

在正常角色选择中以指挥官进入，查看三处模型上下端完整性、白蓝红光柱、近远景体量、浮动和点选后的地面运输目标。巨物感、浮动幅度、设施相邻关系及实际操作仍由玩家验收。

## 实际局内图

[中立白](../../../../Artifacts/OutpostSymmetric20261029/Outpost_R4C4_InGame.png) · [己方蓝](../../../../Artifacts/OutpostSymmetric20261029/Outpost_R1C4_InGame.png) · [敌方红](../../../../Artifacts/OutpostSymmetric20261029/Outpost_R7C4_InGame.png)

图片来自同一次正常指挥官PIE的实际据点，未改归属或模型状态；拍摄时临时隐藏HUD并调整指挥官相机，之后均已恢复。斜对角机位会被前侧支架遮住部分核心，以上改用可直接看到核心的方位。背景为实际场景，未隐藏单位或地面设施。用户在本会话明确要求局内截图并允许读图；不修改项目默认视觉核验边界。

## 交付核对

- [导出](blender-export.json)、[UE资产](ue-asset-import.json)、[源表变更](catalog-source-update.json)、[目录导入](catalog-import.json)。只更新2003相关模型数据及建筑3的表现缩放/偏移，其原显示包围盒和底部对齐保持。
- [退役](ue-model-retirement.json)、[最终资产和49实体数据](ue-production-delivery.json)、[实际运行网格与阵营参数](runtime-mesh-and-colors.json)。退役分阶段完成；最终删除阶段报告的零组件变更表示引用迁移此前已保存。
- [网络配置导出](manifest-refresh.json)与正式/隔离配置快照已同步，避免引用已删除模型。
- [源码Editor编译](native-build.json)成功，最终增量10.87秒；引擎/项目BuildId一致。现有尺寸检查改为新资产与新尺寸，没有新增或运行自动化测试。
- 三个截图当帧净空分别为约5.50、4.81、4.60米；全部49个运行时据点使用新网格、材质和轮廓。相机/HUD/原角色偏好已恢复，PIE及本次逻辑/协调进程已结束。

玩家最终美术、操作与联机效果验收待反馈；没有把截图或数据读回记作玩家验收。
