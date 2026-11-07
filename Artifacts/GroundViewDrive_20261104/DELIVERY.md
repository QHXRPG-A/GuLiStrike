# 游骑兵转腿走位

**已于 2026-11-04 按用户要求撤回。** 恢复版已编译加载，A／D 使用原横移步态及 0.8 倍速度。以下是转腿方案当时的交付记录。[恢复版实际配置](rollback/restored-configuration.json) · [当前玩法](../../Progress/Gameplay/游骑兵.md)

本次修改覆盖 4001/A4B2 正式玩家角色的输入、权威移动、地面动画、网络快照和源表。地面八向满输入为 216 cm/s，普通空中为 324 cm/s，纯 W 有效喷气为 864 cm/s。纯 A、D 都让脚尖朝镜头右侧，分别倒走和正走。

| 项目 | 转腿方案交付时的结果 |
|---|---|
| Excel 与导出 | Mobility 移除 P 列的 0.8 倍率，RocketJump 更新操控说明。保留其他单元格，通过正式管线导出 42 张表。 |
| 静态核对 | 11 项源码约束、8 档输入在 5 个镜头方位的坐标计算、网络版本及旧格式拒绝路径均已核对。前进、后退、IK、相机和炮台阻尼使用各自原有配置。 |
| 原生模块 | 本次经用户确认后，用 `D:/UnrealEngine-5.7` 构建 `GuLiStrikeEditor Win64 Development`。修正动画文件的局部变量重名后，重编退出码为 0。引擎、项目和导航插件 BuildId 均为 `ec746d14-0746-471d-9ee9-2dfd468f881d`。源码版编辑器已加载。 |
| 正式资产 | Mobility 和 Skills 已导入、保存并与导出值核对。角色与模型表均指向 4001/A4B2，WASD 的二维叠加映射正确。已回读动画图 75 个节点和 8 条动作资产的接触与同步数据。 |
| 运行清单 | 200／600 均为 Ground 状态 14，共用机动配置指纹 `98d0bea2f1ce2f5d3a80c4be81eb61b6`。输入、移动呈现及共享控制版本分别为 8、9、8。 |
| 验证场景 | 已保存 `/Game/Maps/LVL_CommanderMassPrototype`，并回读入口 `GroundMobility_Entry=(-2800,50000,1149.04)`、左右标靶、平台、18° 坡台和四级台阶共 10 个实体。最终编辑器无未保存地图或内容包。 |
| 玩家验收 | 待新局操作。此次只做静态、构建和编辑器结构核对，未运行 PIE、自动化或读图验收。 |

[静态核对与八向表](static-review.json) · [源表逐单元格核对](source-tuning.json) · [正式导出日志](data-export.log) · [开发及玩家步骤](../../Progress/DevelopmentDocumentation/20261104-游骑兵转腿走位.md)

[编译](build-result.json) · [编辑器加载](editor-load.json) · [两表正式导入](asset-import.json) · [动画与数据回读](asset-structure.json) · [正式角色与输入](input-assets.json) · [地图与清单](scene-delivery.json) · [最终编辑器状态](editor-final-state.json)
