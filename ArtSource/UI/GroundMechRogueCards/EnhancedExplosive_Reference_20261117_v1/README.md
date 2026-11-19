# 强化爆炸弹 · 参考图 v1

日期：2026-11-17。使用内置 `image_gen.imagegen` 生成三张独立参考图，保留工具原始输出。当前交付阶段为方向参考，用户美术审核待反馈。

对应设计节点：`data/Design/RogueCardGraph.json` 中的 Ground / `12.01`，标题“强化爆炸弹”，描述为游骑兵爆炸半径与伤害强化。本轮制作弹体主题插画，未使用机甲本体参考。

| 版本 | 设计方向 | 原始尺寸 | 原图 | 实际完整提示词 |
|---|---|---|---|---|
| A 双环巡航 | 接近用户参考的纤长弹体、双发光环与云海 | 1316 × 1195 | [PNG](Outputs/A_twin_ring_cruise.png) | [Prompt](Prompts/A_twin_ring_cruise.txt) |
| B 重装高爆 | 厚重前段护甲、粗壮弹体与强化能量仓 | 1317 × 1194 | [PNG](Outputs/B_heavy_blast.png) | [Prompt](Prompts/B_heavy_blast.txt) |
| C 爆轰冲击 | 前冲透视、背景爆轰和外扩冲击环 | 1312 × 1199 | [PNG](Outputs/C_expanding_blast.png) | [Prompt](Prompts/C_expanding_blast.txt) |

表内“设计方向”描述生成意图，不代表助手对结果图完成视觉验收。

## 输入与风格依据

- [用户原始参考的项目内副本](Inputs/user_missile_reference.png)：唯一图像输入，顺序为 1。使用黑色圆柱弹体、双发光环、翼片和尾焰作为参考。
- 原附件：`C:/Users/a/AppData/Local/Temp/codex-clipboard-937c638c-be89-4fa5-8fb2-e6171225cdef.png`。
- 项目规范：[GuLiStrike 美术规范 v1.15](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md)，重点为组件纯色、线稿、三档明暗、爆炸烟火和第 11 节统一色库。
- 本版选色记录：弹体主色 `#2C3735`，暗部/描线 `#274E61` / `#1A182F`；受光及天空 `#6AA4BE`；热区 `#EE9D58` / `#8E3A2A`；亮核/云层 `#FEE4D9` / `#EABCBA`。B 额外使用少量 `#D5C09C`。这是本版提示词用色记录，规范仍是统一色库的唯一来源。

## 生成与尺寸

- 三次独立调用均使用上述输入图，`transparent_background=false`。
- [实际请求记录](generation_requests.json)保存输入顺序、参数和提示词 UTF-8 SHA-256；[来源记录](output_sources.json)保存工具生成文件路径；[交付清单](manifest.json)保存原图哈希、尺寸及项目副本路径。
- 提示词请求 Ground 插画窗口宽高比 `1.0889386:1`，约 `1568 × 1440`；这是文字提示，工具没有被传入不存在的尺寸参数。
- 实际输出相对目标比例分别偏宽约 A 1.1311%、B 1.2927%、C 0.4873%。本次保留原始参考图，不裁切或拉伸；正式制作仍须按现有窗口精确适配，不能改卡框比例迁就原图。

## 核验与阶段

已核对三个 PNG 的文件完整性、真实尺寸、RGB 模式、原始文件与项目副本 SHA-256 一致，以及提示词文件哈希。遵守项目视觉核验边界，未进行助手读图测试或视觉验收。

用户初稿选择待反馈；ComfyUI/Qwen 参考图、六层制作和 UE 接入尚未进入。本轮任务范围为三版参考图。

