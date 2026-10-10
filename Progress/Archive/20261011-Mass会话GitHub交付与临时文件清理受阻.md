---
schema: guli-progress/v1
id: ARC-20261011-005
work_id: ''
kind: archive
role: root
title: Mass会话GitHub交付与临时文件清理受阻
areas: [commander, navigation, presentation, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-10-11'
updated: '2026-10-11'
summary: Mass优化、原地图与报告共104个文件已上传GitHub main，远端提交核对一致；87个临时文件约85.4 MB的删除被自动审批策略拦截，未删除任何文件。
next_action: 删除策略允许后按清单清理本次临时文件；玩家效果验收沿用原地图入口。
relations:
  work_items: [WORK-20261011-001]
  related: [DEV-20261011-001, ARC-20261011-003, ARC-20261011-004]
status_note: 上传成功与清理受阻分别记录；原始采样保留在本地，本轮未重新编译、运行PIE或补充性能收益结论。
---

# 2026-10-11：Mass会话GitHub交付与清理边界

用户明确要求“清除这个会话产生的临时文件，并将项目的变更传GitHub”。本次上传固定应用的Mass避障与三倍转向、原地图验证入口、采样和分析脚本、配对/网络/Trace诊断汇总、构建与自动化证据、需求/开发/玩法及归档文档。此前已回退的StateTree、UI和候选并行方案没有恢复；正式网络配置和协议没有改动。

项目变更提交为[`40b03777b92de947a687b9860d33631732ac6eab`](https://github.com/QHXRPG-A/GuLiStrike/commit/40b03777b92de947a687b9860d33631732ac6eab)，共104个文件，已推送至既有`QHXRPG-A/GuLiStrike`仓库的`main`；`git ls-remote`核对远端SHA与本地一致。地图按Git LFS提交，首次代理连接中断后使用仅对本次进程生效的存储域名直连设置重试成功，未改全局网络设置或仓库历史。详见[交付回执](../../Artifacts/MassAvoidance20261011/github-delivery.json)。本归档和回执通过后续文档提交上传。

提交前差异检查通过，12份相关Python脚本语法检查通过；20个最新构建清单中的源文件哈希与工作区一致。Progress索引构建完成，检查0错误、17项既有文档尺寸/任务数量提示。本轮没有重复构建或运行游戏；最近一次构建、8项自动化及原地图实体读回证据继续使用[固定应用归档](20261011-Mass优化固定应用与开关移除.md)，不将历史40窗口宣称为固定实现的重新采样。

清理清单逐项核对了临时源码副本、可从原始Trace再生成的事件CSV与命令文件、构建UBA、自动化HTML、Python字节码和无引用的一次性脚本，共**87个文件、85424704 B（约85.4 MB）**。自动审批检查连续拒绝批量删除和精确指定的单文件原生PowerShell删除，仅返回`blocked by policy`，未提供具体原因。因此本轮**实际删除0个文件**，清理没有完成；这些文件仍在本地，并已从上传范围排除。见[逐项清单与拦截记录](../../Artifacts/MassAvoidance20261011/cleanup-manifest.json)。

另发现`.git/index.lock`为空，最后写入时间为00:42:58，核对时没有Git写进程。通过可逆改名保留为`.git/index.stale-20261011-004231.lock`，解除提交阻塞；没有删除锁文件或覆盖索引内容。

**53份原始Trace**及有效/失败窗口的UE CSV、原生捕获、网络时间序列、场景配置、行为读回和必要日志保留。大体积逐轮样本留在本地，由本次限定目录的忽略规则防止误入库；最终报告、汇总CSV、历史/当前构建清单和恢复记录已上传。未清理其他会话目录、项目构建目录或用户提供的图片。

本次是文件与Git交付收尾，没有新增玩家可观察行为，无需另建地图。玩家验收仍使用`/Game/Maps/LVL_CommanderMassPrototype`中的`PerfMassMovement_Entry`及[现有开发说明](../DevelopmentDocumentation/20261011-Mass避障简化与转向提速.md)；窄路/拥堵/转向观感和网络可靠事件积压的后续工作保持原状态。
