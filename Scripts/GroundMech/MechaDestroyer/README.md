# 游骑兵机动、相机与炮台制作入口

游骑兵使用 `BP_GroundMech_Light` 和 4001/A4B2。玩法参数在 `data/Excel/GuLiStrikeMech.xlsx` 维护。以下命令从项目根目录执行。编辑器步骤使用已加载项目原生模块的空闲编辑器。

| 需要做的事 | 当前入口 |
|---|---|
| 将修改过的 Excel 导出，再导入五张机动表、同步动画与清单 | 依次运行 `python Tools/DataPipeline/export_data_from_excel.py` 和 `python Scripts/ue_exec.py Scripts/GroundMech/MechaDestroyer/import_mobility_tables.py` |
| 读取用户已摆好的蓝图预览视角 | `python Scripts/ue_exec.py Scripts/GroundMech/MechaDestroyer/capture_3c_preview_camera.py`。回执给出本次采集文件的路径。 |
| 将选定的视角应用到 Camera.Free | `python Scripts/GroundMech/MechaDestroyer/author_mobility_tables.py --camera-capture "<本次采集文件>"`，然后运行正式导出及导入。 |
| 从已发布的当前文本表恢复 3C 参数到源表 | 在重新导出 Excel **之前**运行 `python Scripts/GroundMech/MechaDestroyer/author_mobility_tables.py --restore-3c`。这会按行恢复机动、步态及 Free 相机参数。日常改表直接走第一行。 |
| 读回动画接线、表、相机和清单 | `python Scripts/ue_exec.py Scripts/GroundMech/MechaDestroyer/read_ground_mobility_assets.py` |
| 在原地图重建或更新机动区 | 打开 `/Game/Maps/LVL_CommanderMassPrototype`，运行 `python Scripts/ue_exec.py Scripts/GroundMech/MechaDestroyer/save_mobility_review_scene.py`。它按原标签创建或更新平台、18° 坡台、四级台阶、左右标靶和入口，保存后读回 10 个实体。 |

当前机动区入口为 `GroundMobility_Entry=(-2800,50000,1149.04)`。常规导入调用既有动画同步钩子。重建整张正式动画图时才使用 `install_ground_mobility.py`，其中也保留了 Mouse2D 输入绑定。新回执默认写入 `Artifacts/GroundMobility/`。
