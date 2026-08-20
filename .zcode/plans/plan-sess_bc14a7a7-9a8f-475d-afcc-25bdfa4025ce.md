# DIY 飞船 — Character 主体 + 部件组件 + Socket 槽位 + 飞行中热切换 MVP

## 一、装配模型（按用户定稿）

- **主体 = Character**：新建 `AGuLiStrikeShip : ACharacter`（全新类，不继承现有俯视角 `AGuLiStrikeCharacter`——那套绑死了俯视相机与平面约束）。Character 自带 Capsule 碰撞；舰体可见网格用挂在其下的 StaticMeshComponent（virgon 整块网格），主体上开 socket。
- **部件 = 组件**：`UGuLiStrikeShipPartComponent : UStaticMeshComponent`——部件本身就是网格组件；C++ 子类 `UGuLiStrikeEnginePart`（推力/质量）、`UGuLiStrikeWeaponPart`（伤害/射速/投射物类）。**具体部件 = 蓝图组件资产**（如 BP_Engine_Standard），网格/材质/数值/兼容槽位全在蓝图里填，加新部件零代码。
- **槽位 = 舰体 socket**：在 virgon 静态网格上加 socket，socket 名即槽位名，命名规范 `HP_Engine_L`、`HP_Weapon_Nose` 等。
- **兼容性 = 部件上的槽位名集合**：`CompatibleSockets : TArray<FName>`。一个部件可接多个槽位（例：火箭巢兼容 `HP_Weapon_L`/`HP_Weapon_R`/`HP_Engine_L`）；安装时校验集合包含目标槽位才允许。

## 二、Socket 添加路径（已实测验证）

Python 直接读写 `StaticMesh.sockets` 被 protected 拦截，`EditorStaticMeshLibrary`/`StaticMeshEditorSubsystem` 均无 socket API。因此：
- C++ 写 `UGuLiStrikeDevLibrary::AddSocketToStaticMesh(Mesh, Name, Loc, Rot)`（BlueprintCallable 静态函数，内部 new `FStaticMeshSocket` 填字段 → `Sockets.Add` → MarkPackageDirty），编译后 MCP `execute_python` 调 `unreal.GuLiStrikeDevLibrary...` 批量加，`save_asset` 持久化。
- 兜底：手动在静态网格编辑器 Socket 面板加（仅 6~8 个）。

## 三、装配与热切换流程

Ship 对外 API：
- `InstallPart(PartClass, SocketName)`：兼容校验 → 销毁该槽现有部件 → `NewObject<PartClass>(this)` → `AttachToComponent(HullMesh, FAttachmentTransformRules::KeepRelative, SocketName)` → RegisterComponent → RecomputeStats
- `UninstallPart(SocketName)` / `GetPartAt(SocketName)`
- `CyclePart(SocketName)`：在 `PartCatalogue`（BP 填的全量部件类）∩ 该槽兼容部件间循环，绑热切换键（候选 R/T，定稿前读现有 IMC_GuLiStrike 避免冲突）
- `RecomputeStats()`：GetComponents 遍历在线部件求和 → 推重比即时刷新 CharacterMovement 参数（飞行中生效）
- `Fire()`：遍历已装武器部件，按各自射速从部件世界变换生成现有 `AGuLiStrikeProjectile`，伤害取部件数值

## 四、6DOF 移动（Character 方案）

CharacterMovement 置 `MOVE_Flying`、关平面约束；WASD 局部方向平移 + 鼠标 pitch/yaw + Q/E 滚转（`AddActorLocalRotation`），追尾 SpringArm 带 CameraLag。virgon 体型大，BP 实例整体缩放，MCP 截图迭代定值。

## 五、产出物清单

**代码（`Source/GuLiStrike/Gameplay/Ship/`，无新 Build.cs 依赖）**
| 文件 | 内容 |
|---|---|
| GuLiStrikeShip.h/.cpp | 主体 Character：舰体网格/相机/6DOF/装配 API/数值聚合/开火 |
| GuLiStrikeShipParts.h/.cpp | 部件基类（CompatibleSockets+网格）+ 引擎/武器子类与数值 |
| GuLiStrikeDevLibrary.h/.cpp | socket 添加等编辑器辅助工具（BlueprintCallable） |

**内容资产（MCP 创建）**：`BP_GuLiStrikeShip`、`BP_ShipGameMode`（DefaultPawn=飞船）；部件蓝图 ≥4 个（BP_Engine_Standard/BP_Engine_Heavy 用 fly-01 engines，BP_Weapon_Laser 用 lasers，BP_Weapon_RocketPod 用 rocket 作多槽位兼容示例）；`IMC_Ship` + 7 个 IA_Ship_*（Python 建 EnhancedInput 映射若有 API 缺口则退 C++ BindKey，项目有先例）；`/Game/Maps/LVL_ShipTest` 测试图。

**资产修复（前置必做）**：virgon 9 个材质槽全空 → `set_static_mesh_material` 指定包内 9 个 Flattop 材质（顺序未知，截图迭代确认）；virgon 碰撞检查（无则加 convex）；fly-01 部件材质槽检查。148K 三角面/单 LOD 记入后续优化。

**文档（gulistrike-progress 体系）**：`Progress/RequirementDocument/20260820-DIY飞船.md`（模板 A）+ `DevelopmentDocumentation/20260820-DIY飞船.md`（模板 B 含可勾选任务清单）；完成后 `Archive/20260820-DIY飞船MVP.md` + 新建 `Gameplay/飞船.md` 模块册 + `Progress/README.md` 索引更新。

## 六、数值草案（BP 可调，入技术文档）

HullMass=100 / BaseMaxSpeed=2000 / BaseAccel=800；引擎A 推力600·质量10，引擎B 推力1000·质量30；激光 伤害10·3发/s，火箭巢 伤害40·0.8发/s；推重比=Σ推力÷(HullMass+Σ部件质量)，MaxSpeed=Base×clamp(推重比÷标称比, 0.5, 2.0)

## 七、实施顺序

1. 落需求/技术两份文档
2. virgon 材质指定 + 碰撞 + 截图验证；fly-01 检查
3. C++ 三组文件 + Build.bat 编译通过（含 DevLibrary socket 工具）
4. MCP 调 DevLibrary 给 virgon 加 socket（先截图确认舰体朝向再定位置）→ 保存资产
5. MCP 建 BP/输入/部件蓝图/测试图；关卡预览截图 QA 槽位与缩放
6. PIE 验证：6DOF 可飞；热切换键飞行中换引擎/武器（网格即时替换、速度/火力随之变化）；开火出弹
7. 归档三件套 + 索引

## 八、边界（本轮不做）

存档持久化、机库换装 UI、部件破坏/脱落、燃料能耗、网络复制、Tripo 生成新部件美术（留作扩展）

## 九、主要风险

- C++ 写 `UStaticMesh::Sockets` 在 5.7 的可写性编译期即知，不行则手动编辑器加 socket
- EnhancedInput 资产 Python 创建的 API 缺口 → C++ BindKey 兜底
- virgon 材质槽↔材质映射未知 → 截图迭代
- Character 6DOF 手感 → 参数全 BP 可调，MCP 热调