# 重防号悬浮、蓝色喷流与GPU余迹

2026-09-29：最终GPU资源已生成并保存，原生Editor已编译、重载。用户追加要求“略大于悬浮盘”“再实一点”，当前候选已调整为盘径110%、核心峰值Alpha 0.90、余迹峰值Alpha 0.45。用户视觉终验待确认。

## 当前表现

- 无骨骼静态实例+WPO。模型整体基础悬浮120cm，Idle起伏±12cm/3s，俯仰/侧倾±1.2°（3.7/4.3s周期），0.35s平滑停走混合。5/15cm/s迟滞和0.2s静止门限；稳定ID错开相位。
- 整体悬浮在局部部件运动之后应用。WPO位置/法线、CPU枪口/导弹口、四盘下喷口与模型显示挂点共用变换；导航、碰撞、地面选择圈不变。
- 当前/前帧14+14个姿态值，加命中时间共29float。协议21复制过渡语义，不逐帧复制波形。
- 喷流60→90cm，当前盘径391.45cm，对应开口宽430.59cm；改宽遮罩并加实核心，保留柔和边缘和25cm DepthFade。长度和模型缩放没有重复放大。
- 世界空间余迹寿命0.30s，移动时采样，停止后自然消散。喷口方向为动画后盘法线负方向。

## GPU与大规模应用

`UGuLiWarMachineHoverComponent`为整个Mass表现Actor管理批次，无自身Tick，不为每台单位创建4个Niagara组件。每批最多1024喷口；两个发射器均为GPUComputeSim，分配1024核心槽与10240余迹槽。CPU上传喷口位置、方向、尺寸、颜色、运动标记和代次；GPU缓存余迹世界位置并执行寿命、80cm/s下移和淡出。无逐粒子CPU模拟、灯光、碰撞或流体。

稳定槽位/代次断开传送与复用的旧采样，旧粒子保持出生位置并到期消失；不是会跨地图连线的Ribbon。过期或不可见槽尺寸为0。CPU只保留批次最近0.325s保守包围盒。喷流独立在180–200m淡出，本体不因此隐藏。专用服务器实测0喷流组件。

容量示例：500台全重防号=2000喷口=2批=22528持久GPU槽。此项是容量推算，尚未覆盖全部2000喷口同时移动的最坏负载。可见粒子数与预分配槽容量分别记录。

## 验证与证据

| 项目 | 结果 |
|---|---|
| Editor原生构建 | 源码引擎D:/UnrealEngine-5.7，GuLiStrikeEditor Win64 Development，退出0，8个BuildId一致；见[native-build-report.json](native-build-report.json)。初次API访问错误与成功重试均保留。 |
| 原生回归 | 6项通过：Hover、MechanicalAnimation及4项Pose协议检查。见[automation-final.json](automation-final.json)。 |
| 最终GPU资产 | 系统valid/ready，两个GPU shader均finished/complete，0错误0警告；见[ue-fx-gpu-assets.json](ue-fx-gpu-assets.json)。`ue-fx-assets.json`是已被取代的CPU原型历史记录。 |
| 材质/静态喷口 | 8材质、PICD 1–28、六材质函数调用和无骨骼喷口回读通过，见[asset-verification.json](asset-verification.json)。 |
| 真实预览 | 7样本、9张游戏视口截图覆盖不同Idle相位、开火、转向、720/1440、启停、生命周期示意；见[Previews/review-runtime.json](Previews/review-runtime.json)。 |
| GPU数据一致性 | 冻结预览源后28可见核心、56可见余迹，1024+10240独立槽；上传值与GPU核心位置/方向误差0；宽430.59cm。见[runtime-frozen-preview.json](runtime-frozen-preview.json)。仅显式QA做SimCache读回，生产及性能捕获没有GPU读回。 |
| 迟加入 | 专服+首客户端+后加入客户端，8次采样，名册均500。两客户端各500本体、1个喷流组件，专服0组件。125对按根位置配对的WM高度最大差0.716cm、俯仰/侧倾最大差0.0561°。见[network-late-join-summary.json](network-late-join-summary.json)。 |
| 远景本体 | 500实例及Cull=0属性核对；100/200/360m相机到编队中心截图仍可见两种单位。360m画面HUD垂直离地约206m，二者不是同一距离定义。见[远景截图](performance-held/idle-360m-fx1.png)。 |
| 艺术验收 | 新版观感待用户确认；编译、实体数据与GPU数值校验不替代视觉审核。 |

## 500混编单位性能初步记录

