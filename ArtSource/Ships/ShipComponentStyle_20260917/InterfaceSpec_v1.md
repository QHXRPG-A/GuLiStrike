# Ship 三组件冻结接口 v1

规范 1.0；坐标以 UE 原网格空间、厘米为准。安装变换均为单位变换。
原点 (0,0,0) 保留；Blender 参考转换为 `(x/100, -y/100, z/100)` 米。
以下面数是原型数据，不能作为新成品的 LOD0 面数。

两门炮采用 `Root → BarrelPitch`。Root 为单位变换；BarrelPitch 的参考旋转四元数 (x,y,z,w) 为 `(0,0,0.7071067812,0.7071067812)`。
炮口局部 +X 随参考骨骼变换指向原网格 +Y；保持既有旋转，不擅自改轴。
Thor 的 Socket 原始旋转均为零；保留该接口，不把孔的视觉朝向自动写回为 Socket 旋转。

## 双联炮

- 原型外包络 X/Y/Z：20.167759 / 49.253320 / 10.072564 米。
- 原型：1240 三角面，1 个材质槽。
- 蓝图：`/Game/GuLiStrike/Ship/Parts/BP_SC_Twin_Barrel_Turret`。
- 当前显示网格：`/Game/Assets/Ships/ShipComponent/Rigged/SKM_SC_Twin_Barrel_Turret.SKM_SC_Twin_Barrel_Turret`。
- 兼容安装槽：`bottom_mid_0`, `bottom_mid_2`。
- BarrelPitch 平移（cm）：`[0.0, -220.0, -27.01]`。

| 名称 | 所属骨骼 | 网格空间位置 cm | 骨骼相对位置 cm |
|---|---|---|---|
| Socket_1 | BarrelPitch | (-440.471222, 2885.057617, -88.530794) | (3105.057617, 440.471222, -61.520794) |
| Socket_2 | BarrelPitch | (440.471252, 2885.057617, -88.530794) | (3105.057617, -440.471252, -61.520794) |

- 完整旋转、比例与来源见 [冻结快照](Source/source_snapshot_v1.json)。

## CIWS

- 原型外包络 X/Y/Z：3.201042 / 6.845887 / 2.350498 米。
- 原型：654 三角面，1 个材质槽。
- 蓝图：`/Game/GuLiStrike/Ship/Parts/BP_SC_CIWS`。
- 当前显示网格：`/Game/Assets/Ships/ShipComponent/Rigged/SKM_SC_CIWS.SKM_SC_CIWS`。
- 兼容安装槽：`air_13`, `air_14`, `air_15`, `air_16`, `air_17`, `air_18`, `air_19`, `air_20`, `air_21`, `air_22`, `air_23`, `air_24`, `air_25`, `air_26`, `air_27`, `air_28`, `air_29`, `air_30`, `air_31`, `air_32`, `air_33`, `air_34`, `air_35`, `air_36`, `air_37`, `air_38`, `air_39`, `air_40`, `air_41`。
- BarrelPitch 平移（cm）：`[0.0, 20.0, 18.818]`。

| 名称 | 所属骨骼 | 网格空间位置 cm | 骨骼相对位置 cm |
|---|---|---|---|
| Socket_1 | BarrelPitch | (-13.227221, 413.210510, 11.107655) | (393.210510, 13.227221, -7.710345) |
| Socket_2 | BarrelPitch | (-0.000048, 413.210510, 33.237783) | (393.210510, 0.000048, 14.419783) |
| Socket_3 | BarrelPitch | (13.227186, 413.210510, 11.107655) | (393.210510, -13.227186, -7.710345) |

- 完整旋转、比例与来源见 [冻结快照](Source/source_snapshot_v1.json)。

## Thor 一级导弹舱

- 原型外包络 X/Y/Z：18.048770 / 45.332402 / 5.910690 米。
- 原型：2080 三角面，1 个材质槽。
- 蓝图：`/Game/GuLiStrike/Ship/Parts/BP_SC_Thor_MissilePod`。
- 当前显示网格：`/Game/Assets/Ships/ShipComponent/SM_SC_Thor_MissilePod.SM_SC_Thor_MissilePod`。
- 兼容安装槽：`missilepod_1`, `missilepod_2`, `missilepod_3`。
- 静态模型：5 组舱体 × 每组 3 个可见圆孔，共 15 孔；只有 5 个逻辑发射 Socket。用户已确认保留。

| 名称 | 所属骨骼 | 网格空间位置 cm | 骨骼相对位置 cm |
|---|---|---|---|
| Socket_1 | 静态网格 | (-235.000000, 0.000000, 255.000000) | — |
| Socket_2 | 静态网格 | (-235.000000, -1735.000000, 255.000000) | — |
| Socket_3 | 静态网格 | (420.000000, -875.000000, 255.000000) | — |
| Socket_4 | 静态网格 | (-235.000000, 1675.000000, 255.000000) | — |
| Socket_5 | 静态网格 | (340.000000, 875.000000, 255.000000) | — |

- 完整旋转、比例与来源见 [冻结快照](Source/source_snapshot_v1.json)。
