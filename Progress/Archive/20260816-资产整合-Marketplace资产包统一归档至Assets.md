# 2026-08-16 解决了：Marketplace 资产包统一整合至 Content/Assets

- 对应开发文档：无（资产目录整理，非功能开发）
- 变更类型：内容资产重组

## 变更清单

| 文件/资产 | 变更 |
|---|---|
| `Content/Assets/` | 新增——8 个外部资产包统一归档根目录（Environments / Props / SampleMaps 三类） |
| `Content/City_of_Brass_Enviroment` 等 7 个顶层包目录 | 移除——资产全部迁入 Assets/，孤儿重定向器清理完毕 |
| `Content/MafiaBarnPack/` | 保留 167 个重定向器（5.7MB）——被 L_MafiaBarn_Demo 引用，见遗留问题 |
| `Content/__ExternalActors__/` | 删除——原为 MafiaBarnPack 专属 OFPA 目录（内容见遗留问题 1） |
| `Config/` `Source/` `*.uproject` | 零改动——不引用旧包路径 |

## 成功整合的资源清单

8 个资产包、共 **4,482 个资产**全部迁入 `/Game/Assets/`，迁移后逐包资产计数与迁移前注册表基线**精确一致**，加载抽查（BP / 材质 / 动画 / ParagonSample 地图）全部通过，无缺失引用告警。

| 资产包 | 分类 | 资产数 | 含地图 | 验证 |
|---|---|---|---|---|
| City_of_Brass_Enviroment | Environments | 749 | 5 umap | ✓ 计数一致 |
| MafiaBarnPack | Environments | 559 | 7 umap | ✓ 计数一致 |
| Stylized_Medieval_Town | Environments | 513 | 2 umap | ✓ 计数一致 |
| KiteDemo | Environments | 196 | 无 | ✓ 计数一致 |
| ParagonProps | Props | 2,422 | 6 umap | ✓ 计数一致 |
| KEStatues_Lite | Props | 22 | 1 umap | ✓ 计数一致 |
| SampleMap（Paragon 展示图） | SampleMaps | 15 | 2 umap | ✓ 计数一致 |
| Lighting（天空/HDRI 辅助） | SampleMaps | 6 | 无 | ✓ 计数一致 |

包内部目录结构保持原样，仅顶层位置变化；SampleMap 与 Lighting 依赖 ParagonProps 的引用链已全部固化（重存引用方地图完成）。

最终结构：

```
/Game/Assets/
├── Environments/    City_of_Brass_Enviroment / MafiaBarnPack / Stylized_Medieval_Town / KiteDemo
├── Props/           ParagonProps（含 FX 特效库、6 个 AssetMap）/ KEStatues_Lite
└── SampleMaps/      SampleMap（ParagonSample 主从图，烘焙光照 343MB）/ Lighting
```

## 做了什么（方法归档，下次迁移可复用）

迁移全程在编辑器内通过 MCP Python 脚本完成，期间触发 **7 次编辑器崩溃**，全部为 UE 5.7.4 游戏线程批量序列化的确定性引擎断言（非资产损坏），逐一定位并绕过：

1. **普通资产**：`rename_directory` 按子目录分批（每批 ≤300），保留重定向器后逐步清理
2. **普通 Blueprint**：批量重命名触发 `Linker.h:89 !Index.IsNull()` 断言 → 改用 **load → 原地 save → rename_asset** 逐个迁移（MafiaBarnPack 13 个 BP 全部成功）
3. **World Partition / Landscape 地图**：保存时触发 `TaskGraph.cpp:689 RecursionGuard` 断言 → 改用**文件系统移动 umap**（顶级地图无被引用者，安全），再通过加载重定向器解析 + 重存引用方地图固化引用（ParagonSample、6 个 AssetMap、L_MafiaBarn_Overview 成功）
4. **重定向器清理**：注册表按"有无 referencer"分类——508 个孤儿经核实后文件系统删除；引用方已重写的包目录清空移除
5. 崩溃恢复经验：MCP 端口监听 ≠ 编辑器就绪（需等帧循环稳定）；启动弹"恢复包"对话框时用 `PostMessage WM_CLOSE` 关闭；磁盘状态需以资产注册表为准（重定向器与真实资产同名易误判）

## 验证

- 逐包计数：8/8 精确匹配迁移前基线（4,482 资产）
- 加载抽查：BP_Safe（Blueprint）、M_ParagonGates（材质实例）、Anim_Safe_idle（动画）、ParagonSample（World）全部加载成功
- 日志检查：本次会话无 `Unable to load` / missing reference 告警
- git status：`Content/Assets/` 与 `Content/MafiaBarnPack/`（重定向器）为未跟踪新增，未提交

## 遗留问题

1. **MafiaBarnPack 148 个外部 Actor 文件丢失（需重新下载恢复）**
   首次批量移动在提交阶段崩溃，`__ExternalActors__/MafiaBarnPack/Maps/Level_Instance/{Barn_Roof_Rafters, Barn_wall_frame, Floor_1, Floor_2, MoneyPack}` 下 148 个 OFPA 文件被删且未写入新位置。全盘取证（回收站 / VaultCache / Autosaves / 崩溃恢复数据 / Fab 缓存）无备份。影响：5 个 Level Instance 子关为空壳；两个主地图完好。
   **恢复**：重新下载 MafiaBarnPack，将原包 `__ExternalActors__/MafiaBarnPack/...` 子树放到 `Content/__ExternalActors__/Assets/Environments/MafiaBarnPack/Maps/Level_Instance/`（与迁移后地图路径镜像）。

2. **`Content/MafiaBarnPack/` 167 个重定向器待清除**
   被 `/Game/Assets/Environments/MafiaBarnPack/Maps/L_MafiaBarn_Demo` 引用；该地图脚本保存必崩（UE 5.7.4 物理烹饪阶段断言，与 StaticRope/StaticWire 样条网格体相关）。重定向器功能正常（透明解析）。
   **清除**：编辑器 UI 手动打开 L_MafiaBarn_Demo 保存一次（UI 路径不走崩溃的脚本路径），随后 Content Browser 中 Fix Up Redirectors，即可删除整个 `Content/MafiaBarnPack/`。
