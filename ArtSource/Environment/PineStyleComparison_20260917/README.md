# 松树林 × Ship / 扫荡者 / 战争机器

独立 UE 同场景候选，2026-09-17。5 棵松树、4 块岩石、1 片草坪（2,293 个 Foliage 实例），以及三台现有风格单位；不是新建模型或正式关卡替换。

## 打开

源码 UE5.7 当前已打开并保存：

`/Game/GuLiStrike/Environment/ArtReview/PineStyleComparison_20260917/LVL_Pine_Units_Comparison`

供应商源：`D:/BaiduNetdiskDownload/塞尔达松树林/StylizedPineEnvironment/StylizedPineEnvironment`。仅原样复制 Assets 内容至 `/Game/StylizedPineEnvironment/Assets`，保持原包引用。供应商原材质、网格和正式单位资源未修改。

## 实机截图

![同场景总览](Previews/03_overview.png)

[低机位](Previews/04_low_angle.png) · [俯视](Previews/05_top.png) · [指挥官200米](Previews/06_commander_200m.png) · [800米](Previews/07_commander_800m.png) · [1800米](Previews/08_commander_1800m.png) · [单位近景](Previews/09_units_detail.png)

均为源码 UE5.7 真实渲染，1920×1080，无外部调色。指挥官镜头使用55°俯角、45°FOV和标注距离；不是进入PIE后的实战截图。200米镜头跟随扫荡者，800米保留中距离画幅，不能期待450米长的Ship在所有游戏距离都完整入镜；总览和俯视用于检查完整布局。

## 比例与工艺

- 三台单位保留网格 Scale 1；Ship为当前风格舰体和独立轮廓，不包含完整可作战挂件装配。
- 环境以10倍实例尺度试摆；一块岩石8倍，草8–12倍随机。没有把缩放烘进供应商网格，不能将此当作原NaturePack十倍生产方案交付。
- 草每簇37个三角面，场景LOD0理论总计84,841面，单个FoliageType/层级实例组件、无碰撞。未做绘制调用/GPU基准。
- 所选松树LOD0为1,770 / 2,138 / 2,472面；岩石336 / 550 / 460面。供应商所选网格均只有1级LOD，本次不改造它们。
- 太阳对齐项目材质ArtLightDirection (.35,-.55,.76)，强度7，天空补光1.2，固定曝光0；只修改新评审关卡。
- `PineComparison/Stage` 的平整Cube地面及新建审阅材质仅是背景，不是完成的地形资产。

## 当前观察，非用户终验

松树的剪影与岩石大面可以作为候选；原包表面比项目机械单位更细碎、渐变更多。草坪偏黄、密集细条纹明显，800/1800米及俯视覆盖感变弱。

引擎读回：树叶为 Masked / 双面 / Subsurface，草为 Masked / 双面 / DefaultLit，树皮和岩石为 DefaultLit；三台单位为 Unlit 的美术光向三档方案。它们不是同一着色路线。若继续采用，下一步应另行确定配色、分档光影、草远景表现和LOD，不能只称“同为风格化”就判为完全一致。

## 证据与复现

- [尺寸、面数、材质源路径](inspection.json)
- [实例构建记录](scene_build.json)
- [相机参数](camera_presets.json)
- [七机位截图完成记录](gallery.json)
- [保存后重新加载读回](saved_readback.json)
- [UE制作脚本](../../../../Scripts/pine_style_comparison.py)
- [开发记录](../../../../Progress/DevelopmentDocumentation/20260917-松树林与三单位同场景对照.md)

初始脏包为空，只保存新关卡、新地面材质和新FoliageType；切换离开并重新加载后，5棵树、4块岩石、三组单位及2,293个草实例均保留，最终脏包为空。截图序列曾在收尾保存时发生Slate回调重入，已改为先解除回调再保存并完整重拍；不影响正式资产。

未修改C++，不触发原生编译门禁；没有新增自动化测试，未记录动态风摆或完成性能验收。候选场景待用户判断，原NaturePack的A/B审核状态不变。
