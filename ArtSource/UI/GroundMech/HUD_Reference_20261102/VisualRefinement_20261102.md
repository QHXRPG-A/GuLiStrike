# Ground HUD：第二轮实机截图校验

用户明确要求“你来进行截图校验”，本任务继续实际截图、操作、修正和复查。正式入口为 `/Game/Maps/LVL_CommanderMassPrototype` 的 Ground 席位；本次没有更换正式机甲、卡牌插画、卡池或战斗数值。

## 本轮结论

三张界面均已在实际 PIE 中检查，驾驶舱边框、技能符号、文字间距、卡牌排布和多宽高比锚点已修正。地图缩放越界和强化摘要错误也已修复。当前仍不能记为与参考图一比一通过：地图的环境信息、建筑与道路层次、框架材质细节和发光形态仍有差异；真实战场也不是参考图中的概念环境。

| 项目 | 实际结果 |
|---|---|
| 战斗 HUD | 新开局自动创建 1 个 Ground HUD；生命、燃料、键位和属性显示；两种已获强化名称、图标正确 |
| 正面单击选牌 | 实际鼠标单击折射、灼烧各一次，均等待服务器成功后退出；权威计数为 `06.01=1`、`07.01=1`，`08.01=0` |
| 选牌退出清理 | overlay、capture、target、ticking 均为 false；恢复战斗 HUD |
| 地图操作 | 实际滚轮缩放、鼠标拖动、定位自身、选择据点和 Esc 关闭均观察到响应 |
| 地图边界 | 修复 UMG 默认裁剪覆盖 Slate 设置的问题；最终三个地图控件均为 ClipToBoundsAlways，放大后不越出仪表框 |
| 宽高比 | 近 16:9、16:10、21:9 三种比例均实际打开三张界面并检查；边缘模块贴边，中间操作区保持居中 |
| 快捷键 | 模态内 Esc/F4 已观察；战斗内 M/F4 的电脑控制短按投递仍未可靠触发。已有动作映射与原生动作注入证据，不登记为真实键盘通过 |
| 刷新与性能 | 源码保留普通 0.2 秒、事件帧末合并、显示值变化才提交；本轮没有进行 5Hz 计时、GPU/CPU 或完整战斗性能测量 |

## 最终实机画面

下图为实际窗口截图，包含系统窗口边框；保留电脑控制工具原始 JPEG 字节，未重绘或修图。截图时临时隐藏了 Canvas 调试文字，世界中的调试几何和既有特效仍保留。交付前恢复原调试显示。

战斗 HUD（两种强化已由真实选择获得）：

![战斗 HUD](../../../../data/GroundHUDValidation/Refinement20261102/37-combat-two-owned-final.jpg)

平面大地图：

![平面大地图](../../../../data/GroundHUDValidation/Refinement20261102/38-map-copy-final.jpg)

正面选牌（单击提交，无二次确认）：

![选牌](../../../../data/GroundHUDValidation/Refinement20261102/33-cards-owned-proof.jpg)

## 发现的问题与修正

- 固定 1920×1080 面在宽屏留下浮动边框：改为保持设计比例、按视口扩展设计面；边角、底栏和内容面分别锚定。Chrome 中央伸展，边角保持形状。SizeBox 仅尺寸变化时更新。
- 小字号同色描边让汉字显得拥挤：正文取消白色扩边，较大文字仅保留深色细描边；标题和身份行分离，按钮文本按需缩小。
- 技能、强化与生命图标风格不一致：改为原生 Slate 线条符号，静态失效缓存；HUDuiPRO 继续供无字驾驶舱翼、网格及能量刻度使用，不常驻播放媒体。
- 地图轮廓与高度表达弱：调整高程明暗、侧面和等高线，标记使用共用的玩家箭头、友方圆点、敌方三角及据点菱形。地图仍读取实际高度场。
- 地图缩放后线条覆盖外围框：`Configure()` 设置 UMG 的 `ClipToBoundsAlways`，不再只设置会被 UMG 覆盖的 Slate 默认值。
- Ground 卡牌整体偏低：共享 Director 的直接选择分支上移，并同步命中中心；卡牌间距、卡框、标题、副标题和按钮与参考统一。Commander 默认 `DirectSelection=false`。
- 点击折射后摘要显示“灼烧”及机体图标：服务器实际发放正确。摘要误把不透明卡牌 ID（如 `06.01`）当成英文标识；现从真实卡牌定义读取标题文字键，映射缩写和图标，未知卡牌保留真实标题及通用强化符号。实点两张候选复查已正确。

