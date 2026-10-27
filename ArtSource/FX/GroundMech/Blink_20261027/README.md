# Ground 闪现 GAS 与绿色能量拖尾

最新：[单体机甲能量替身与铺屏镜头E_v1](Shape_E_v1/DELIVERY.md)。32米瞬移、隐藏真实模型、一个移动的能量形体、96条独立线束；闪现专用构图临时收近并平滑归正。已编译加载、四表及两清单读回，原地图9实体保存完成；最新镜头只作结构化数值核验，视觉待玩家。以下记录属于早期候选，不作为当前参数。

源资源：`/Game/Assets/VFX/Movement/AdvancedDashVFX/NiagaraVFX/NS_GreenCrystalTrail`，DemoLevel/BP_Skl_BP11。原包只读，纹理与材质依赖继续引用原包；没有迁入演示角色和地图逻辑。

项目副本：`/Game/GuLiStrike/FX/GroundMech/Blink/NS_GroundMech_Blink`，单CPU发射器Trails，保留原颜色、材质、模块结构、寿命和噪声。玩家反馈首版过疏后，项目副本生成率由200改为670/s，骨骼速度继承由1改为0；VFX表基础缩放由2.673395965改为1。世界空间骨骼采样已经包含机甲尺寸，无需再用身高比整体放大。

数值：技能ID3/Blink与VFX ID56/GroundMechBlink。距离1200cm，CD2s，Begin0.02/Loop0.8/End0.2s；前摇按用户要求由0.1缩至原来的0.2倍。技能仍由Hybrid固定步推进，0.02秒是配置值，实际阶段切换受固定步采样限制。

入口：`/Game/Maps/LVL_CommanderMassPrototype`，选择Ground，定位GroundMobility_Entry。按Shift一次，方向取按键时镜头水平朝向；冷却中拒绝不排队。使用已有平地、18度坡面、台阶核对遇障停止与空中兼容。

维护：Excel → `Tools/DataPipeline/export_data_from_excel.py` → `author_blink_assets.py`派生并绝对设置项目参数 → `import_blink_data.py` → 最后运行`prepare_blink_scene.py`。仅新增或修改原生类型时另行取得构建许可；本次修正没有原生代码变更。两张源表和项目资产为正式输入，JSON、生成头文件与清单不是日常调数入口。

`asset_readback.json`保留初次派生记录；`asset_readback_visual_fix.json`记录本轮两个参数的前后值、原资源未改动以及Niagara零错误/零警告编译。`visual_contract_before.json`、`skeletal_source_before.json`和`inline_objects_before.json`保存原系统、骨骼采样来源及Demo的2.5秒播放时序。`data_readback.json`记录最新三表与指纹；`scene_readback.json`记录保存后的8实体、VFX软引用和1倍缩放。

本次源资源识别截图与用户参考分别为`Reference_GreenCrystalTrail_Demo.png`、`Reference_User.png`。这只确认选择源特效；新机甲技能的视觉及联机效果待玩家验收。

## 实际交付

源码版Editor构建成功；首次构建65.37秒，构造期默认子对象问题修复后最终构建6.88秒，退出码均0。引擎/项目BuildId均为`d4757e21-eaef-492e-b079-c8c0056c493d`。最终编辑器PID44420，实际类、CDO策略、三表和原地图8实体已读回。

最新配置指纹`d37873d780eeb5d3c036c3561e731780`，200/600清单一致。入口标记(-2800,50000,1149.04)并非重生点，保持正式阵营部署出生；可先在Ground出生位置直接按Shift，再按需前往坡面/台阶验证区。已按用户授权退出PIE、完成修正和保存；没有启动新的PIE或读图测试。本轮技术读回不代表修订版视觉通过，仍待玩家确认。
