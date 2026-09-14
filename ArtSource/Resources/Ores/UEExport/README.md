# 单位矿 UE 导出文件

24个FBX由当前Blender审核模型导出，每个文件只包含一个单位矿，命名为 `SM_Ore_Blue_01_Full.fbx` 等。逐件源对象、尺寸、面数、材质及SHA256见 [export_manifest.json](export_manifest.json)。

- UE目录：`/Game/GuLiStrike/Resources/Ores`。
- `Meshes/Blue` 与 `Meshes/Red` 各12个静态网格；`Materials` 保存蓝红各Crystal/Rock两个材质。
- FBX对象位置、旋转归零，缩放为1，模型底部Z=0；审核矩阵的位置偏移已去除。FBX保留米制单位信息，UE导入时转为厘米。
- RGB保存 `OreColor` 的线性值，Alpha保存 `OreGlow`，导出使用 `colors_type='LINEAR'`。UE导入/构建的内部sRGB往返后，VertexColor材质节点获得原始归一化字节。不要把RGB再次导出为sRGB，否则UE材质会偏亮。
- 保留切面法线与两材质槽。晶体为不透明、少量Clear Coat、低强度局部发光；岩基为粗糙不透明材质。无需外部颜色贴图。
- Full/Partial/Remnant是不同资源状态，独立保存；不是距离LOD。基础碰撞由UE生成，游戏采矿逻辑另行接入。

## 复现

在打开源 `.blend` 的Blender中运行 [导出脚本](../../../../Scripts/Blender/export_ore_resource_variants_ue.py)。脚本只编辑临时副本，结束后恢复原审核场景与选择，不保存或重载源工程。

关闭占用同一项目的UE实例后，从项目目录运行源码版引擎的启动脚本：

```powershell
& 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\UE5.7\test1\GuLiStrike.uproject' '-ExecutePythonScript=D:\UE5.7\test1\Scripts\import_ore_resource_variants.py'
```

[导入脚本](../../../../Scripts/import_ore_resource_variants.py)仅更新指定目录内带本生成器元数据的资源；遇到同名但不属于本次生成器的资产会停止。FBX导入应在正常UE启动脚本中运行，避免在网络回调中同步执行。

[导入开发记录](../../../../Progress/DevelopmentDocumentation/20260910-双矿单位矿模型UE导入.md) · [导入报告](../../../../outputs/ore-ue-import-20260910/ue_import_report.json)
