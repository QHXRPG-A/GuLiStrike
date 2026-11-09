---
schema: guli-progress/v1
id: ARC-20261102-010
work_id: WORK-20261102-001
kind: archive
role: root
title: GroundHUD属性合并与仪表轮廓截图修正
areas: [ui, presentation, combat, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-11-02'
updated: '2026-11-02'
summary: 按用户要求将无单位属性并入左下生命面板，缩小字号；补充独立仪表轮廓、双准星暗衬、强化符号与真实建筑地图信息。
next_action: 继续地图和材质还原，处理编辑器首次选牌加载表现，补齐真实输入、战斗反馈与性能验收。
relations:
  work_items: [WORK-20261102-001]
status_note: 最后构建10步/12.64秒成功，三种画幅的合并战斗面板已截图；原始截图25张中1张错误窗口排除。PID3276保持Ground战斗PIE，无脏包；整体一比一尚未通过。
---

# Ground HUD 属性合并与仪表轮廓截图修正

用户“你来进行截图校验”授权本HUD任务助手读图；沿用同一任务已批准的停止PIE、编译与重启范围。过程中用户明确要求属性不带单位、与左下血条合并且字号缩小。这两条覆盖原参考图对应布局，原参考图片与哈希保留。

生命面板改为486×224，底边锚点不变；三项属性在血条下横排，标签16号、数值18号，详情入口同行。原右侧属性面板与专属形状分支删除。常驻射速/移速移除单位；详情文案由excelize修改源表C265，经原管线导出、DataTable导入、StringTable重建保存，315行实际内容与源JSON逐行一致。

外壳采用独立阶梯/不对称轮廓和玻璃渐变，继续使用HUDuiPRO静态输入；折射采用白色弹头与橙色反弹轨迹，闪现采用前进回旋箭头。主准星与命中X增加暗衬、画幅适配；枪口菱形对齐时缩小保留。建筑目录在本地Ground绑定时复制名称和真实包围范围，地图显示轮廓、侧面与真实名称；普通单位空间聚合按阵营合并。没有增加媒体播放、地图SceneCapture或每帧Actor搜索，0.2秒采样/帧末事件合并不变。

## 证据和范围

最后合并版在近16:9、16:10、21:9截图复查，无属性文字挤压或边界溢出；实际控件父级和字号也已回读。此前本轮实点折射后权威计数06.01=1，卡牌退出资源清理正确；地图实际选择矿厂显示真实名称。原始JPEG共25张，1张错误窗口明确排除，其余也包含失败输入和加载过程，不能一概算通过。

源码版`D:/UnrealEngine-5.7`、目标`GuLiStrikeEditor Win64 Development`四次构建均退出0：14步/21.43秒、10步/18.81秒、4步/9.48秒、10步/12.64秒。最终引擎/项目BuildId均为`ec746d14-0746-471d-9ee9-2dfd468f881d`。未改引擎或插件源码。

最终PID3276，正式地图`/Game/Maps/LVL_CommanderMassPrototype`的Ground战斗PIE保持开启，当前新局无已获强化；18个相关编辑器实体与三个地图裁剪读回，地图/内容脏包为空。Canvas调试、席位提示与下次PIE窗口3840×2037已恢复。

当前仍未达到一比一验收：地形细节和材质层次还有差距；战斗键盘短按和详情鼠标点击未可靠验证；编辑器首次开牌短暂棋盘格/材质编译尚待处理。命中X虽已改绘制，未进行真实伤害/吸收/击杀截帧、完整战斗、刷新计时或CPU/GPU性能验证。

完整记录见[第四轮图文报告](../../ArtSource/UI/GroundMech/HUD_Reference_20261102/VisualDetails_20261102.md)、[截图/源码/构建清单](../../data/GroundHUDValidation/Detail20261102/manifest.json)、[交付读回](../../data/GroundHUDValidation/Detail20261102/handoff-state.json)。本轮未迁入新的外部资源。
