# 指挥官性能正式版证据

当前配置与使用说明见[正式版本交付](../../DevelopmentDocumentation/20261021-交火优化正式版本交付.md)。本目录只保存适合随项目提交的计时、验证与清理摘要；未包含截图、原始Trace或个人认证信息。

- `selection-pair-61.json`及`selection-pair-61-network.json`：用户指定的两版16.22/16.23ms取舍证据。该单轮的15ms字段为历史采集口径，当前目标是完整20ms响应。
- `production-paired-summary.json`及`production-workload-check.json`：清理后同build-42的三轮正常对照。P95按合并原生帧间隔重新排序计算，不能平均各轮P95。
- `compression-cpu-diagnostic.json`：带Trace的独立CPU诊断；与正常整帧采样分开，不能把嵌套或跨线程计时相加。
- `response-diagnostic.json`：24成员语义输入的阶段计时，未通过完整20ms。它不能替代真实鼠标输入到显示的验收。
- `build-verification.json`、`source-manifest.json`、`compression-regression.json`、`static-check.json`：编译身份、源码版本及检查结果。
- `defaults-readback.json`、`profile-equivalence.json`、`logic-launch-readback.json`：正式默认、对照配置及所属逻辑客户端参数。
- `map-entry.json`：原地图默认停用入口的保存与实体回读；不表示玩家视觉通过。
- `cleanup-manifest.json`、`cleanup-result.json`：本会话精确清理范围。原始Trace及导出命令保留，可重建已删除的巨大CSV。
- `evidence-origins.json`：文件来源与源文件哈希。

完整原始数据保留于`D:/UE5.7/PerformanceCaptures/GuLiStrike/hybrid-cpu-20261020`。历史源备份已验证后合并为`adoption-42`下的ZIP，不属于正式代码。正式版本依赖项目内保存、应用于外部源码UE5.7的Slate补丁。

当前代码上传回执见`github-main-code-delivery.json`：272个源码、配置、脚本和文档文件已正常推送至远端`main`，代码提交为`3e16822e38dcec2068395086b3440695a5340c2b`。地图、材质与美术源包共70个文件保留为本地后续资源提交，LFS额度恢复后再同步；远端尚不是本机完整资产副本。

`github-delivery.json`保留首次完整推送被LFS预算拒绝的历史回执，不能用它判断后续代码上传状态。此次拆分没有改变正式源码和本机资产内容，未强推或跳过LFS。
