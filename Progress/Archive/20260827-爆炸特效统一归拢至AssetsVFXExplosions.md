# 爆炸特效统一归拢至 /Game/Assets/VFX/Explosions

日期：2026-08-27　执行方式：UnrealMCPython 实时通道 + 独立编辑器进程 `-ExecutePythonScript`

## 目标与结果

把全项目散落的爆炸特效统一收进单一文件夹 `/Game/Assets/VFX/Explosions/`（平铺，无子文件夹）。

**终态：根目录恰好 445 个实体资产；目标外爆炸关键词命中 0。**

| 来源 | 迁移数 |
|---|---|
| AllExplosions 子包摊平 | 123 |
| RealisticExplosionPackD 子包摊平 | 184（其中 15 个因与 AllExplosions 重名加前缀） |
| NuclearExplosion 子包摊平 | 64 |
| GroundFire01 子包摊平 | 9 |
| ParagonProps FX 库爆炸件（材质/粒子/贴图/网格） | 64 |
| GuLiStrike/FX 的 NS_TopDownAction_Destruction | 1 |
| 合计 | **445** |

执行前 AssetRegistry 全盘扫描 `/Game` 共 5428 个资产，关键词
`explos/explod/detonat/boom/blast/nuke/mushroom/groundfire` 命中 71 个，
人工剔除 7 个误报：5 个 MafiaBarnPack 收音机音箱（Boombox）+ 2 个 Paragon 英雄名
TwinBlast 枪口特效（M_TwinBlast_Muzzle、SM_Twinblast_SimpleUltBullet）。

## 平铺改名规则

按字母序逐组入驻目标文件夹，先到先得用原名；遇到重名（大小写不敏感）自动加来源包前缀。
RealisticExplosionPackD 是 AllExplosions 的合订包，共用演示脚手架全部加前缀：
`BP_DemoDisplay(_Enum)`、`BP_Spawn_Particle`、`DefaultTextMaterialTranslucentUnlit`、
`M_DemoWall*`(5)、`SM_Base`/`Base2`/`BaseRoom`/`Base_FlatWall`、`SM_NamePlate`
共 15 项，完整映射见 `Data/tmp_explosion_moves.json`。

## 执行过程要点

1. 前 138 个走实时通道（GuLiStrike 1 + GroundFire01 9 + NuclearExplosion 64 + Paragon 64）
   全部成功落盘后，**AllExplosions 批次执行中编辑器崩溃**（TCP 连接复位、进程消失）。
   再次验证了既有结论：批量重资产操作不要走 UnrealMCPython 实时通道，
   必须用独立编辑器进程 `-ExecutePythonScript`（本次崩溃零损失，报告文件均在）。
2. 独立进程续跑遇两个坑：
   - `-nullrhi -unattended` 会触发 Fab 商城插件 `FFabBrowser::OpenTab` 断言崩溃
     （内嵌 CEF 初始化失败），去掉这两个参数、正常渲染最小化启动即可；
   - Python API 差异：`AssetRegistry.get_referencers()` 在 UE5.7 需要第二参数
     `AssetRegistryDependencyOptions()`；`EditorAssetLibrary.delete_asset()`
     接受路径字符串而非对象指针；registry 无 `get_assets_by_package` 属性报错
     实为方法未暴露（用 `get_assets_by_path` + 过滤替代）。
3. 总计 6 轮独立进程运行完成迁移 + 引用固化 + 清壳，进度与结果全程写
   `Data/tmp_explosion_*.json` 与 `tmp_explosion_progress.log` 留痕。

## 重定向器清理账目

迁移天然产生旧路径 ObjectRedirector：

- **387 个**被引擎在重命名会话内自动清理（无任何引用者）；
- **51 个**由清扫轮显式删除；
- **2 个**随演示蓝图消费者重存后删除；
- 其余小壳是被正常引用而**必须保留**的，分两类：
  - 演示内容互引：两张商城演示地图（Overview、Demo_NuclearExplosion）和三个
    DemoDisplay 蓝图内部仍指向旧路径，引擎在保存时按"被引用即重建"机制维护这些壳。
    **后续彻底清零方法**：GUI 编辑器里打开这两张图各另存一次，再对文件夹跑一次
    Fix Up Redirectors；或者直接决定不再需要商城演示关卡，删除后壳即可清掉。
  - `/Game/GroundFire01/BP/BP_GroundFIre_01` 单文件壳：存在真实外部引用方
    （疑似 LVL_ShipTest 内摆放物），保留以保证现有关卡正确加载。

旧的四个内容子目录（AllExplosions/GroundFire01/NuclearExplosion/RealisticExplosionPackD）
已不含任何实体资产，仅剩上述几 KB 的壳文件。

## 版本控制处理

`Content/Assets/` 整体在 .gitignore 中（商城大资产不入库策略）。本次迁入使两个项目自有
资产的真身落到该目录，已用 `git add -f` 强制纳入跟踪作为例外：
`Content/Assets/VFX/Explosions/{NS_TopDownAction_Destruction, BP_GroundFIre_01}.uasset`。
`Content/GuLiStrike/FX/NS_TopDownAction_Destruction.uasset` 的删除随之入库；
原 GroundFire01 顶层壳暂时保留原跟踪状态。

C++ 无需改动：BlinkVFX.cpp 硬编码的两个闪现材质按约定留在 `/Game/GuLiStrike/FX/` 未动，
源码没有任何爆炸特效路径硬编码。

## 校验基线

- 迁移前后总数量一致：445 = 380（原四子包）+ 63（Paragon 有效命中）+ 2（散落残留）；✔
- 关键词复扫目标外命中：0（TwinBlast×2 为白名单误报项）；✔
- 磁盘核对：目标根 443 uasset + 2 umap = 445；✔
- 全部报告：`Data/tmp_explosion_scan.json`（原始清单）、`Data/tmp_explosion_moves.json`
  （445 行完整新旧映射）、`Data/tmp_explosion_standalone_report*.json`（各阶段账目）、
  `Data/tmp_explosion_progress.log`（逐条进度）。
