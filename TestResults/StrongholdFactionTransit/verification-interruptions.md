# 定向观察中断记录

- `pie.log`：把中继22改为中立后，团队私有资源快照把中立赠送矿厂误当作红蓝矿厂，触发 `GuLiCommanderResourceAdapter.cpp` 断言。已修正展示边界并完成双目标重构建；`pie-final.log` 与 `reroute-after.json` 证明该场景不再中断、路线按当前速度重算。
- `pie-recheck.log`：编辑器启动首帧尚未完成视口初始化时创建临时截图相机，触发引擎 `FSceneViewport::EnqueueBeginRenderFrame` 除零。重新启动并等资源扫描完成后创建相机成功；未修改引擎代码。
- `pie-final.log`：断路到站后，准备四邻接包围观察，首次将11改为红方时，赠送矿厂的 `GuLiGroundAccessRampComponent::BeginPlay` 在建筑高度向下5000cm未命中地面，触发 `bHasGround` 断言。此处属于旧建筑地形适配问题，本次未修改。包围／恢复的完整PIE场景保持未验证，不能据网络代码推断通过。该断言发生在运输到站证据落盘后，独立于已通过的运输观察。
