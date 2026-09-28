# 战争机器腿末叉耳与销轴连接 v2

保留的 v2 Blender 模型审核候选。用户随后指定支架外下倾30°、盘保持水平，最新见 [v3审核版](../WarMachine_Slope30_v3/README.md)；该反馈不记为v2通过。保留 v1 的四处盘心承接，根据用户红框反馈将支架与节点的直接插入改为叉耳、轴眼和横向销轴。按用户明确选择，先审核模型，再重绘两张卡。

## 审核入口与操作

[Blender 审核版](WarMachine_LegClevis_Review.blend)已在当前 Blender 打开并保存；默认 Scene 为 `WarMachine_HubSeat_After`，展示完整新模型。使用中键旋转检查，数字键盘 0 回到预设相机。顶部 Scene 下拉切换到 `Connection_Before_After` 可看同视角三维对照。

[可编辑分件源](WarMachine_LegClevis_Editable.blend)保留 279 个命名零件及修改器。审核版是带纹理、线稿和轮廓壳的显示副本；两份文件均未覆盖旧模型。

## 连接前后对照

左侧是已做盘心承接的 v1，右侧是新增叉耳销轴的 v2。均为真实 Blender 网格渲染，未使用生图或图片修饰。

![左v1，右v2](Previews/Connection_Before_After.png)

## 完整模型与局部

![整机三分之四](Previews/After_Hero.png)

![连接斜视](Previews/After_Connection_Oblique.png)

![连接侧视](Previews/After_Connection_Side.png)

![连接俯视](Previews/After_Connection_Top.png)

局部视图隐藏其他机身部件以清楚展示结构，不代表整机缺件。[用户红框反馈](References/User_BeamJoint_Feedback.png)随源文件保留。

## 改动与检查

- 四处节点底座继续与原盘心承接面贴合，悬浮盘的位置、半径、厚度和外圈装甲不动。
- 长支架末端收窄，带实际穿孔的轴眼夹在两片连接耳之间；两片耳板也有实际轴孔。横轴贯穿三孔，两端保留白色轴圈与蓝灰固定帽。
- 机身侧关节、腿末装甲、白色套环、黄色指示灯及底部承接座沿用 v1；四处连接由同一函数构建并镜像。
- 与 v1 比较：4 根长支架修改，新增 32 个关节零件；原 247 个分件中其余 243 个几何保持一致，总计 279 个分件。
- 与最初源模型比较：所有 56 个悬浮盘分件保持原样，武器和原挂点值保持不变，整机包围盒差为 0。
- [几何检查](geometry-check.json)：四处接触高度间隙均为0、盘心同轴误差在浮点范围，改动部件与盘壳无表面相交，左右对称误差约0.000004米。
- [销轴与轴孔检查](joint-check.json)：四根销轴及十二个轴孔同轴，径向间隙为正；新增零件封闭、法线朝外。本轮为静态装配，没有添加骨骼、约束动画或关节玩法。
- [保护文件核对](protected-files-check.json)：原分件源、原绑定成品、原三渲二模型与两份文字prompt均未改变。
- [当前Blender回读](live-readback.json)与[交付清单](delivery-manifest.json)记录路径、可见对象、源版本及文件哈希。

助手已检查这次模型的整机、连接斜视、侧视、俯视及前后对照；这不是用户美术通过。用户模型审核仍待反馈。没有运行 UE、C++ 编译或卡牌生图。

## 审核后两张卡

用户确认本模型后，再制作独立 UE 截图预览副本，沿用 v7 相机、曝光和材质，更新整体、双联炮、悬浮盘三张真实模型截图。两次 image_gen 均输入新整体图和新局部图。

直接读取 v7 的 `FireRate.txt`、`HighSpeed.txt`，文字逐字不改；路径和SHA见保护文件核对。新生成图原样交给用户，不做助手读图评价、选图或自动修订。增加导弹伤害沿用已有图；不把这一选择记为美术终验通过。此阶段不拆层、不覆盖正式 UE 网格或卡牌资源。

## 源文件与复现

- [实际生产连接构造](../../../Scripts/Blender/war_machine_reference.py)
- [v2制作与检查入口](../../../Scripts/Blender/revise_warmachine_clevis.py)
- [共用真实网格渲染流程](../../../Scripts/Blender/revise_warmachine_hub_seat.py)
- [v1中间模型与原始盘心修订记录](../WarMachine_HubSeat_v1/README.md)
- `SourceSnapshot` 保存本轮修改前的连接脚本与渲染脚本；旧源文件和旧成品均保留。

使用 Blender 执行入口的 `prepare` 生成新分件并检查，`render` 渲染同机位对照，`validate` 只核对销轴装配。输出固定写入本版本目录，不执行FBX导出或UE导入。
