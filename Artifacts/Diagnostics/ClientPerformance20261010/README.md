# 2026-10-10 客户端性能证据与会话收尾

本目录保留本会话的性能统计、逐样本负载/帧数据、计时汇总、选定帧分析、技术输入检查、源码构建加载记录和可播放视觉证据，供本地及GitHub交付使用。多线程方案尚未实施。

## 结果与方案

- [需求](../../../Progress/RequirementDocument/20261010-PIE性能基线与快照多线程优化.md)
- [开发方案](../../../Progress/DevelopmentDocumentation/20261010-PIE性能基线与快照多线程优化.md)
- [本轮枪口性能报告](muzzle-batch/performance-report.md)：200单位约26.4→34.0 FPS；600单位＋额外500弹丸约12.7→12.9 FPS，压力收益未证实。
- [逐样本及计时分析](muzzle-batch/performance-analysis.json)、[CPU窗口汇总](muzzle-batch/trace-window-summaries.json)。18份样本在`muzzle-batch/paired`，保留原帧CSV、实际人口/运动/反馈、World计数和中位/P95/最差帧分析；统计不是独立Client1成本。
- [三轮屏外计数](offscreen-evidence.json)：每客户端平均约576～592单位可见、每帧约4～14单位跳过呈现；本轮不是大规模屏外收益对照，弹丸可见数没有独立采集。
- [技术合同](muzzle-batch/runtime-contract-validation.json)、[正式资源及地图读回](muzzle-batch/delivery-readback.json)、[采用决定](muzzle-batch/adoption-decision.json)。
- [源码加载记录](muzzle-batch/build-adoption-loaded-receipt.json)、[构建退出码](muzzle-batch/build-adoption-exit.json)、[原始最终构建日志](muzzle-batch/build-adoption.txt)。这些只覆盖当时构建版本，不证明后来全部未构建变更通过。
- [玩家操作指南](muzzle-batch/manual-review-guide.md)、[旧/新移动跟随对照](muzzle-batch/visual/old-new-moving-follow.gif)。玩家视觉验收仍待确认。
- [GPU Ribbon读回](muzzle-batch/ribbon-renderer-readback.json)、[测量外GPU补充](muzzle-batch/gpu-supplement/summary.json)。ID52保持CPU模拟且GPU Ribbon初始化Dispatch为0，其他资源GPU输出不可当作逐帧均值。
- 早期阶段的轻量汇总在`prior-stages`，保留事实来源，不拿不同场景旧数字计算累计优化收益。

## 清理范围与可复现边界

[保留证据清单](retained-evidence.json)记录源路径、归档路径、大小与SHA-256。[清理清单](cleanup-manifest.json)记录每个临时文件、原因、大小与执行状态，完成结果在[清理回执](cleanup-receipt.json)。

清理限本会话明确生成的`outputs/performance`阶段目录、`Scripts/Performance`和`Scripts/Vfx`的会话期Python字节码。删除原始Trace/缓存、逐事件CSV、失败或被替代采样、独立旧代码比较副本、已编码的中间帧以及中间诊断输出。保留正式源码、脚本、Excel、Niagara/材质、地图、有效帧CSV、结论、检查回执、最终构建日志、选定图像和可播放对照。

未搜索或清理Saved、Intermediate、Binaries、DerivedDataCache、商城资产、下载目录或其他会话工作树；不使用`git clean`。清理的原始Trace无法再用Insights重放，原始逐事件Scope并集不能从已删CSV重新导出；保留的计时汇总、选定帧分析和统计仍可核对。重新完整分析需要沿既有脚本重新采样，不能宣称删除原始输入后仍能无损重放。

历史Progress中的本地输出目录保留Markdown入口。正式证据以本目录为主；历史链接到已清理的大文件保留为清理前事实，不悄悄改写旧归档。
