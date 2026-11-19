# 六座 SSF 建筑 · B21 组件纯色

[红蓝总览](Previews/SSF_Overview_B21.png) · [Blender 工程](SSF_A5_ComponentColor_B21.blend) · [分件与色值清单](B21_Component_Color_Manifest.json)

Blender 场景 00 是十二座总览。场景 01–06 分别对应六座建筑，使用渲染视图查看三档明暗和线稿。

| 建筑 | 纯色分件 | 红蓝对照 |
|---|---|---|
| 空军基地 | 砂色机库门与灰绿导轨、石墨机构分开着色。 | [空军基地](Previews/AirBase_B21.png) |
| 克隆中心 | 五座培养舱统一青绿，软管、舱台和维护盖按实体分件着色。 | [克隆中心](Previews/CloningCenter_B21.png) |
| 指挥中心 | 上观察环通体深色，下状态环通体深青。中层机构采用石墨色。 | [指挥中心](Previews/CommandCenter_B21.png) |
| 军工厂 | 四根弯管和两根侧管统一石墨色。四片卷门采用米砂色，独立灯壳采用琥珀橙。 | [军工厂](Previews/MilitaryFactory_B21.png) |
| 反应堆 | 散热片、支撑、护板和琥珀橙锁杆各用单一底色。既有琥珀橙标记填满对应面片。 | [反应堆](Previews/Reactor_B21.png) |
| 战略中心 | 石墨色阵列单元、浅色支架、灰绿底座和队色罩壳按原模型分件着色。 | [战略中心](Previews/StrategyCenter_B21.png) |

回读覆盖 3001–3006 的 712 个主体分件和红蓝配对的材质槽。每队 24,746 个 LOD0 三角面，保有原 UV、骨骼权重、838 段模型边线稿和原轮廓壳。主体材质使用色库常量，明暗为 40%／68%／100%。逐栋效果供用户审核。

UE 5.7 独立导入回读了六份临时网格的三角面、材质槽和线稿数据。预览材质的贴图引用数为零。B21 组件纯色阶段已完成，后续描线加强与场景预览见 [B22 交付](../Production_FeatureConcepts_B22/REVIEW.md)。
