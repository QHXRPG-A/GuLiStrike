# 松树林原地图原位适配

## 当前交付

主验收地图：`/Game/StylizedPineEnvironment/Maps/Demo_Map`。

这是供应商原地图的直接副本，不是重建的关卡。地图中原对象继续引用 `/Game/StylizedPineEnvironment/Assets/`，修改已发生在这些原包名下。下载目录原包未改动；319个源文件与实施前基线哈希完全一致。

[实际预览与证据入口](Review_Demo_v1.html)。用户已经批准此前 `PineStyleAdaptation_20260918/Review_v1.html` 的实际树/草方向，本整图仍待终验。

已保存并重新打开读回：93个网格、8个母材质、74个材质实例、2个地形草类型均有适配记录；8母材质编译状态正常。原404个Actor与3529个手工Foliage实例的数量和变换无差异，额外1个编辑器专用审核相机不参与游戏。3个原环境蓝图随网格重建更新引用并编译保存，没有改逻辑图。

最终九机位补拍已完成并逐张检查，林下与符文机位已修正；只调整审核相机，没有移动原场景物体。再次离开并重新打开已保存地图，在项目默认FXAA下补拍入口，天空补光无需手动刷新也能保持；[最终读回](final_readback.json)仍为零布局差异、零未适配项、空脏包。整图等待用户终验，动态录像与GPU性能尚未验收。

编辑器先前退出两次，冻结日志已确认一次崩溃落在并行速度渲染的工作线程；本次正常重启、补拍、再次重开顺利完成，但根因未确定。见[稳定性记录](editor_stability.md)，不将停止调用强制材质重编接口误报为崩溃修复。

## 实际改动

- 松针、树皮、草、花、岩石、木结构母材质：有限三档主色，移除PBR颜色/法线碎噪。植被保持来源透明遮罩、无附加描边；草花不是逐叶逐瓣高面数新建模型。
- 实例配色保留深/浅石、活/枯木、红/黄/白花及绿植差异。地形六个原刷层与Auto坡度/草生成分布仍在，表面改为干净哑光色块并保留场景投影。
- 草LOD0仍37三角面；网格构建比例XY1.35/Z0.6，整体约23.3cm高（不含风摆边界）。原Actor/实例Scale不改，自动草密度1000→550。
- 花LOD0各40三角面。93网格均三级LOD；草、3朵花和2块独立木板的自动减面结果保持原面数，其余按实际结果减少。每款各1份的LOD0/1/2三角面总和72,236 / 50,625 / 32,703，不代表场景绘制面数或GPU收益。
- 草花采用根部高度权重风；4秒周期，草默认约0.7cm峰值，花约2cm。树保留来源风变形并整体减弱。WindStrength=0有明确乘零路径，但连续风摆录像尚未交付。
- 美术光方向沿用原Demo太阳约(-0.700,-0.507,0.503)。树/草/花/石/木的三档表面不会随场景灯自动改变；地形使用哑光受光方式以保留落地投影。
- 天空和局部后处理调回清晰冷暖色块；未改全局配置。九机位预览使用临时TAA和100%屏幕比例，入口前后对照两侧均使用项目FXAA。当前编辑器已恢复原值AA=1、ScreenPercentage=0；不能把预览AA设置当成项目配置修改。
- 岩石/木石对象保留几何接缝和分档边界，本轮没有额外制作描边壳或内部线稿纹理。

## 目录和证据

| 文件 | 用途 |
|---|---|
| `copy_and_backup.json` | 42个Maps/Demo复制文件与277个项目Assets备份的路径/哈希 |
| `Backup_ProjectBefore/` | 项目修改前Assets的逐字节备份；不要放入Content |
| `original_assets.json` / `original_scene.json` | 原资源与原地图不可变快照 |
| `original_graph_*.json` | 原8个母材质图 |
| `adapted_instances.json` / `grass_changes.json` / `environment_changes.json` | 材质实例、草及环境修改记录 |
| `mesh_lods.json` | 93款逐项LOD真实面数 |
| `adapted_scene.json` / `readback.json` | 保存重新打开后的场景/引用/编译读回 |
| `final_readback.json` | 补拍与再次重开后的布局/覆盖/环境读回，不触发材质强制重编 |
| `delivery_manifest.json` | 交付文件哈希、319源文件保持结果、已批准v1的10个证据文件哈希 |
| `Previews/` | 真实UE截图；03–09为最终九机位，11为重开后项目默认FXAA。01/02/10是中间记录，不作最终对照 |
| `editor_stability.md` / `editor_crash_20260918_015505.log` | 崩溃事实、冻结日志及未确定的根因边界 |

可编辑源为供应商UE网格、材质及本项目制作脚本；原包没有提供Blender、FBX、PSD，未伪造这些DCC源文件。

## 继续操作

源码版引擎：`D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.exe`。

使用 `Scripts/commander_editor_python.py` 执行 `Scripts/adapt_pine_demo.py`，逐个动作运行。不要重跑 `prepare_pine_demo_adaptation.ps1`、`open_demo`、`inventory` 或已完成的母材质初建；它们有基线/已完成保护。

当前源码UE已停在Demo_Map入口，下一步是用户查看整张实际原图并给出终验意见。已有布局读回不等于性能通过；未测GPU/DrawCall和完整运行时。需要重新读取时用 `final_readback`，不要将会触发 `ForceRecompileForRendering` 的材质诊断接口当成普通只读查询。

恢复：先退出编辑器，按 `copy_and_backup.json` 指定的相对路径逐项恢复 `Backup_ProjectBefore/Assets` 到项目原 `Content/StylizedPineEnvironment/Assets`。地图原件仍在下载目录，需要回退地图时使用清单所列源文件。不要删除或替换整个Content，也不要把源文件复制到错误包路径。