## 多宽高比与交互证据

| 比例 | 原生 HUD 尺寸 | 截图 |
|---|---|---|
| 近 16:9 | 1914.681×1080 | 最终上方三图；缩放/拖动/定位/选中见 39–42 |
| 近 16:10 | 1723.691×1080 | [战斗](../../../../data/GroundHUDValidation/Refinement20261102/15-combat-responsive-16x10.jpg)、[地图](../../../../data/GroundHUDValidation/Refinement20261102/16-map-responsive-16x10.jpg)、[选牌](../../../../data/GroundHUDValidation/Refinement20261102/17-cards-responsive-16x10.jpg) |
| 近 21:9 | 2513.019×1080 | [战斗](../../../../data/GroundHUDValidation/Refinement20261102/12-combat-responsive-21x9.jpg)、[地图](../../../../data/GroundHUDValidation/Refinement20261102/13-map-responsive-21x9.jpg)、[选牌](../../../../data/GroundHUDValidation/Refinement20261102/14-cards-responsive-21x9.jpg) |

16:10/21:9 截图在锚点修复后、最后的字体和强化摘要修正前拍摄；最后两项不改变锚点。窗口边框与 DPI 导致原生比例与请求的 1920×1080、1920×1200、2520×1080 略有差异。未把最后版本的全部比例重拍写成已执行。

[地图拖动](../../../../data/GroundHUDValidation/Refinement20261102/40-map-drag-final.jpg)、[定位自身](../../../../data/GroundHUDValidation/Refinement20261102/41-map-locate-final.jpg)、[选择据点](../../../../data/GroundHUDValidation/Refinement20261102/42-map-selection-final.jpg)、[权威获得计数与清理状态](../../../../data/GroundHUDValidation/Refinement20261102/owned-both-settled.json)可分别核对。早期拖动未明显变化，本轮最后一次实际拖动已观察到地形和全部标记一起平移。

## 导表与稳定性修复

前次记录的 `FillDataTableFromJSONString` 并未真正绕过资产工厂：引擎内部仍调用 FactoryCreateText，重建被 Python/界面强引用的 UObject 时会断言。本轮新增仅编辑器使用的 `GuLiDataTableImportLibrary::UpdateFromJson`：先在强引用临时表校验 JSON，再将行复制到同一目标对象；拒绝 PIE 内导入。仅公共文字/HUD 两表改走该接口，其余表的管线保留。

非法 JSON 拒绝且原表未改；315 行公共文字、34 行 HUD 表成功保存回读，65 条 HUD 文案与共享 StringTable 更新。退出 PIE 后重复导入及重启后回读均完成。原源表由 excelize 管理，生成结构未手改。

本轮新增符号的首版还触发过 `TArray::Add` 源地址来自自身数组的断言，已复制首点后追加并重编译复查。两次源编译失败（命名空间/格式化和继承成员同名）也已修正。失败日志保留，不将这些尝试记为通过。

全部构建沿用本任务已有许可，使用 `D:/UnrealEngine-5.7` 的 `GuLiStrikeEditor Win64 Development`。最终强化映射构建 14 步/22.63 秒、字体构建 4 步/9.96 秒，均退出码 0；最终引擎和项目 BuildId 均为 `ec746d14-0746-471d-9ee9-2dfd468f881d`。本轮没有重新核验未构建插件的 BuildId。

## 交付状态与剩余边界

最终编辑器 PID 18976，Ground PIE 保留战斗 HUD；测试局内真实取得折射和灼烧各一次。下次 PIE 窗口恢复为原 3840×2037，席位提示和 Canvas 调试显示已恢复，地图/内容脏包为空。编辑器世界仍为指定地图，18 个 GroundHUD/GroundRogue 相关实体已只读回读；本轮无需更改场景布局。

完整受伤/重生、燃料耗尽、闪现、双准星遮挡、命中/护盾/击杀与网络重复回执未在本轮完成；不由静态画面替代。既有 `M_MassBulletTrail` 着色器错误与黑色长条仍属于场景表现问题，本轮没有扩展修改。用户尚未确认最终视觉通过。

[全部证据与哈希](../../../../data/GroundHUDValidation/Refinement20261102/manifest.json) · [最终交付状态](../../../../data/GroundHUDValidation/Refinement20261102/handoff-state.json) · [前次截图记录](VisualAudit_20261102.md)
