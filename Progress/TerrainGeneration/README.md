# LVL_Main 陨石坑地形

本目录保存 2026-08-26 对 `/Game/Maps/LVL_Main` 的灰盒地形改造资料。

- `LVL_Main_before_crater_20260826.png`：修改前由 VibeUE `LandscapeService.export_heightmap` 导出的可恢复高度图。
- `LVL_Main_crater_plateau_8161.png`：与现有 8161×8161 Landscape 精确匹配的 16 位高度图。
- `LVL_Main_after_crater_20260826.png`：保存后从关卡重新导出的高度图；与目标高度图逐像素完全一致。
- `LVL_Main_crater_plateau_preview.png`：快速俯视预览，不参与导入。
- `LVL_Main_editor_clay_isometric.png`：编辑器内全场几何检查图。
- `LVL_Main_editor_clay_plateau.png`：编辑器内中央高原与四向坡道检查图。
- `generate_crater_heightmap.py`：可重复生成上述目标高度图的脚本。

设计约束来自 `Progress/RequirementDocument/20260826-5v5大战场玩法草案.md`：椭圆形巨型陨石坑作为实体边界，盆地内部保持连续可通行，中央高原作为多区可见地标但不阻断两侧环形路线，并以四个宽坡道提供高原通达性。

## 编辑器验证

- Landscape：8161×8161，XY Scale 100，约 8.16 km × 8.16 km。
- 中央高原顶面：约 +65 m，实测坡度 0°。
- 四向高原坡道：中段实测约 18.5°。
- 两侧基地候选区与外圈绕行区：实测约 0.34°。
- 陨石坑实体壁：实测约 50.5°；坑缘峰值约 +242 m。
- 原有 PlayerStart、4 个 Pickup、4 个 Spawner 已按修改前离地高度重新贴合地表；`DEMO_WM01_Walking` 已移至中央高原顶面。
- 已请求重新构建导航。

重新生成：

```powershell
python Progress/TerrainGeneration/generate_crater_heightmap.py `
  Progress/TerrainGeneration/LVL_Main_crater_plateau_8161.png `
  --preview Progress/TerrainGeneration/LVL_Main_crater_plateau_preview.png
```
