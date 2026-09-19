# 轻型机甲与 SpiderMech · UE 同步

当前两台成品已同步至源码版 UE5.7，并保留在 `/Game/Maps/LVL_GroundMech_Demo` 测试地图中。查看[UE 实际画面](Review_UE_v9.html)。

用户明确要求“同步至UE”，随后补充“SpiderMech 也同步”。本次使用 [v9 Blender 成品](../Production_v9_VentMount/Mechs_VentMount_v9.blend)，SHA256 为 `6caa1f5569d1c929b30f924c71f9b06144208ed340c437f3194a3399ec46912b`。该指令放行这两台当前成品进入 UE，不扩展为其他候选的视觉终验。

| 对象 | UE 入口 | 当前内容 |
|---|---|---|
| 轻型地面玩家 | `/Game/GuLiStrike/GroundMech/BP_GroundMech_Light` | 四部件更新为 `Style_v9/Meshes`；赭金弧面封舱甲、贴壳排气底座、内凹肩部散热舱、对称灰蓝腿甲 |
| SpiderMech | `/Game/GuLiStrike/Mechs/SpiderMech/BP_SpiderMech_Styled` | 原网格、四片完整暗红上腿甲、结构线与轮廓；继承原 SpiderMech 蓝图及原动画蓝图 |
| 测试地图 | `/Game/Maps/LVL_GroundMech_Demo` | Ground 出生使用当前轻型；Spider 参照演员为 `GroundMech_SpiderMech_Styled_Reference`，位于约 `(18000,-6500,882)` cm |

轻型 Actor 缩放为 1，模型比例 2.0512617，基准高度 748.3796 cm。现有胶囊、挂点、相机、动画和 Enhanced Input 接入保留。WASD 移动，Shift 按住跑，鼠标控制上身，滚轮缩放，B 进入原建造流程。

Spider 主体仍为 **839,778 三角面**，299 骨骼、原 10 个主体材质槽；独立描边壳另有 839,778 面，占第 11 槽。本次没有再次减面。轻型主体为 **20,038 面**，描边壳另有 11,325 面。不能继续以旧 4,824 面预算描述当前轻型版本。

材质在各自 `Materials` 与 `Textures` 子目录。UE 重建三档明暗、结构线遮罩、背面描边壳和原功能发光；轻型四部件使用各自 2K 图集，Spider 保留原 UV 的 10 组 2K 贴图。商城源资源与旧 Ground 网格保留。

## 实际验证

- [FBX 回读](fbx_readback.json)：五个导出网格尺寸、主体/描边面数、材质槽、骨骼数量与权重通过。曾发现合并描边时重复换算单位，已修复并重新导出；错误版本未安装到玩家蓝图。
- [UE 导入](ue_import.json)：骨骼名、层级、参考姿态及厘米尺寸通过；Spider 参考骨最大位移误差 0.000355 cm。轻型原挂点保持。
- [独立进程回读](ue_saved_readback.json)：重启后读取的四部件、Spider 11 槽、原动画引用和三项蓝图编译通过。
- 发现当前会话动画类默认值为 IgnoreRootMotion，但新实例仍取旧 MontagesOnly。重编译刷新类的属性初始化数据并保存后，独立进程在编译前创建实例也正确取 IgnoreRootMotion。修改前动画蓝图已保留副本；没有修改 C++ 或商城动画。
- [既有连续跑动验收](root-motion.json)：根骨最大偏移 0 cm，最高速度 1,440 cm/s，无直接写入角色位置。
- [既有双端验收](network.json)：通过；最终位置误差约 7.965 cm、上身朝向误差约 0.073°。本轮只复跑这两项相关验收，未重跑此前完整玩法回归。
- [四张实际 UE 截图](ue_capture.json)：保留 Demo 原环境、灯光和曝光；临时轻型展示演员和相机已移除。[俯视运行画面](UE_Ground_PIE.png)为本轮首次进入 PIE 时的外观记录，根位移结论以修复后的上述采样为准。

当前未做这套完整网格与描边成本的性能压测；不将技术回读记为用户最终外观评价。本轮没有 C++、Build.cs 或原生插件修改，不触发原生重建。

## 源文件与回退

- 导出脚本：[export_mech_style_sync_v9.py](../../../../Scripts/Blender/export_mech_style_sync_v9.py)；[导出清单](export_report.json)。
- 独立导入：[import_style_v9.py](../../../../Scripts/GroundMech/import_style_v9.py)；实时引用安装：[install_style_v9_live.py](../../../../Scripts/GroundMech/install_style_v9_live.py)。
- [同步与验证清单](delivery_manifest.json)包含实际资源路径、源文件与 FBX 哈希。
- 回退备份位于 `/Game/GuLiStrike/GroundMech/Style_v9/Rollback`：`BP_GroundMech_Light_BeforeStyle` 与 `ABP_GroundMech_BeforeStyleValidation`。旧网格在 `/Game/GuLiStrike/GroundMech/Meshes`，未覆盖或删除。

重新执行实时安装前先退出 PIE，并确认当前 Demo 没有其他未保存地图修改。导入 worker 只写它拥有的新风格资源；实时引用更新由当前编辑器执行。
