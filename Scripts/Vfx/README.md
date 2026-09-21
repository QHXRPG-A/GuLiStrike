# 特效目录维护

唯一资源与基础缩放来源是 `Data/Excel/GuLiStrikeVfx.xlsx` 的 `Effects`。前三行依次为字段名、类型、必要性，第 4 行起是数据。用项目 `xlsx` CLI 编辑，不手改 JSON、生成头文件或 DataTable。

`id` 是不复用、不重排的正整数；`name` 是稳定英文行名，也是生成的 `GuLiVfxIds::Name` 符号。资源的完整软路径和三轴基础缩放共同决定身份，同一组合只能有一行。新增用途合并到 `Note`；颜色、旋转、时长及多层组合继续维护在使用方。可选引用为 0，必需引用必须有效。蓝图特效填写生成类 `_C` 路径，保证打包后能够加载。

维护步骤：

1. 编辑 Excel；其他表的引用列使用 `int` 类型的 `…VfxId`。先登记目录，再添加引用。
2. 运行 `python Tools/DataPipeline/export_data_from_excel.py`。所有表和 INI 引用在写入任何生成物前统一校验；重复 ID、重复组合、非法缩放或悬空引用都会阻止导出。
3. 结构或原生代码改变时，先用源码版 UE5.7 编译 Editor/Game Development，再重启编辑器。
4. 运行 `python Scripts/ue_exec.py Scripts/Vfx/import_vfx_tables.py`。仅导入 Effects、机甲技能、法术场三表，并设置已有配置资产与蓝图的 ID。内容相同时跳过表重导入及资产重写。
5. 本次迁移验收运行 `python Scripts/ue_exec.py Scripts/Vfx/validate_vfx_static.py`。其迁移基线比对属于 2026-09-21 的审计；日后有意调整资源或缩放时，旧基线差异不表示目录规则错误，需要记录新的变更依据。

运行时：`UGuLiVfxRegistrySubsystem` 按 World 缓存目录，按路径缓存资源。C++ 使用 `GuLiVfx::Load<T>(Context, VfxId)`、`GuLiVfx::Scale(Context, VfxId, DynamicScale)`；蓝图使用子系统的 Get Definition / Load Resource / Load Actor Class。编辑器无 World 查询使用 Get Definition Without World。加载的资源类型必须匹配消费方；Dedicated Server 不加载表现资源。异步调用方先取 `GuLiVfx::Path`，维持原 StreamableManager 流程，完成回调使用 `LoadIfNeeded=false`。

最终几何缩放为基础值乘动态系数。命名 Niagara 浮点缩放参数要求基础值均匀，组件保持 1；普通组件使用三轴向量。材质覆盖和后处理沿用原宿主及参数，不缩放宿主单位；本次这些材质的基础值均为 1。预警、光柱、血条等有独立几何载体的材质在对应几何变换上应用基础值。

三类机枪受击共用 **ID 5 / MachineGunImpact**；僚机弹道为 ID 4，指挥官地面机枪弹道为 ID 38（同资源、基础缩放不同），玩家弹道/枪口为 ID 8/9。批量弹道的 ScaleX/Y 分别控制粒子长度/宽度，二维粒子不使用 Z，组件保持单位缩放。现有指挥官和僚机导弹均读回为 0.4，合用 ID 2。实例 `EffectId` GUID、法术场玩法 ID 与 `VfxId` 不同。玩家子弹只同步整数 VfxId，战局协议版本升级为 18，客户端与服务器需一起更新。

指挥官数据通道枪口使用 ID 3，X/Y 控制粒子长度/宽度，Z 控制灯光半径，组件保持单位缩放。建筑放置预览材质使用 ID 39；其基础值与建筑模型缩放相乘，不改变建筑实体或碰撞尺寸。

`migration-manifest.json` 是迁移审计及使用位置的 ID 接线清单，不是第二份运行时目录。`create_registry_source.py`、`capture_*` 是一次性迁移工具。源资源路径仅在 Excel、生成数据、审计记录及资源制作脚本的输出/内部依赖定位中保留；玩法定义 DataAsset、普通模型、普通表面材质、动画和通用 UI 不属于本目录。

历史 `Scale020/migrate_editor_properties.py` 和 `Scale020/run_fx_review.py` 固定于旧属性及三变体基线，检测到新目录时会在任何资产操作前拒绝执行。当前目录使用上面的定向导入及静态校验入口，不重新运行历史缩放迁移。

本任务未运行 PIE 或压力测试。自动化目录规则、现有数学/缩放规则及玩家子弹序列化检查在无渲染命令行 Editor 中执行。
