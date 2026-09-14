# 双矿单位矿模型 v01

本工程用于 Blender 美术审核。一个矿簇由多个单位矿组成；每个单位矿对应完整、残缺、残矿三个仍有资源的网格，资源归零由程序销毁实体。

## 在 Blender 审核

打开 [OreResourceVariants_v01.blend](OreResourceVariants_v01.blend)，默认停在 `ORE • 24 Unit Review`。

- 左侧蓝矿、右侧红矿；每侧从上到下是01–04号家族，从左到右是 Full（完整）、Partial（残缺）、Remnant（残矿）。
- `ORE • Family Studio` 提供单家族三状态近景，默认显示蓝矿01。主审核场景中也可直接选中模型，按小键盘句点聚焦、鼠标中键旋转。
- `ORE • Cluster Examples` 是两种矿的静态拼装示例，每种26个链接实例：3个中心完整、8个中层残缺、15个外缘残矿。
- 原始 `Scene` 及其中的 Cube、Camera、Light 已保留。

## 家族与三角面

| 矿种编号 | 造型 | Full | Partial | Remnant |
|---|---|---:|---:|---:|
| Blue 01 | 单峰放射 | 6256 | 2328 | 1186 |
| Blue 02 | 双峰错列 | 6256 | 2330 | 1166 |
| Blue 03 | 宽冠扇簇 | 6536 | 2608 | 1366 |
| Blue 04 | 三峰弧脊 | 6396 | 2480 | 1288 |
| Red 01 | 高柱尖塔 | 4860 | 1900 | 1060 |
| Red 02 | 双柱分叉 | 4860 | 1892 | 1036 |
| Red 03 | 刀片扇束 | 5092 | 2116 | 1172 |
| Red 04 | 倾斜脊簇 | 4976 | 1996 | 1116 |

24个资产对象采用 `Ore_Blue_01_Full` 等名称。审核拼装的额外对象共享这些网格，不算新增造型。

## 几何与材质约定

- 米制，Z向上、+Y作为统一朝向；几何相对地面枢轴建模，模型在审核矩阵中只有展示位置偏移。对象旋转为零、缩放为1，底部局部Z为0。
- 完整态最高8米，水平包围盒较长一边12米；残缺高度6.38–7.21米，残矿高度2.49–2.55米。扇簇和脊簇的占地为非正方形，具体XYZ尺寸见清单。
- 同一家族共用母版与确定性随机种子。保留的岩块、短晶和晶体根环坐标相同；晶体截断后使用有浅凹起伏的封闭断面。
- 各零件仍是可编辑的闭合网格岛，顶点组保留 Core、Middle、Seed、Rock 等单件编号。内部穿插用于晶体嵌入岩基，不做昂贵布尔融合。
- 每件2个材质槽：0晶体、1岩基。晶体不透明；`OreColor` 保存逐面颜色，`OreGlow` 保存局部发光权重。材质图已连接这些属性，未依赖外部贴图。
- `UV_LocalTriplanar` 是稳定局部投影UV，允许重叠，用来保持各状态一致；它不是唯一展开的烘焙图集。后续 UE 接入时再确定导出和材质属性转换。
- 模型体积与晶体数量仅是美术数据，不代表游戏矿量。场景地面、文字、灯光和相机均为审核辅助。

## 文件与复现

- [制作脚本](../../../Scripts/Blender/build_ore_resource_variants.py)（项目实际路径：`Scripts/Blender/build_ore_resource_variants.py`）。
- [12张正式预览与清单](../../../outputs/ore-resource-variants-20260910/README.md)。
- 脚本也嵌入工程 Text 数据块。Blender Scripting 打开该脚本后 Run Script 可重新生成；仅重建带本脚本所有权标记的数据。手工改稿请先另存版本再重建。

命令行复现（在项目根目录）：

```powershell
& 'D:\steam\steamapps\common\Blender\blender.exe' --background --python Scripts/Blender/build_ore_resource_variants.py -- --build
& 'D:\steam\steamapps\common\Blender\blender.exe' --background ArtSource/Resources/Ores/OreResourceVariants_v01.blend --python Scripts/Blender/build_ore_resource_variants.py -- --render
```

Blender阶段技术核验已完成，最终美术取舍待用户审核。用户后续要求的UE导入也已完成：24个静态网格和4个材质位于 `/Game/GuLiStrike/Resources/Ores`，详见 [UE导出与导入说明](UEExport/README.md)。矿量阈值和采矿逻辑尚未接入。
