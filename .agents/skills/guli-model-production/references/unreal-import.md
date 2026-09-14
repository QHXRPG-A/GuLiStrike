# UE5 导入与验证

项目为 `D:/UE5.7/test1/GuLiStrike.uproject`。遵守项目搜索禁令，不遍历缓存或商城大资产目录，不读取 uasset/umap 二进制内容；用编辑器 API 检查。

## 范围和执行入口

先核对源文件版本、目标路径及同名资产。只更新任务范围内的资源；覆盖已存在模型时保留对象路径和引用，不把名称相近但用途不同的游戏资产替换掉。

实例 `Scripts/import_industrial_defense_models.py` 使用项目 ArtSource 下认可的 V3 文件，分别写入 `/Game/GuLiStrike/Buildings/RedOreRefinery`、`ShieldGenerator`、`HeavyDefenseCannon`。各根分 Meshes、Materials、Textures，重防炮另有 Animations。

导入采用归属 metadata 和源文件 SHA256，支持窄范围重跑，只保存自己的包。该脚本在正常 UnrealEditor 的 `-ExecutePythonScript` 启动阶段运行，退出逻辑要求 `-IndustrialDefenseImportWorker`，不能从 live MCP 直接调用 main。

项目遇到过 Interchange 的 AssetImportTasks 在 MCP game-thread/task-graph 回调中同步执行时递归等待。大批 FBX 使用已验证的独立进程，或有成功依据的安全执行入口。显式设置 FbxFactory 和 FbxImportUI；暂时关闭 `Interchange.FeatureFlags.Import.Enable` 时在 finally 恢复原值，不修改持久配置。

独立进程只在写入范围与当前编辑器没有重叠时并行使用：新目录先在 live editor 确认不存在，辅助进程禁用 UnrealMCP、UnrealMCPython 以免抢端口。更新已加载资产应采用当前编辑器安全入口，或协调保存/关闭，不让两进程同时写同一包，也不强制结束用户编辑器。

本机 Editor 为 `D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.exe`。辅助进程使用 `Start-Process -WindowStyle Hidden` 和轻量 `/Engine/Maps/Entry`；只导入可用 `-nullrhi`，视觉检查需要渲染环境。

## 网格和材质

显式指定 FBX 类型，关闭自动类型检测及自动材质/贴图导入。保留法线和切线，转换场景与单位，导入缩放默认为 1，再比对实际尺寸。

静态网格按需求合并，保留 UV0，必要时生成 lightmap UV。小型规则模型不必默认开启 Nanite。骨骼动画关联同一 Skeleton，关闭不需要的自动物理资产。碰撞、LOD、控制蓝图按实际用途决定；不要将“导入完成”混同于完整玩法配置。

| 纹理 | UE 设置 | 连接 |
| --- | --- | --- |
| BaseColor | sRGB，Default | RGB → Base Color |
| ORM | 关闭 sRGB，Masks | R → AO，G → Roughness，B → Metallic |
| NormalDX | 关闭 sRGB，Normalmap | RGB → Normal；已经是 DX 时不再翻绿 |
| Emissive | sRGB，Default | RGB → Emissive Color |

纹理采样器类型与压缩设置匹配。骨骼使用的材质启用 used_with_skeletal_mesh。按 Blender 光照前的 PBR 值还原；色调映射差异不等于贴图失效。

## 当前编辑器回读

优先使用明确的 UnrealMCP 工具。Python 可经 `Scripts/commander_editor_python.py --file ...` 执行：它用 hex 包装避开长命令中的 `.py` 被控制台误判为文件，并回传异常。脚本结尾调用 `unreal.MCPythonHelper.submit_result(json.dumps(...))`；检查业务 success，也写结构化报告，不能仅凭 TCP 成功或退出码宣布完成。

独立导入后仅扫描新增资产根刷新 AssetRegistry，再 load 具体路径。检查类型、单位尺寸、材质槽和纹理引用；Normal/ORM 色彩空间；骨骼名、层级及刚性权重；动画 Skeleton、固定件、活动件和时长。可复用项目 SkeletonService 和 SkinWeightModifier，不解析资产二进制。

## 动画单位与实际播放

比较动画轨道与 Skeleton 参考姿态的局部位移、缩放，并检查实际组件的尺寸和关节位置。V3 的 FBX 导入曾出现参考根骨骼缩放为 100、动画根骨骼缩放为 1：旋转曲线和时长均正确，播放后整个模型却缩小至 1/100。

本例只有关节旋转，`Scripts/normalize_industrial_defense_animation.py` 先确认没有位移/缩放运动，再用参考姿态的位移及缩放配合原始旋转关键帧，保存到同一个动画资产；导入脚本已调用该步骤。不能把此方法直接用于包含后坐、位移、伸缩或变形的动作，也不要靠给演员放大 100 倍掩盖源问题。

编辑器单节点预览须确认 AnimSingleNodeInstance 已存在且实际时间改变。仅重复设置相同 AnimationMode 不会重建实例。实例脚本通过切换模式后 `override_animation_data` 从持久动画数据初始化并刷新骨骼；读取组件姿态确认根部固定、水平转动、去除父级旋转后的局部俯仰，以及缩放和轴心与参考姿态一致。

保留实际 UE 画面证据。`Scripts/preview_industrial_defense_models.py` 使用独立展示地图及明确的光照/曝光，等待锁定相机后的画面稳定再截图；不要将欠曝误判为材质失败。切换关卡前不在 Python 中持有待加载 World 的引用，避免编辑器切图垃圾回收失败。

使用单独展示地图或临时预览演员，避免为拍图保存当前关卡的其他改动。模型、Skeleton、演示动画导入成功，不代表自动瞄准、伤害、建筑目录或网络逻辑已实现。
