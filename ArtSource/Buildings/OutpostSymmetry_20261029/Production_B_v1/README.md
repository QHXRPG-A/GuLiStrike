# 据点 Blender 成品 B_v1

依据用户已确认制作的[参考A_v2](../References_A_v2/README.md)还原，并落实“中间的光柱需要白和阵营色之间能够切换”。用户已明确“导入至UE并实装，删掉之前的据点模型”，据此放行本版本B并完成正式接入，见[UE交付](../UE_Delivery_v1/README.md)。

[打开模型](Outpost_Symmetric_B_v1.blend) · [参考与成品对照](Review.html) · [实际成品设计板](Previews/08_Review_Sheet.png)

![实际Blender成品](Previews/08_Review_Sheet.png)

## 模型内容

- 主场景`OUTPOST_B_v1`，根对象`OP_Root_Midplane`；53个命名网格分件，保留上下Mirror、单段Bevel及可编辑源几何。
- 完整模型经修改器求值后为**4,824三角面**，源半模及未应用倒角部分为1,572三角面。统计只包含单个模型，不包含展示地面、灯、相机、文字和设计板重复实例。
- 尺寸17.877653×20.233376×29.299271米；原点位于水平对称中面，Z范围约±14.649635米。参考图透视与二维笔触不直接当作网格坐标，三轴尺寸按已有护盾3倍目标落实。
- 保留中央八边腰环、四组主装甲支架、成对上下冠部和完整下半部。交付前按参考补足腰环厚度和包覆倒角的蓝灰护板，最后为4,824三角面；上下镜像顶点最大误差约0.000000715米，无非流形边、松散顶点或退化面，所有分件有UVMap。
- 固定外壳使用已选奶油白、蓝灰和深色机构；三档明暗及外轮廓/结构线已制作。实际渲染使用Eevee及Freestyle，未把AI参考图当作三维成品。
- `OUTPOST_B_v1_ReviewSheet`用同一模型的集合实例生成主效果与三视图；源场景`Scene`及默认Cube/Camera/Light保留。

## 中央光柱改色

当前Blender的3D视图按`N`，打开`GuLi`页签，可用**中立白／己方蓝／敌方红**按钮切换，并调节光柱发光强度。外壳和其他固定灯带使用独立材质。

重新打开文件后，即使未加载侧栏脚本，根对象的原生自定义属性仍然有效：

| 属性 | 值与含义 |
|---|---|
| `Core_Color_Mode` | 0=中立白；1=己方蓝`#6AA4BE`；2=敌方红`#A34053` |
| `Core_Emission_Strength` | 中央光柱独立发光强度，默认1.65 |

原生材质驱动已在保存文件重新打开后验证。若需要恢复便利按钮，可运行内嵌文本`OP_Core_Light_Controls.py`；未修改Blender全局自动执行设置。

中央柱体和亮条分别用`OP_Core_Ownership_Surface`、`OP_Core_Ownership_Highlight`。共享节点组`OP_Core_White_Team_Switch`暴露`Neutral White / Team Color / Use Team Color`，发光强度独立驱动，UE现已按实际归属和本地视角绑定。白/己方蓝/敌方红已在正常指挥官PIE确认，外壳及固定装饰灯不随归属改变。

[白色](Previews/01_Hero.png) · [己方蓝](Previews/06_Core_Blue.png) · [敌方红](Previews/07_Core_Red.png)

## 交付与核对

- [主效果](Previews/01_Hero.png)、[正视](Previews/02_Front.png)、[侧视](Previews/03_Side.png)、[背视](Previews/04_Back.png)、[下侧视图](Previews/05_Underside.png)。下侧视图仅临时隐藏展示用地面，模型本体全部保留。
- [几何核对](Evidence/geometry-audit.json)、[按钮与材质切换](Evidence/color-switch-readback.json)、[保存文件重新打开核对](Evidence/saved-file-readback.json)。参考图已打包，三个制作/审核脚本已内嵌。
- [参考审核A依据](approval_A.json)、[最终清单与哈希](manifest.json)。用户“blender已开，一比一制作”作为A_v2制作放行；后续“导入至UE并实装，删掉之前的据点模型”作为[对应源哈希的成品B放行](../UE_Delivery_v1/approval_B.json)。

已按[项目模型流程](../../../../.agents/skills/guli-model-production/SKILL.md)导出FBX并接入正式ModelId 2003；旧UE模型已退役，Blender分件源保持。UE最终观感仍待玩家验收。

UE导入以[批准哈希对应的冻结Blender源](../UE_Delivery_v1/Sources/Outpost_Symmetric_B_v1_Approved.blend)为准。工作`.blend`在导出后又发生保存；其当前内容保留，正式导入所用字节版本由原`.blend1`校验并冻结，两个版本不混同。
