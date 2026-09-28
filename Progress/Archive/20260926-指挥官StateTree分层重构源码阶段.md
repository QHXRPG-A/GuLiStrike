---
schema: guli-progress/v1
id: ARC-20260926-001
work_id: ''
kind: archive
role: root
title: 指挥官StateTree分层重构 — 源码与迁移前备份
areas: [commander, resources, building]
categories: [gameplay]
status: recorded
verification: partial
created: "2026-09-26"
updated: "2026-09-26"
summary: 持续任务、版本回执、终态收尾、V2迁移和实际资产导出源码已完成静态核对，旧资产已备份；等待原生编译确认。
next_action: 获得编译确认后构建并加载新模块，完成三树资产迁移、Xmind与原地图审核入口交付。
relations:
  work_items: [WORK-20260926-001]
status_note: 本记录只证明源码静态检查及备份；尚未编译新代码，尚未执行V2资产迁移或运行时验收。
---

# 2026-09-26：指挥官 StateTree 分层重构源码阶段

## 实施事实

- 新增 Actor／Mass PersistentTask，共用操作进入、等待、回执与退出逻辑；旧类型保留用于读取迁移前资产。
- 操作按单位、Version、ExecutionId、Serial 校验，阶段接受后只观察结果；终态观察与收尾分离。
- 三树初始化源码按公共控制、取单、执行和业务分支分层。矿车共用返货，建造明确退单，Mass 处理共享小组阶段变化。
- 移除常规命令中的平铺图重建，新增 V2 一次性迁移及 `UStateTreeEditorData` 递归导出；相关日常 Python 入口只读。
- 新增实际资产到 Xmind 的导出脚本；更新原地图审核入口脚本，尚未对地图执行。
- 旧资产已复制到 `Artifacts/CommanderStateTree/HierarchyV2/Backup/`，未读取二进制内容到上下文，未改写原包。副本大小：Miner 649537 字节、Builder 215998 字节、Mass 214631 字节；文件路径与时间见同目录 `manifest.json`。

## 验证与边界

- 源码审查覆盖阶段选入、失败出口、父状态优先级、请求失效、重复提交、收尾资格消费以及原生 UE 5.7 接口。发现并修正带货矿车的恢复准入、建造退单优先级、Mass 成员同步阶段和资源未就绪的 Deferred 路径。
- `git diff --check` 通过；六个相关脚本 `ast.parse` 通过。源 Soldiers JSON 四兵种绑定仍为 1／2 → Mass、3 → Miner、4 → Builder。
- Progress 索引生成通过，结构检查 `errors=0`，13 项已有文档尺寸／任务数量提示。本次未改动那些历史文档。
- 未新增自动化测试，未执行 PIE、Standalone、运行时测试或性能测试。
- 已按 `gulistrike-progress` 技能向用户请求本次 `GuLiStrikeEditor Win64 Development` 编译确认，归档时尚未收到答复。未执行 Build、Live Coding 或 Hot Reload；预检未发现 UnrealEditor 进程，12029 连接被拒绝。
- 真实 V2 资产编译、保存、回读、新 Xmind 和 Map 实体交付仍未执行。本记录不代表新的树已经在 UE 生效。

## 结果与后续

- [技术记录与玩家审核范围](../DevelopmentDocumentation/20260926-三棵指挥官StateTree分层重构.md)
- 静态记录：`Artifacts/CommanderStateTree/HierarchyV2/static-review.json`。
- 后续对应 Map：`/Game/Maps/LVL_CommanderMassPrototype`；加载新节点后迁移 Miner → Builder → Mass，从实际导出生成 Xmind，最后保存审核 Note 并核对相关实体数据。
