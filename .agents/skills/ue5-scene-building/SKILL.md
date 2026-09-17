---
name: ue5-scene-building
description: UE5.6/UE5.7 地编（关卡编辑/场景搭建/白盒 blockout）工作流。当用户要求搭建、布置、修改场景或关卡，提到"地编"、"场景搭建"、"摆场景"、"布置关卡"、"搭一个XX场景"、"白盒"、"blockout"、"level design"、"scene building"、"scene layout"、"environment art"时使用。基于 MCP 工具的"构建→截图→检查→修正"循环，含需求解析、构建顺序、资产选择、执行纪律与视觉 QA。
---

# UE5 地编工作流（场景搭建）

目标：把用户用自然语言描述的场景，在当前打开的 UE 编辑器里可靠地搭出来，并通过截图验证效果。

## GuLiStrike 制作前必读

在 GuLiStrike 项目制作或修改场景前，必须读取[《GuLiStrike 美术规范》](../../../Progress/RequirementDocument/GuLiStrike美术规范.md)，核对本次参考、灯光标准、模型线稿/三渲二默认及资产例外；下文通用地编经验不能覆盖项目规范。

新制作模型执行“参考图审核 → Blender 一比一还原 → 成品审核 → UE 导入”；布置已有过审资产不重复建模或审核。按规范记录场景参考与候选画面的审核，未经放行不替换正式美术。已有明确过审或免审记录继续有效；等待审核时展示具体结果、链接本技能及规范说明依据。使用 `gulistrike-progress` 维护变更与验收，不在本技能复制风格台账。

## 第一步：需求解析

从用户描述中提取五要素，缺关键信息时**最多问一轮**澄清问题，然后开工：

- **主体**：场景核心是什么（城镇/战斗竞技场/森林空地/建筑内部…）
- **氛围与时间**：阴郁/明亮、清晨/黄昏/夜晚（决定光照方案）
- **内外景**：影响天空、大气、雾的用法
- **玩法用途**：纯观赏还是可供玩（可供玩 → 必须先读 `references/gameplay-blockout.md`）
- **交付物**：改完的场景、截图、还是渲染图

同时**先检查现状**：用 `level.list_level_actors` 和 `util.get_viewport_camera` 了解当前关卡内容与视角，绝不假设是空关卡。

## 核心循环：构建 → 截图 → 检查 → 修正

不要一次性摆完所有 actor 才看效果。按里程碑推进，每个里程碑走一遍循环：

1. **构建（Act）** — 通过 MCP 摆放/修改 actor。小批量进行。
2. **截图（Capture）** — `vision.capture_viewport` 截图；需要固定机位对比时先用 `util.set_viewport_camera` 再截。
3. **检查（Read）** — 读截图做空间推理；美术化阶段按 `references/art-direction-qa.md` 的清单自查（剪影、光向、构图、比例）。
4. **修正（Correct）** — 调整后重复 2-3。

**三角度 QA**（每个大里程碑三个角度都看）：
- 俯视（top-down）：整体布局、间距、路径走向
- 平视（eye-level）：美观、比例、遮挡关系
- 玩家视角：实际游戏观感；GuLiStrike 使用实际飞船/指挥官镜头，180cm 人眼仅适用于确有人物视角的任务

空间问题用工具查，不要只靠猜：重叠用 `actor.get_actor_bounds`，悬浮/穿地用 `actor.line_trace`，报错用 `util.get_output_log`。

## 构建顺序

复杂场景按以下阶段推进，每阶段截图确认后再进下一阶段：

1. **环境壳**：地形/地面/天空/大气
2. **白盒（blockout）**：用基础形状按正确比例搭主体结构，验证布局和流线——此阶段不上美术资产
3. **灯光 + 大气**：主光（DirectionalLight）+ SkyLight + 雾，先定氛围
4. **材质**：给白盒替换成正式资产/材质
5. **细节布置**：道具、植被、小物件
6. **相机与交付**：设置展示机位，截图/渲染，写总结

## 执行纪律（硬规则）

以下规则来自实际踩坑（游戏线程串行执行、编辑器内存态），违反会导致死锁或丢工作：