硬件RTX 4090 / i9-14900KF，Editor内Standalone PIE，1280×724，各质量档3，未打包。500正式实例由250扫荡者和250重防号组成，两阵营各125+125；7预览关闭。每段240帧，汇总排除前20帧。

| 相机至蓝方编队中心 | FX | 喷口中位数 | 批数 / 槽容量 | Game中位ms | 总GPU中位ms | NiagaraGPU模拟中位ms |
|---|---|---:|---|---:|---:|---:|
| 100m | 关 | 0 | 0 / 0 | 38.035 | 5.423 | 无该pass |
| 100m | 开 | 330 | 1 / 11264 | 21.976 | 5.033 | 0.0090 |
| 200m | 关 | 0 | 0 / 0 | 24.893 | 5.834 | 无该pass |
| 200m | 开 | 182 | 1 / 11264 | 36.349 | 6.329 | 0.0098 |
| 360m | 关 | 0 | 0 / 0 | 21.805 | 6.587 | 无该pass |
| 360m | 开 | 0 | 0 / 0 | 32.707 | 6.956 | 无该pass |

以上**不能作为严格开销A/B结论**：正常停止任务阻止了自动行军，但本关自动建造的清场逻辑仍移动了个别单位，捕获首尾最大根位置变化3620.83cm；后台场景也继续变化。Game与总GPU变化不归因于喷流，不宣称开启FX变快。`RenderThreadTime`列接近0，作为无效Draw计时保留原始值，不填写成0ms渲染开销。喷口/批次和GPU模拟pass仅证明实际运行负载，所有移动喷口中位数为0，尚未测满负载余迹。

原始CSV、图与有效性字段见[performance-held/summary.json](performance-held/summary.json)。更早`performance/`采样部队移出镜头，开启FX时喷口中位数0，已标无效；保留作为排查证据。后续需等关卡自动建造/清场稳定，或使用不会改动编队的隔离配置再做ABBA多轮捕获，并在全部喷口有效视距内测移动状态。

## 场景与重建

Map：`/Game/Maps/LVL_CommanderMassPrototype`。Actor：`WarMachineHover_PlayablePreview`，原点`(8000,-16000,0)`，3列7样本依次展示Idle、静止后坐、原地转向、720、1440、启停、生命周期示意。PIE后自动驱动，`gs.Commander.HoverPreview 1`开启。生命周期示意不等于真实传送/死亡验收。

资产：

- `/Game/GuLiStrike/FX/WarMachineHover/M_WarMachineHoverJet`
- `/Game/GuLiStrike/FX/WarMachineHover/M_WarMachineHoverTrail`
- `/Game/GuLiStrike/FX/WarMachineHover/NS_WarMachineHoverPool`
- `/Game/Commander/Units/MechanicalAnimation/MF_GuLiRigidMechanical`及六个主体/替身材质
- `/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid`：FX_Hover_FL/FR/RL/RR

脚本位于项目`Scripts/`：

- `Blender/extract_warmachine_hover_nozzles.py`：只读提取无骨骼源模型盘底。
- `import_warmachine_hover_nozzles.py`、`build_mass_rigid_materials.py`：静态挂点/WPO。迁移保留函数GUID。
- `build_warmachine_hover_fx.py`、`Niagara/GuLiHoverTrail.hlsl`：专用材质和GPU系统。复制项目现有WingmanLaserPool，复用Niagara模板模块；不改来源资源。
- `verify_warmachine_hover_assets.py`：保存资源及真实GPU shader状态回读。
- `author_warmachine_hover_scene.py`：重建被Git忽略的地图内样本。
- `capture_warmachine_hover_review.py`、`validate_warmachine_hover_runtime.py`、`validate_warmachine_hover_late_join.py`：显式PIE验证与证据。
- `profile_warmachine_hover.py`、`analyze_warmachine_hover_profile.py`：采样与有效性判别。先在新鲜500单位PIE用`gs.Commander.HoverQA.Hold`发停止指令和暂停产兵；结束PIE丢弃测试设置。

VFX源表48号、JSON、生成ID和DataTable已同步，见`registry-import.json`。重建最终GPU资产前须加载已编译的新Editor模块；脚本和运行时均拒绝把旧CPU原型当GPU资源。

## 未完成的验收

用户视觉确认；真实Mass连续启停/传送/死亡/混编复用完整组合；带丢包/延迟迟加入；CPU/GPU网格顶点枪口直接对齐；受击/传送/残骸逐项视觉；500全重防号同时移动的最坏负载；可靠Draw和打包性能。晚加入的残骸目前以最新样本初始化，不保证重现死亡瞬间的悬浮相位。上述项目没有登记为通过。
