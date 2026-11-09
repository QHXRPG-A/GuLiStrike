# Mass弹丸与弹道发黑：2026-11-02修复

正式材质已保存，玩家可重新Play `/Game/Maps/LVL_CommanderMassPrototype`，选择Commander，在普通兵种交火中核验。调试观察位置约`(-4300,54000,905)cm`；无需启用开关。预期为弹丸/尾迹恢复发光、友黄敌红，命中后按原时序消散。视觉与照明待玩家确认。

## 根因与修改

`/Game/GuLiStrike/FX/BulletTrails/Review_A1/M_MassBulletTrail`将ParticleColor的默认RGB输出接入Custom.Tint，却在HLSL读取Tint.a。当前源码版UE5.7编辑器的SM6日志报`vector swizzle 'a' is out of bounds`，随后使用默认材质。已改接RGBA，并修正`Scripts/Vfx/author_bullet_trails.py`第108行的制作输入。

正式ID38仍指向`NS_MassBulletTrail_GPU`，Renderer仍使用同一材质。本次仅改变一根材质连接；其他节点、HLSL、输出、颜色、发光、宽度、时序、Ground资源和原生代码不变。只保存该材质，未保存或修改地图。

## 证据

- `shader_failure_before.txt`：当前编辑器修复前的实际SM6错误和默认材质回退；`material_before.json`记录RGB错误连接。
- `material_after.json`、`material_fix.json`：RGBA输出索引5接入Tint，定向材质编译和保存回执。
- `static_check.json`：脚本AST语法与材质图对比通过，17根内部连接中只变更Tint一根，其他节点和输出一致；`git diff --check`通过。
- `runtime_after.json`、`runtime_summary.json`：普通Commander PIE 20秒，11次采样，12个活跃组件样本均256粒子/256槽位；184条有透明度的粒子观察记录无黑RGB、无非正发光。详细样本为黄色`(1,1,0,1)`、发光24；本次未采到可列示的敌方红色样本，不能作为完整敌我视觉验收。
- `runtime_attempt1.json`：首次诊断误用未暴露的PlayerController.get_pawn，在采样前失败并清理。第二次用GameplayStatics.get_player_pawn完成，不合并为成功样本。
- `final_readback.json`：正式ID38、材质RGBA连接、Niagara Sprite Renderer引用、30个现有部署Actor及原图路径。调试回调已注销、PIE和子进程清理完成，原Ground席位/开局询问恢复，内容及地图脏包为空。
- `shader_log_after.txt`、`shader_verification.json`：从修改前记录的字节偏移读取当前编辑器日志，Shader任务完成；修改后没有新的材质编译失败/Alpha越界。日志路径和偏移在`log_window.json`。

修复前`get_material_diagnostics`曾返回`is_compiled_ok=True`，但实际异步SM6编译失败；因此不单独用该布尔值宣称Shader成功。运行记录中的`NiagaraComponent.get_material(0)=None`不是Renderer材质查询，实际引用以`final_readback.json`中的`NiagaraSpriteRendererProperties.material`为准。

SimCache只在本次获准诊断中读取数字，不进入正式玩法。未截图或读图，未改原生代码/构建、未新增自动化测试、未做性能测量。颜色及发光数值和编译成功不代表玩家视觉验收。
