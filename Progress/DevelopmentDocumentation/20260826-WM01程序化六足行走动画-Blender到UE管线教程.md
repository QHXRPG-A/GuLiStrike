# WM01 程序化六足行走动画 — Blender 到 UE 完整管线教程

> ⚠️ **2026-08-26 回退说明**：应用户要求，本教程产出的动画资产（wm01_walkcycle 四件套、DEMO Actor、Blender Action）已全部删除，用户改为自己在 UE 内制作动画。**管线知识与踩坑记录仍然有效**（尤其"坑清单"与编辑器动画不 tick 的原因分析），保留作参考。资源现状见 [回退归档](../Archive/20260826-动画资产回退-用户改为UE内自制.md)。

- 日期：2026-08-26
- 状态：已回退（教程内容保留作管线参考）
- 适用：带骨骼网格的机甲（本例 WM-01，99 骨骼）；同样流程适用于任何骨架

## 成果

| 产物 | 路径 |
|---|---|
| 行走循环 AnimSequence | `/Game/Assets/WarMachines/CombatAvatarWM-01/wm01_walkcycle/SkeletalMeshes/wm01_walkcycle_Anim`（1.97s / 48 帧闭环） |
| 动画源 Blender 工程 | `Saved/MCPExports/mach_1_final.blend`（Action：`WM01_WalkCycle`，75 条 fcurve） |
| 导出 GLB | `Saved/MCPExports/wm01_walkcycle.glb`（20MB，蒙皮+动画） |

原理：**三角步态（tripod gait）**——六条腿分两组（奇数 01/03/05 与偶数 02/04/06），相位差半周期：一组抬起前摆时另一组贴地后蹬。用脚本按正弦函数驱动每条腿的髋/大腿/小腿/踝骨骼旋转并批量 K 帧，比手 K 快且参数可随时调。

## 第一步：Blender 程序化 K 帧

在 MCP 交互会话中执行（或存成 .py 用后台模式跑，见第二步）。核心脚本（可复用，改 `LEGS`/幅度即可用于其它骨架）：

```python
import bpy, math
from mathutils import Euler

arm = bpy.data.objects['Sci-Fi-MS-Spider-Mecha_222347']
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='POSE')

# 新建 Action（先清旧）
if arm.animation_data and arm.animation_data.action:
    old = arm.animation_data.action; arm.animation_data.action = None
    bpy.data.actions.remove(old)
act = bpy.data.actions.new('WM01_WalkCycle')
arm.animation_data_create(); arm.animation_data.action = act

FRAMES = 48                                    # 2 秒 @ 24fps
LEGS = ['01','02','03','04','05','06']
PHASE = {n: (0.0 if int(n) % 2 else 0.5) for n in LEGS}   # 奇偶两组 = 三角步态
AMP_HIP, AMP_C, AMP_D, AMP_A = 0.30, 0.35, 0.22, 0.18      # 摆动幅度（弧度）

def set_pose(t):
    for n in LEGS:
        s = math.sin(2*math.pi*(t + PHASE[n]))      # >0=摆动相(抬腿) <0=支撑相(蹬地)
        lift = max(0.0, s)
        arm.pose.bones[f'Leg_{n}e'].rotation_euler = Euler((0,0, AMP_HIP*math.cos(2*math.pi*(t+PHASE[n]))),'XYZ')
        for seg, amp in (('c',AMP_C),('d',AMP_D)):
            arm.pose.bones[f'Leg_{n}{seg}'].rotation_euler = Euler((-amp*lift,0,0),'XYZ')
        arm.pose.bones[f'Leg_{n}a'].rotation_euler = Euler((AMP_A*lift*0.5,0,0),'XYZ')
    arm.pose.bones['Mainbody'].rotation_euler = Euler((0.02*math.sin(4*math.pi*t),0,0.02*math.cos(4*math.pi*t)),'XYZ')

for f in range(1, FRAMES + 2):                # 49 帧 = 第 1 帧复制品 → 完美闭环
    set_pose((f-1)/FRAMES)
    for pb in [arm.pose.bones['Mainbody']] + [arm.pose.bones[f'Leg_{n}{s}'] for n in LEGS for s in ('e','d','c','a')]:
        pb.keyframe_insert(data_path='rotation_euler', frame=f)

bpy.ops.object.mode_set(mode='OBJECT')
act.use_fake_user = True                        # 防止保存时被清
bpy.ops.wm.save_as_mainfile(filepath=r"D:/UE_5.7/test1/Saved/MCPExports/mach_1_final.blend")
```

骨骼映射说明（WM-01 腿链）：`xxe`=髋(连骨盆，绕 Z 摆动)、`xxd`/`xxc`=大腿/小腿(抬腿时绕 X 弯曲)、`xxa`=踝(补偿)。`_lever` 液压杆骨骼不 K（跟随父骨骼自然联动）。

**步态参数速查**：

