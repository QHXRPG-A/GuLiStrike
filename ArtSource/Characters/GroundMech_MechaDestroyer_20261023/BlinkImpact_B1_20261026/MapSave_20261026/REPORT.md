> 2026-10-26 本会话已按用户指令完成归档。以下保留当时版本及审核事实，最终方向/时长与清理说明见[完成归档](../../../../../Progress/Archive/20261026-地面机甲镜头方向闪现与会话完成归档.md)；不补记未执行的视觉验收。

# 地图保存后反复提示未保存：原因与修复

2026-10-26，用户反馈 `/Game/Maps/LVL_CommanderMassPrototype` 在“保存内容”窗口反复保存仍提示未保存。

## 最早异常与排除项

预期：保存地图后，在没有编辑的情况下保持 clean。实际：保存返回 true，日志明确把临时包移入正式 `.umap`，即时 dirty 列表为空；编辑器空闲后又出现该地图。文件非只读，D盘约473GB空闲，当前只有一个源码版编辑器进程。排除文件锁、权限和空间不足。

通过编辑器导出保存前/空闲后的文本对象数据，唯一差异是 `GuLiBiZhiMaoReviewActor_0.ReviewVertexVAT` 的 `PerInstanceSMCustomData` 动画帧字段；详见 [差异](idle_changes.diff)、[保存即时结果](save_attempt.json)和[延迟结果](delayed_dirty.json)。不是读取二进制地图，也没有截图或视口目检。

地图中的 `BiZhiMaoQA_Sample` 审核样机允许编辑器Tick。其 `UpdateInstanceTransform` 和 `GuLiVATAnimation::WriteInstance → SetCustomDataValue` 每帧调用ISM的 `Modify()`，后者将这些派生预览数据标记为包变更。暂停该样机Tick并保存后，dirty列表持续为空，进一步确认触发源：[暂停保存](pause_and_save.json)、[延迟读回](paused_delayed_readback.json)。

## 修复范围

仅修改 `Source/GuLiStrike/Gameplay/Presentation/GuLiBiZhiMaoReviewActor.cpp` 的自动预览更新。Editor非游戏World内，在写预览帧的同步作用域中，临时将该ISM组件标记为Transient并排除事务记录；退出立即恢复原flags。没有清空地图dirty标志，不抑制Actor、模型、动画资源等正常作者编辑，也不永久隐藏/删除审核样机。

游戏World和PIE的逻辑不走此分支。模型、骨架、地面机甲玩法、命中特效、地图几何和导航保持。未改引擎源码，也未增加测试框架。

## 构建与核对

沿用本会话保存/编译/重启授权，保存当前地图后正常关闭编辑器，以 `D:/UnrealEngine-5.7` 构建 `GuLiStrikeEditor Win64 Development`，4个构建动作、退出码0。项目/引擎BuildId一致：`d4757e21-eaef-492e-b079-c8c0056c493d`，新编辑器进程19988。[构建结果](build.json)、[日志](build.log)。

重新打开原地图后样机Tick自动启用。保存后的55秒内动画帧仍在变化，地图dirty为空：[启动读回](fixed_start.json)、[持续预览读回](fixed_preview_readback.json)。对样机执行显式Modify但不改变属性值，以检查正常作者修改标记仍能保留；最后再次保存，并在预览继续运行后核对地图保持clean。最终结果见 [最终读回](final_save_readback.json)。

本轮直接验证编辑器保存状态；未启动PIE、未做视觉或性能测试。此结果只说明保存循环已修复，不代表之前闪现/命中特效的玩家视觉验收通过。
