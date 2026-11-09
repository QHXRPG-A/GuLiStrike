---
schema: guli-progress/v1
id: ARC-20261102-009
work_id: WORK-20261102-001
kind: archive
role: root
title: GroundHUD中文字体与仪表细化截图修正
areas: [ui, presentation, combat, performance]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-11-02'
updated: '2026-11-02'
summary: 第三轮实机截图接入真实中文字重、技能仪表与地形侧壁，修正标题及行位置；发现并修复字体重复导入后的Slate缓存崩溃。
next_action: 继续参考材质与地图信息表现核对，补齐战斗快捷键、完整战斗和性能验收；等待玩家最终视觉决定。
relations:
  work_items: [WORK-20261102-001]
status_note: 25张原始截图，最终源码版Editor构建4步/10.74秒成功；近16比9与21比9复查，16比10截图在最后行位置修正之前。PIE保留战斗，无脏包；一比一与完整功能性能未验收。
---

# Ground HUD 中文字体与仪表细化截图修正

沿用用户“你来进行截图校验”与同一HUD任务的编译、结束PIE及重启许可。第三轮增加真实中文Medium/Bold、技能总框和键位边框、标题分区、地图符号图例及基于高度场边缘的地形侧壁；保留HUDuiPRO静态输入、已有卡图/六层视差、直选权威流程和0.2秒/事件更新合同。

卡牌标题74→90并增高容器，顶部副标题上移；实机截图发现属性文字碰到分隔线、图例文字偏低，已调整行位置。最后构建在近16:9与21:9复查，16:10三界面截图发生于最后这次共用行位置调整前。实点折射提交后权威计数与摘要正确，退出后overlay/capture/target/ticking均关闭。地图内F4及Esc已观察，战斗内M电脑控制短按仍未可靠触发。

字体首次运行崩溃栈位于Slate复合字体缓存：配置助手替换数组而未失效旧缓存。现替换前FlushCompositeFont/FlushObject，重建后MakeDirty；连续两次导入与后续PIE启动复查未复现。失败截图和日志保留。富文本初次派生的JSON结构错误也保留失败记录，正式脚本使用完整ImportText继承，只改变字体和描边字段。

## 资源清单

| 最终项目内文件夹 | 来源 | 内容与用途 | 依赖和边界 |
|---|---|---|---|
| `ArtSource/UI/GroundMech/HUD_Reference_20261102/Fonts` | [Noto CJK官方仓库](https://github.com/notofonts/noto-cjk/tree/main/Sans/OTF/SimplifiedChinese) | 原始Medium/Bold OTF、来源哈希、版权和OFL1.1 | 未修改字体字节；只导入这两个字重，无其他字体家族 |
| `/Game/GuLiStrike/GroundMech/UI/Fonts` | 上述OTF经FontFileImportFactory导入 | 两个Stream字重与复合字体，HUD表UIFont引用 | 原生UMG/Slate；地图Ground正常入口，无外部Actor或示例地图依赖 |
| `Content/GuLiStrike/GroundMech/UI/Fonts/License` | 字体OFL及内嵌版权声明 | 打包随附许可和声明 | DefaultGame.ini加入NonUFS清单；尚未执行打包验收 |
| `/Game/GuLiStrike/GroundMech/UI` | 项目已有共享卡牌富文本表 | Ground派生DT_GroundCardTextStyles | 保留Excel驱动颜色/字号规则；Commander继续原样式；源表更新自动同步 |

资源详见[字体说明](../../ArtSource/UI/GroundMech/HUD_Reference_20261102/Fonts/README.md)；不读取UE二进制资产内容。

## 构建、验证和交付

源码引擎`D:/UnrealEngine-5.7`、目标`GuLiStrikeEditor Win64 Development`四次构建退出码均0：17步/33.14秒、4步/4.53秒、6步/10.70秒、4步/10.74秒。最终引擎与项目BuildId均为`ec746d14-0746-471d-9ee9-2dfd468f881d`；本轮未重编插件。7份Python AST与定向差异检查通过。

35行HUD、315行公共文字、字体与Ground富文本表均保存回读。最终PID37136，正式地图`/Game/Maps/LVL_CommanderMassPrototype`的Ground新开局自动创建HUD；18个相关编辑器实体回读，三个地图控件裁剪一致，地图/内容无脏包。保留战斗PIE，恢复Canvas调试、席位提示与下次PIE原3840×2037尺寸。

完整证据见[第三轮图文报告](../../ArtSource/UI/GroundMech/HUD_Reference_20261102/VisualFidelity_20261102.md)、[25张截图及源数据哈希](../../data/GroundHUDValidation/Fidelity20261102/manifest.json)、[最终交付状态](../../data/GroundHUDValidation/Fidelity20261102/handoff-state.json)。前两轮图文报告仅修复了截图相对路径，不改历史事实。

本轮没有执行5Hz计时、完整战斗、CPU/GPU性能或打包验收；实际地形、建筑道路信息、框架材质与发光仍不同于参考，用户尚未确认一比一通过。既有世界调试几何和非本任务特效未扩展修改。