| 参数 | 值 | 效果 |
|---|---|---|
| FRAMES | 48 | 2 秒一个循环；改小=步频快 |
| PHASE | 奇 0.0 / 偶 0.5 | 三角步态；全部同相=同步跳跃式 |
| AMP_HIP | 0.30 rad | 步幅（髋摆角） |
| AMP_C/AMP_D | 0.35/0.22 rad | 抬腿高度 |
| Mainbody 双频微幅 | 0.02 rad | 身体起伏滚动（频率 ×2 = 左右组交替） |

## 第二步：导出带动画的 GLB

**不要用插件的 `execute_blender_code_for_cli` 跑大工程导出**——它有 60 秒硬超时，42MB 工程加载+导出会超时且进程被杀。正确做法：Bash 后台直拉 blender.exe：

```bash
"D:/steam/steamapps/common/Blender/blender.exe" --background \
  "D:/UE_5.7/test1/Saved/MCPExports/mach_1_final.blend" \
  --python "D:/UE_5.7/test1/Saved/MCPExports/export_walkcycle.py"
```

export_walkcycle.py 要点：选骨架+蒙皮网格 → `bpy.ops.export_scene.gltf(filepath=..., export_format='GLB', use_selection=True, export_skins=True, export_animations=True)` → 结果写 JSON 报告（后台模式看不到 stdout 时用于确认）。

验证导出成功：报文里 `fcurves: 75`（25 骨骼 × 3 欧拉角通道），GLB 约 20MB。

## 第三步：UE 导入与整理

用 MCP 的**延迟导入通道**（`asset.import_gltf`，调度到编辑器 tick 执行；`asset.import_fbx` 有 Interchange 重入崩溃风险，勿用）：

```
import_gltf(file_path="D:/UE_5.7/test1/Saved/MCPExports/wm01_walkcycle.glb",
            destination_path="/Game/Assets/WarMachines/CombatAvatarWM-01")
```

约 10 秒后产出嵌套结构：`wm01_walkcycle/SkeletalMeshes/` 下 4 件（SkeletalMesh + **AnimSequence** + Skeleton + PhysicsAsset）+ `Materials/` 存根 MIC。

**必做的两件清理**（Interchange 存根问题，同本项目资产整理惯例）：

```python
# 1) 显式重指真材质（勿信重定向器自动解析）
m = unreal.load_asset(mesh_path)
# ...重建 materials 数组指向 CombatAvatarWM-01 下的真 MIC，save_asset
# 2) 删存根 MIC：wm01_walkcycle/Materials/Sci-Fi_MS_..._Baked_Material
```

## 第四步：验证播放

临时 SkeletalMeshActor 挂动画（MCP execute_python）：

```python
comp.set_editor_property('animation_mode', unreal.AnimationMode.ANIMATION_SINGLE_NODE)  # 枚举值，不能传字符串！
comp.set_animation(anim)      # anim = load_asset(.../wm01_walkcycle_Anim)
comp.play(True)
comp.set_position(0.98, True) # 跳到任意时刻检查相位差
```

实测验证结论：六条腿相位各异、抬摆分明、材质正常（视口截图确认）。注：编辑器视口截图若 HighResShot 卡住不落盘，用 `vision.capture_viewport`（RGBA PNG 需 PIL 转 RGB 才能预览）。

## 在游戏里使用（两种接法）

1. **直接用 walkcycle 那套 mesh+skeleton+anim**——最省事，但注意：Socket 挂载体系在 `WM01_Avatar`（原骨架）上，武器挂接需重建到这套骨架，不推荐；
2. **把 AnimSequence 用到 WM01_Avatar_Skeleton**（推荐）：两副骨架骨骼名与层级完全一致，在编辑器中对 `wm01_walkcycle_Anim` 执行 Retarget（IK Retargeter 或同构骨架直接换 Target Skeleton），动画即作用于带 Socket 的正式主体。之后建 Anim BP（Idle/Walk 按速度混合）接入 Pawn。

## 本管线踩坑记录（复现时注意）

| 坑 | 现象 | 解法 |
|---|---|---|
| Blender 5.2 Action API 变更 | `act.fcurves` 报 AttributeError | fcurves 移入 layers/strips/channelbag；脚本里 try/except 兼容 |
| MCP CLI 60s 硬超时 | 大工程导出进程被杀、无产物 | Bash 后台直拉 blender.exe --background |
| `transform_apply` 在 MCP 会话静默失效 | 缩放没进数据、导出双倍 | 用顶点/编辑骨骼直接赋值代替算子 |
| `animation_mode` 传字符串报错 | NativizeEnumEntry 失败 | 传 `unreal.AnimationMode.ANIMATION_SINGLE_NODE` |
| 引擎 HighResShot 偶发卡死 | 文件不落盘 | 改用 vision.capture_viewport + PIL 转 RGB |

## 下一步扩展

- 步频/步幅参数化：把 `FRAMES`/`AMP_*` 提为输入，导多套（慢走/冲刺）供 Anim BP 按速度混合
- 转向步态：横向腿相位不对称化，或导左右转弯两条序列
- 终极路线：Control Rig 程序化 IK 步态（任意速度自适应、动态落点），本 K 帧循环可作为过渡方案先跑起来