- **严格串行**：所有 MCP 调用在编辑器 game thread 上执行，**绝不并发调用**，即使操作看似独立。
- **一次一个逻辑步骤**：一次调用做一件事；5 个以上同类操作可用 `util.execute_python` 写一段脚本批量执行。
- **读每个返回结果**：很多工具不抛异常，只在响应体里报告成败。结果不是明确成功就停下来排查（查 `util.get_output_log`）。
- **写后回读验证**：`set_property`/`set_location` 等写入操作，改完用对应的 get 工具回读确认，部分写入路径会静默无效。
- **明确范围保存**：记录初始脏资产，保存本任务明确修改的关卡和资产；不要使用 SaveAll 顺带写入用户或其他任务的改动。MCP 编辑不可靠撤销，必要时保留目标备份。
- **PIE 期间慎用编辑器工具**：Play-in-Editor 时编辑器专用工具行为异常；结果可疑先 `util.is_in_pie` 检查并停止 PIE。
- **调用挂起时**：多半是编辑器弹了模态对话框阻塞了一切——让用户检查编辑器窗口，不要重试轰炸。
- **长操作前提醒用户**：每次调用期间编辑器 UI 会冻结，导入/渲染等长操作先说明。

## 工具选择

优先用 `unreal-mcpython`（功能更全）：
- 摆放/变换：`actor.spawn_from_class`、`actor.spawn_from_object`、`actor.spawn_on_surface_raycast`、`actor.set_location/set_rotation/set_transform`、`actor.set_actor_folder`
- 资产查找：`asset.list_assets`、`asset.find_by_query`、`asset.get_asset_info`、`asset.get_static_mesh_details`
- 检查：`actor.get_actor_bounds`、`actor.line_trace`、`level.list_level_actors`
- 视觉：`vision.capture_viewport`、`vision.capture_actors`、`util.set_viewport_camera` / `get_viewport_camera`
- 批量结构（仅白盒阶段合适）：`unrealMCP` 的 `construct_house`、`create_wall`、`create_tower`、`create_staircase` 等

## 资产选择规则

1. **先用项目内已有资产**：用 `asset.list_assets` / `asset.find_by_query` 在 `/Game/` 下按关键词搜，不要默认摆 `/Engine/BasicShapes/Cube`。商城资产在 `Content/Assets/` 下（只用 MCP 工具查，不要用文件系统遍历该目录）。
2. 摆放前用 `asset.get_static_mesh_details` 确认尺寸（BoundSize），再计算缩放和间距。
3. **加灯前先读现有太阳的强度**学习该场景的光照校准——UE 模板常围绕很小的 intensity 值校准，直接用物理直觉的大数值会过曝。新加的光源强度参照场景内已有光源。
4. 找不到合适资产时，先告诉用户缺什么，再退而求其次用基础形状 blockout，并在交付说明里列出待替换项。

## 世界规则（单位与坐标）

- 单位**厘米**，Z 朝上、X 为前向，旋转用角度（Roll/Pitch/Yaw）
- 常用参考尺寸：人眼高度约 165-180，门约 210×90，普通层高 300-400，走道宽度按相机视野预留
- 内容路径用长包名：`/Game/Folder/Asset.Asset`
- Actor 标签（label，可见、可设、不必唯一）≠ 内部对象名；查询定位优先用标签或类名

## 布局规范

- 所有 actor 归入有意义的 folder（`actor.set_actor_folder`），如 `Environment/Buildings`、`Environment/Props`。
- 地表物体用 `spawn_on_surface_raycast` 或先 `line_trace` 打地面高度，避免悬浮/穿地。
- 间距用 `get_actor_bounds` 校验，宁可疏不要挤。
- 引导玩家视线用光、线条、色彩和地标，不要用墙硬挡；单向落差/不可回头点必须提前示意（telegraph）。

## 验收与汇报

完成后对照最初需求逐项核对（主体/氛围/内外景/交付物），并向用户报告：
- 放了什么：Actor 标签、使用的资产路径
- folder 结构
- 截图文件位置
- 哪些还是白盒临时形状、待替换的资产清单
- 遗留问题（光照待烘焙、性能提示等）

## 参考文档（按需读取）

- **`references/gameplay-blockout.md`** — 场景用于实际玩法/战斗时必读：玩家指标先行、白盒验证、软锁检查、节奏曲线
- **`references/art-direction-qa.md`** — 进入美术化阶段或截图自查时读：构图、光影、比例的自查清单
