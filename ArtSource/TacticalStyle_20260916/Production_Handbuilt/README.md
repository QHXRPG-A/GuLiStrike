# 扫荡者 / 战争机器：按已验收图制作

建模依据是 `../Concepts` 中两台单位各四张、共八张用户已验收参考。设计不可再自行简化或另做风格迁移。`approved_design.json` 记录具体文件和哈希。

本目录由 Blender 解析几何建立，无 Tripo 网格或贴图依赖。保留橙红、暖白、蓝灰机械结构；按设计分别制作规则曲面、平整面板与统一倒角，不添加随机凹凸和脏纹理。

当前交互文件为 `Tactical_ApprovedDesign_Review.blend`。场景“扫荡者_按验收图制作”和“战争机器_按验收图制作”是最新制作版；其中保留了八张已验收图，当前打开战争机器分件场景，图像编辑器显示战争机器效果图作对照。已验收的是参考设计，最新模型还原度尚待用户查看。

- `*_Handbuilt_Editable.blend`：命名分件、可编辑倒角与法线修改器。
- `*_Handbuilt_Rigged.blend`：单材质图集、刚性绑定及固定相机。
- `SM_*_Crowd.fbx` / `SK_*.fbx`：静态 / 骨骼导出，制作方向 +X、Z 向上，米制。
- `T_*_BaseColor.png`：2K纯色、规则线条图集，不含场景灯光和随机法线。
- `*_Handbuilt.glb`：便携模型预览，运行时机械元数据以FBX与JSON为准。
- `*_delivery.json`：实际网格统计、尺寸、刚性权重、挂点和机械轴。
- `*_Packaged_*.png` / `Sweeper_Pitch*.png`：实模与机械姿态预览。

扫荡者机枪抬起对应骨骼局部 Y 负角，范围 −15°～+60°；四轮绕 Y 轴转动。所有金属分件单骨骼满权重。战争机器两只背舱固定45°，四个侧炮枪管与两组发射出口分开记录。

扫荡者已导入源码版 UE5.7，静态网格、骨骼网格、材质、2K图集与独立预览关卡均已保存并读回。目录为 `/Game/Commander/Units/Tactical/Sweeper`，预览关卡为 `/Game/Commander/Units/Tactical/Preview/Sweeper/LVL_Sweeper_Review`。`UE_Sweeper_Review.png` 是引擎实际渲染图，`Sweeper_ue_import.json`、`Sweeper_ue_skin_readback.json` 记录尺寸、骨骼和刚性权重。UE静态网格5,456三角面，导入器剔除退化面后比FBX少26面；单材质、三套UV保留，最大尺寸差小于0.0001厘米。主编辑器已打开静态网格预览。

战争机器当前交付Blender模型及导出文件，尚未导入UE。两台单位的实战模型引用、ISM机械动作、中远景LOD和性能采样仍未完成；整体项目任务仍以Progress开发记录为准。

本次FBX读回：扫荡者5,482三角面、战争机器14,018三角面；每台一个材质槽、三套UV、RigidPart顶点色均保留，Blender骨骼分别7/2根。当前制作网格高于原游戏预算，需要在保留已验收设计的前提下制作游戏LOD，不应把当前统计当作原计划预算已达成。读回尺寸误差小于0.000002米。扫荡者−15/0/35/60度姿态的刚性蒙皮误差小于0.000002米，固定主体位移为0；实战动作尚未接入。UE骨骼网格另保留FBX容器根Rig_Sweeper。

扫荡者侧甲弧度修正：两侧是镜像的外鼓成形装甲，不能建成平板。主体、白色涂装和线条共用曲面拓扑；曲面法线按规则截面计算，边缘保留倒角。现版本已更新交互文件、分件源、骨骼文件、图集与两份FBX。正面、俯视、三分之四及灰模预览已检查；整体尺寸、轮轴和枪口坐标不变。`shoulder_validation.json` 记录封闭网格、对称、骨骼和FBX读回结果。修改前资产在 `Before_Shoulder_Curvature`，交互文件中旧场景为“扫荡者_侧甲弧度修正前”。

战争机器本轮重建由 `Scripts/Blender/war_machine_reference.py` 提供243个命名零件：层叠主甲、厚边白色面板、四个悬浮盘及四面覆盖甲、关节和侧炮护甲、每侧双炮、双背舱。后视以用户本轮第五张平行背舱图为准，第三张外撇版本不采用。双舱几何轴实测45.00001度，外撇小于0.00001度；FBX镜像误差小于0.000007米。`WarMachine_reference_validation.json` 记录结果。分件源保留倒角和法线修改器，包装时仅删除完全包藏面，共删除1,562三角面。回退文件保存在 `Before_WarMachine_Reference_Rebuild`，交互文件内旧场景为“战争机器_本轮重制前”。

UE预览关卡保存时，项目已有 `GuLiFlightNavigationWorldValidator` 对该独立美术关卡返回NotValidated，触发一次handled ensure；关卡和截图成功生成。这不代表项目级数据验证通过。本轮未改C++、全局渲染设置、主地图或自动化测试文件。
