# 机枪效果优化 · 2026-09-30

用户批准：重防号枪口2倍；扫荡者/重防号机枪弹道照地；所有机枪命中2倍。

- [实施前文件哈希](baseline.json)、[已有源码差异](preexisting-source.diff)：记录并保留其他在途工作。
- [Excel写入单元格](vfx-source-cells.json)：ID5尺寸2；ID52新枪口尺寸1。
- [静态检查](static-review.json)：脚本语法、差异、数据导出与本地引擎API核对。
- [场景回读](scene-readback.json)：原型地图10个EditorOnly对象已保存，灯光数组已绑定。
- [原生构建](native-build.log)、[BuildId](module-build-ids.json)：源码版Editor构建成功，项目与六个插件匹配引擎。
- [资产应用](asset-application.json)：3个生产资产已保存，Niagara编译零错误、零警告；源表、生成数据和UE引用已核对。
- [离屏预览记录](Previews/isolated-review.json)：7个样本的开始、峰值、衰减及清理阶段，共28帧；不代表玩家视觉验收。
- [最终运行快照](runtime-final-snapshot.json)：双端PIE已启动，但未观察到实际交火，不能作为密集预算与回池验证通过的证据。
- [开发记录](../../Progress/DevelopmentDocumentation/20260930-机枪枪口命中与弹道照明.md)。

用户回复“编译”后已完成正式构建、重开编辑器与生产资产应用。首轮颜色有限值API不兼容已修复，最终退出码0。半径800cm、亮度25、每客户端跨批次最多6灯已写入；阴影与半透明受光关闭。源码按真实弹头位置上传灯光，实际交火跟随、预算和生命周期仍待玩家验证。

可复现顺序：`Scripts/apply_machine_gun_effects.py` → `Scripts/author_machine_gun_review_scene.py`（通过 `Scripts/ue_exec.py`）。仅在本次原生模块已加载且PIE停止时执行。对照捕获脚本为 `Scripts/capture_machine_gun_review.py`，预览后会恢复EditorOnly和数组，不自动保存激活状态。

对照相机 `MachineGunReview_Camera`；场景原点 `(-10000,-12000,1000)`。枪口尺寸对照为1/2/1，命中为1/2，弹道为无灯/最多6灯。样本保持停播。用户要求“不要读图，总结”后停止进一步读图和运行验证，本轮PIE已结束，临时出生点和后台节流已恢复；玩家视觉审核待完成。
