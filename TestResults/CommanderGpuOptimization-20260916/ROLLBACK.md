# 本轮回滚清单

本轮开始前已有大量未提交改动。回滚应只撤销本轮补丁，不使用整仓reset或以Git HEAD覆盖文件。

## 配置

- `Config/DefaultEngine.ini`：撤销新增FXAA设置及GPU预算的SystemSettings块，恢复TSR、体积雾、Epic虚拟阴影与Lumen默认参数。软件VRS原值为开启。
- `Config/DefaultGame.ini`：撤销`GuLiUnitRenderSettings`的12条映射及新材质目录的AlwaysCook条目。
- 只回滚某项画质时，仅调整对应键。VRS两项开关须一起设置。

## C++

- 撤销士兵、僚机、矿车、工程车与残骸中的策略接入。
- 撤销本轮新增的`GuLiUnitRenderPolicy.h/.cpp`。先撤销调用，再移除共用实现。
- 僚机的池归还/重新配置先恢复传送材质及清理旧身体覆盖，也包含在补丁内。
- 完成回滚后重新构建源码Editor及Game Development，并核对BuildId。

## 资产

- 所有新增资产位于`/Game/GuLiStrike/Rendering/UnitLowReflection`，精确清单见[材质清单](material-variants.json)。
- 原网格、原始材质及共享Interchange父材质未改动；撤销映射后即可恢复原身体材质。
- 新资产可先保留为未引用资产；需要清理时通过UE编辑器核对引用后删除该目录中的本轮13个资产。

## 依据

- [本轮文本差异](implementation.patch)相对于本轮开始的工作区状态生成，包含配置、C++与材质制作/检查脚本。
- [修改前文本哈希](before-manifest.json)与`before/`保存原文件；[交付文本哈希](implementation-manifest.json)保存本轮完成状态。
- 若后续已有其他修改，按补丁逐段合并，保留那些新修改。自动回退前先检查补丁能否准确应用。
- `check_preexisting_failures.ps1`仅用于本轮失败归因，不是通用回滚工具。

交付时已通过以下只读检查，未实际回退。补丁保留现有文件的CRLF/LF差异：

```powershell
git apply --reverse --check TestResults/CommanderGpuOptimization-20260916/implementation.patch
```
