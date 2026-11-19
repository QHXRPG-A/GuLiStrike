# 中央光柱自发光再翻倍

用户要求“光柱自发光强度再增大2倍”，相对上一版白腰带与全灯2倍版本：

| 发光区域 | 上版 | 当前 | 本次倍率 |
|---|---:|---:|---:|
| 中央光柱柱面 | 4.4 | 8.8 | 2 |
| 中央光柱亮条 | 12 | 24 | 2 |
| 青绿灯槽 | 5.6 | 5.6 | 1 |
| 蓝色灯槽 | 8.0 | 8.0 | 1 |

正式材质`/Game/GuLiStrike/Buildings/OutpostSymmetric/Materials/M_OutpostSymmetric_Toon`已编译并保存，错误列表为空。共享HLSL生成器同步，重跑不叠乘；白腰带、白／阵营色切换、1.35色度保护、原Bloom、60秒自转与8秒浮动保持。

[逐项倍率及差异核对](change-readback.json) · [材质编译保存与49引用](material-delivery.json) · [修改前HLSL](surface-before.hlsl) · [当前HLSL](surface.hlsl) · [修改前材质图](material-before.json) · [当前材质图](material-after.json)

新旧HLSL仅版本标记和两个光柱峰值变化；4824三角面及49个预览引用相同。本次没有构建C++、启动PIE或新增截图。玩家在`/Game/Maps/LVL_CommanderMassPrototype`以指挥官进入，观察R4C4中立白、R1C4己方蓝和R7C4敌方红，验收最新光柱强度；玩家视觉状态待反馈。

[当前开发记录](../../../../Progress/DevelopmentDocumentation/20261028-据点巨物尺寸与轻微浮动.md)
