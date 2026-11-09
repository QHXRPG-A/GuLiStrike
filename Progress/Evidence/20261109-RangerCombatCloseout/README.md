# 游骑兵战斗会话结项证据

用户于2026-11-09明确要求清理本会话临时文件、将相关文档标记完成并将项目除资源外的变更上传GitHub。`WORK-20261108-001`与`WORK-20261109-001`据此结项；技术验证仍为partial，原换档边界和未执行的视觉/恢复项目未被改写成通过。

## 清理与保留

[cleanup-plan.json](cleanup-plan.json)记录6个本会话Artifacts目录的逐文件保留/删除清单，删除前验证长度与SHA-256。[owned-pie-temporary-dirs.json](owned-pie-temporary-dirs.json)记录由本会话回执中的RoomId确认的14个临时PIE目录，[pie-cleanup-files.json](pie-cleanup-files.json)保留其24个文件的哈希。删除后核对结果见[cleanup-result.json](cleanup-result.json)。

清除647个过程采集、旧构建输出、基线副本、Excel批次JSON和一次性控制脚本，以及24个已退出PIE临时文件，总计1,726,453,353字节。保留117个标定、编译、导入、场景、失败边界及运行报告。正式源码、维护/制作脚本、Excel源表、UE资源、美术原图及手调动画未删除；未扫描或清理Saved、Intermediate、Binaries、DerivedDataCache和Content/Assets。

超过200MB的两份全世界重复采样已提取唯一事件，保留每发冻结值、真实伤害回执、首尾目标状态和源文件哈希：

- [强化爆炸弹第一局](explosive-run1-unique-events.json)：3369条Fire记录。
- [强化爆炸弹第二局](explosive-run2-unique-events.json)：520条Fire记录。
- [左右Loop长局](loop-run18-unique-shots.json)：8470条原始普攻记录，保留ShotId、fire_ordinal、侧别、时刻及实际回执。

原汇总中的已删除原始采集文件名作为来源记录保留，不表示大文件仍存在；相应事实可用以上去重事件与保留报告核对。此前失败运行的结论保留，不因清理被改写为成功。

## 项目同步范围

提交范围为项目源码、维护脚本、配置、Excel及导出数据、技能/项目规则、文档和必要技术证据；排除UE资产、地图、美术目录、媒体和构建产物。本地历史含资源提交，使用远端main为父提交生成非资源变更；保留本地资源历史和工作区资源修改，不强推、不改写远端历史。

本次只有清理、文档结项和Git操作，没有新编译、PIE或读图检查。GitHub提交及回读结果由本目录同步回执记录。
