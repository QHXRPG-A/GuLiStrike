# 第四轮：合并属性、仪表轮廓、准星与地图标记

用户明确要求助手截图校验，并继续授权同一HUD任务的编译重启。本轮又明确要求“属性不显示单位”和“属性跟左下角的血条板块合并，字号小一点”。因此参考图中的右侧属性面板和单位标注不再作为还原目标；这两项以最新用户指令为准，原始参考保留。

## 实装变化

- 左下生命面板扩为486×224设计单位，底边仍锚定原安全区。武器等级、射速、移速在血条下方横排，标签16号、数值18号；详情入口随之移动。删除原右侧属性面板及专属形状分支。常驻和展开详情都只显示数值。
- 生命、喷气、小地图和强化外壳分别采用阶梯或不对称轮廓，增加淡玻璃渐变与局部刻线。继续复用HUDuiPRO静态翼形、网格和能量刻度；未新增常驻媒体或场景捕获。
- 主准星增加深色衬线并按画幅缩放。枪口菱形保留真实投影，重合时缩小置于主准星内，不再直接隐藏。命中X同比放大；本轮未获得实际命中/击杀截帧证据。
- 折射采用白色弹头和暖橙反弹轨迹，与射击图区分；闪现采用向前回旋箭头。
- 地图从既有建筑目录复制名称与实际碰撞包围范围，在本地Ground绑定时缓存。建筑显示范围轮廓，小图增加高度侧面，选择后显示真实名称；同一区域同阵营普通单位合并，按既有可见成员绘制，无每帧Actor扫描。

普通数据计时器仍为0.2秒，连续位置采样仍由独立0.2秒限制，事件帧末合并；地图旋转、准星与短动画按显示帧处理。建筑范围是玩法包围范围，不是建筑美术模型的精确俯视图。

## 截图与交互证据

原始JPEG全部来自Windows Computer Use，未修图。一次返回了错误窗口，保留为`03-invalid-capture.jpg`并排除HUD结论。编号15、16是快捷键试验，文件名不能作为内容通过的依据。详见[截图与源文件清单](../../../../data/GroundHUDValidation/Detail20261102/manifest.json)。

| 检查 | 实际证据与边界 |
|---|---|
| 合并后的16:10 | [战斗画面](../../../../data/GroundHUDValidation/Detail20261102/22-combat-merged-16x10.jpg)，[实际控件与字号](../../../../data/GroundHUDValidation/Detail20261102/readback-merged-16x10.json) |
| 合并后的21:9与近16:9 | [21:9画面](../../../../data/GroundHUDValidation/Detail20261102/23-combat-merged-21x9.jpg)、[交付战斗画面](../../../../data/GroundHUDValidation/Detail20261102/24-combat-merged-handoff.jpg)；三种宽高比均未看到属性文字挤压或越界 |
| 属性无单位 | [详情画面](../../../../data/GroundHUDValidation/Detail20261102/14-details-no-units.jpg)，[315条文案逐行比较与保存](../../../../data/GroundHUDValidation/Detail20261102/import-text-final.json)；该详情画面在最后合并位置之前拍摄 |
| 建筑信息 | [实点先驱号兵营](../../../../data/GroundHUDValidation/Detail20261102/08-building-selected.jpg)，已显示名称、距离、坐标与阵营 |
| 地图/选牌多画幅 | [16:10地图](../../../../data/GroundHUDValidation/Detail20261102/17-map-16x10-final.jpg)、[16:10卡牌](../../../../data/GroundHUDValidation/Detail20261102/18-cards-16x10-final.jpg)、[21:9地图](../../../../data/GroundHUDValidation/Detail20261102/20-map-21x9-final.jpg)、[21:9卡牌](../../../../data/GroundHUDValidation/Detail20261102/21-cards-21x9-final.jpg)；这些拍于最终属性合并前，合并只改变战斗面板 |
| 实点折射与清理 | [服务器计数与退出资源](../../../../data/GroundHUDValidation/Detail20261102/owned-ricochet-final.json)，06.01=1，overlay/capture/target/ticking均关闭；[新摘要符号](../../../../data/GroundHUDValidation/Detail20261102/12-ricochet-owned.jpg)。该回执来自本轮较早的PIE局次 |

最后战斗状态的鼠标点击详情位置未打开详情；当时游戏捕获鼠标并改变镜头，此帧保留为25号，不作为详情入口点击通过的证据。原生详情入口本轮已打开并确认无单位文案，真实键盘I仍待单独确认。

## 构建和交付状态

四次源码版`GuLiStrikeEditor Win64 Development`构建均退出0：14步/21.43秒、10步/18.81秒、4步/9.48秒、10步/12.64秒。最后一次包含属性合并，模块BuildId与源码引擎相同；见[最终模块基线](../../../../data/GroundHUDValidation/Detail20261102/build-baseline-merged.json)。未改插件或引擎源码。

交付时编辑器PID3276，`/Game/Maps/LVL_CommanderMassPrototype`的Ground战斗PIE保持运行。18个相关编辑器实体及三个地图裁剪读回；脏地图/内容为空，Canvas调试、席位提示和下次PIE窗口3840×2037已恢复。当前新局没有已获强化，折射点击证据属于本轮先前局次。见[交付读回](../../../../data/GroundHUDValidation/Detail20261102/handoff-state.json)。

## 未完成的验收

当前不是“一比一已通过”。真实关卡地形和建筑内容与概念场景不同，地图地形细节、整体材质层次仍有差距。电脑控制的战斗M短按未可靠打开地图，不能把原生入口调用通过等同于物理键盘通过。编辑器首次打开卡牌曾短暂显示棋盘格并编译材质，稳定后卡面和后续同进程开场正常；首开加载表现仍需处理。

没有进行普通/事件刷新实际计时、完整战斗、CPU/GPU性能或打包验收。截图只能证明所示画面；真实伤害、护盾吸收、击杀与准星重合状态仍需专门验证。
