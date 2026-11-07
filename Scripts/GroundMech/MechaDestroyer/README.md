# 游骑兵机动、相机与炮台制作入口

游骑兵使用 `BP_GroundMech_Light` 和 4001/A4B2。玩法参数在 `data/Excel/GuLiStrikeMech.xlsx` 维护。以下命令从项目根目录执行。编辑器步骤使用已加载项目原生模块的空闲编辑器。

## 游骑兵动画目录

在 UE 内容浏览器打开 `/Game/GuLiStrike/GroundMech/Animations`。在 `Source/A4B2` 手调 14 段项目自有源动画，在 `A4B2` 查看播放用动作、混合空间、转身蒙太奇、正式动画蓝图及机枪后坐曲线。A／D 对应 `Anim_Mecha_Strafe_L` 和 `Anim_Mecha_Strafe_R`。两个目录的同名片段使用不同骨架。商城包继续保留原资源。

闪现直接编辑 `A4B2/Anim_Mecha_Blink`（前）及同名 `_back`、`_left`、`_right` 三份方向动画。在骨骼树选 `MiddleJoint`、`cockpit`、`GunArm_L/R` 或腿骨，通过 Additive 图层轨道编辑关键帧并保存；`Gun_L/R` 保持枪管装配关系。四份资源统一为 150 fps、82 采样点、0.54 秒，`BlinkConceal=0.02s`、`BlinkRecovery=0.24s`。技能表决定阶段时长，最终方向混合空间由权威时钟显式求值；日常导表不会重制这些手调曲线。当前操控和完成状态见[游骑兵闪现](../../../Progress/Gameplay/游骑兵闪现.md)。

手调横移、前进或后退片段后，先用 `Scripts/ue_exec.py` 执行本目录的 `export_b2_source_animation_data.py`，将项目源动作重新采样，再执行下方步态导出、导入流程。正式产物由项目脚本写入 `Animations/A4B2`。机枪后坐使用该目录的 `CF_Machinegun_Recoil`，独立枪组件使用原生 `GuLiGroundMechWeaponAnimInstance`。

| 需要做的事 | 当前入口 |
|---|---|
| 将修改过的 Excel 导出，再导入五张机动表、同步动画与清单 | 依次运行 `python Tools/DataPipeline/export_data_from_excel.py` 和 `python Scripts/ue_exec.py Scripts/GroundMech/MechaDestroyer/import_mobility_tables.py` |
| 读取用户已摆好的蓝图预览视角 | `python Scripts/ue_exec.py Scripts/GroundMech/MechaDestroyer/capture_3c_preview_camera.py`。回执给出本次采集文件的路径。 |
| 将选定的视角应用到 Camera.Free | `python Scripts/GroundMech/MechaDestroyer/author_mobility_tables.py --camera-capture "<本次采集文件>"`，然后运行正式导出及导入。 |
| 从已发布的当前文本表恢复 3C 参数到源表 | 在重新导出 Excel **之前**运行 `python Scripts/GroundMech/MechaDestroyer/author_mobility_tables.py --restore-3c`。这会按行恢复机动、步态及 Free 相机参数。日常改表直接走第一行。 |
| 读回动画接线、表、相机和清单 | `python Scripts/ue_exec.py Scripts/GroundMech/MechaDestroyer/read_ground_mobility_assets.py` |
| 在原地图重建或更新机动区 | 打开 `/Game/Maps/LVL_CommanderMassPrototype`，运行 `python Scripts/ue_exec.py Scripts/GroundMech/MechaDestroyer/save_mobility_review_scene.py`。它按原标签创建或更新平台、18° 坡台、四级台阶、左右标靶和入口，保存后读回 10 个实体。 |

当前机动区入口为 `GroundMobility_Entry=(-2800,50000,1149.04)`。常规导入调用既有动画同步钩子。重建整张正式动画图时才使用 `install_ground_mobility.py`，其中也保留了 Mouse2D 输入绑定。新回执默认写入 `Artifacts/GroundMobility/`。
