"""Record the human approval, permitted build and saved formal handoff."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
delivery = json.loads((ART / 'formal_delivery.json').read_text(encoding='utf8'))
assert delivery['formal_switched'] and delivery['approval_B'] == 'approved'
changes = []

def amend(path, edits=(), append=''):
    file = ROOT / path
    old = file.read_text(encoding='utf8')
    new = old
    for a,b in edits:
        assert a in new,(path,a)
        new = new.replace(a,b)
    if append and append not in new:
        new = new.rstrip() + '\n\n' + append.rstrip() + '\n'
    if new != old:
        assert file.read_text(encoding='utf8') == old,'File changed during this edit: '+path
        changes.append(dict(path=path,before_sha256=hashlib.sha256(file.read_bytes()).hexdigest(),after_sha256=hashlib.sha256(new.encode()).hexdigest()))
        file.write_text(new,encoding='utf8')

dev='Progress/DevelopmentDocumentation/20261005-指挥官三档LOD纠正与资源迁移.md'
amend(dev,[
 ('status: verification','status: done'),('verification: partial','verification: passed'),
 ('next_action: 用户审核CommanderLOD_3Tier_v1具体成品后切换正式资源组；彼之矛原生配置另待获准编译加载。',"next_action: ''"),
 ('status_note: 六种候选、真实预览、审核Map和静态/美术回读已交付；实际版本B待用户，正式资源未切换。','status_note: 用户已通过当前具体版本B；六组正式三档资源切换及保存回读完成，源码Editor编译重开通过；游戏运行与FPS另记未运行。'),
 ('只改候选资源，正式 Soldiers 与旧资源组保持；近景源和冻结 Blender 不覆盖。','当前正式入口见[正式交付](../../ArtSource/CommanderLOD_20261005/formal_delivery.json)；六组Soldiers与ID8施工模型引用已切换，旧资源组及冻结源保留。'),
 ('- [ ] 用户对具体实际版本B的决定。','- [x] 用户对具体实际版本B的决定：2026-10-05“审核通过”，固定哈希761bc5edd06b。'),
 ('- [ ] B通过后切换正式资源组并回读；失败恢复该组。','- [x] B通过后切换六组正式资源并回读；保存源/原生全字段快照和整组回滚入口。'),
 ('原生编译未执行，新代码运行效果待编译后验证；PIE、联机、移动/建造运行验收和帧率测试未运行。','用户明确“现在编译并重开 UE”；源码版GuLiStrikeEditor Win64 Development编译成功，模块BuildId一致并重开加载。PIE、联机、移动/建造运行验收和帧率测试未运行。'),
 ('实际版本B待用户决定。每种候选模型恰有三档；','实际版本B已通过，正式模型恰有三档；候选证据按批准时冻结。'),
 ('未擅自重建主要机构或判为通过。','未擅自重建主要机构；用户已放行当前实际版本，源档剪影/描边偏差继续记录。'),
 ('没有执行原生编译、Live Coding、PIE、联机或FPS；旧编辑器模块尚无新的逐顶点VAT DataAsset字段，彼之矛候选纹理/材质可预览，正式运行配置需获准编译加载后完成。','已执行获准原生编译、重开和新字段回读；未执行Live Coding、PIE、联机或FPS。彼之矛正式VAT定义配置完成，三档、零运行骨骼、6个片段（待机/四向/终止姿态），三组贴图校验通过。')],
 '## 2026-10-05 B放行与正式交付\n\n用户在已打开审核Map后明确“审核通过”，对应版本`CommanderLOD_3Tier_v1`、SHA256 `761bc5edd06b08c598771943b46ebcbd7bc8285b42d345e234fea0907d02783d`。53项制作产物、批准证据和冻结源仍保持原哈希。\n\n六组76个正式资产位于`/Game/GuLiStrike/Commander/Units/<Soldiers.Name>/LOD_3Tier_v1`，内部依赖无候选目录引用。Soldiers仅切换模型/表现类/VAT三字段，Buildings ID8仅切换Mesh，其他源值和原生全字段回读一致。彼之矛按2倍模型配置600cm步幅，144cm/s对应0.24周期/秒；源测量300cm保持。\n\n审核Map已改为18组、60个正式网格组件；既有彼之矛审核区13实体保持，样机替换为真实无骨骼VAT Actor并接入两台工程车准备参数。建造、移动和联机等待玩家运行验收，此次美术B不代替玩法或性能验收。\n\n[正式清单](../../ArtSource/CommanderLOD_20261005/formal_delivery.json) · [构建记录](../../ArtSource/CommanderLOD_20261005/Reports/native_build.json) · [地图回读](../../ArtSource/CommanderLOD_20261005/Reports/formal_review_scene.json) · [新增归档](../Archive/20261005-指挥官三档LOD审核放行与正式资源切换.md)')
req='Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md'
amend(req,[('verification: partial','verification: passed'),
 ('next_action: 审核当前实际三档候选版本，通过后按完整资源组切换正式引用。',"next_action: ''"),
 ('status_note: 用户已明确批准本方案；实际成品B和正式切换尚未批准。','status_note: 用户已批准方案及当前具体成品B，六组正式资源切换和保存回读完成；游戏运行和FPS未运行。')],
 '2026-10-05后续：用户明确“审核通过”，放行`CommanderLOD_3Tier_v1`；随后明确“现在编译并重开 UE”。[正式资源切换和获准构建](../Archive/20261005-指挥官三档LOD审核放行与正式资源切换.md)已完成；规范仍为v1.3，游戏与性能验证另记。')
current='指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见'
for path in ('Progress/DevelopmentDocumentation/GuLiStrike美术规范.md','Progress/Gameplay/彼之矛.md','Progress/Gameplay/指挥官/03-数据技能与武器表现.md','Progress/DevelopmentDocumentation/20261005-彼之矛工程车建造与四足重炮.md'):
    amend(path,[('本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。','本轮保留已审近景，CommanderLOD_3Tier_v1实际版本B已通过；六组正式资源已切换保存，游戏运行与FPS未运行。')])
amend('Progress/Gameplay/先驱号.md',[
 ('当前三档候选合计37,338／15,905／1,944面，阈值1／.40／.06；正式引用待本版本B放行后切换。','当前正式三档合计37,338／15,905／1,944面，阈值1／.40／.06；已通过B并切换至`/Game/GuLiStrike/Commander/Units/DefaultSoldier/LOD_3Tier_v1`，七动作及原步态参数保留。')])
amend('Progress/Gameplay/指挥官.md',[
 ('本轮[三档候选](../../ArtSource/CommanderLOD_20261005/Review/index.html)已保存，正式引用待对应版本B放行后整组切换。','本轮[三档成品](../../ArtSource/CommanderLOD_20261005/Review/index.html)已通过具体版本B，六组正式引用已切换；[正式清单](../../ArtSource/CommanderLOD_20261005/formal_delivery.json)登记保存回读和获准构建。')])
amend('Progress/RequirementDocument/GuLiStrike美术规范.md',[],
 '2026-10-05具体版本放行：`CommanderLOD_3Tier_v1`的六种单位B已通过，正式资源迁移至`/Game/GuLiStrike/Commander/Units/<Soldiers.Name>/LOD_3Tier_v1`。[放行与正式切换记录](../Archive/20261005-指挥官三档LOD审核放行与正式资源切换.md)不改变本节三档规则，也不将超预算或实战性能记为通过。')
amend('Progress/DevelopmentDocumentation/GuLiStrike美术规范.md',[],
 '## 2026-10-05 指挥官三档成品放行\n\n用户在打开审核Map后明确“审核通过”，对应`CommanderLOD_3Tier_v1`、SHA256 `761bc5edd06b08c598771943b46ebcbd7bc8285b42d345e234fea0907d02783d`；[B决定](../../ArtSource/CommanderLOD_20261005/approval_B.json)与批准时源文件哈希固定。随后用户明确“现在编译并重开 UE”，源码Editor构建、模块版本和新字段加载通过。六种兵种76个正式资源组已切换并保存回读，模型均为LOD0/1/2；[正式交付](../../ArtSource/CommanderLOD_20261005/formal_delivery.json)与[新增归档](../Archive/20261005-指挥官三档LOD审核放行与正式资源切换.md)为当前入口。\n\n四足彼之矛保留已审四色、稀疏轮廓与三档明暗，正式运行资源无骨骼。车辆远景每台多10面、彼之矛近/远景原预算差额和重防号源远景剪影偏差继续登记；美术B不替代预算、玩法或帧率结果。规范仍为v1.3，未启动PIE/联机/性能测试。')
biz='Progress/DevelopmentDocumentation/20261005-彼之矛工程车建造与四足重炮.md'
amend(biz,[
 ('status: in_progress','status: verification'),
 ('summary: 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。','summary: 巨型重炮ID6源码已获准编译加载，正式无骨骼三档VAT与数据/UI已安装，真实审核Actor已保存，待玩家玩法验收。'),
 ('next_action: 审核LOD_v2实际成品后更新UE网格与VAT参数；沿用待答复的Editor编译事项，加载后安装玩法数据与原型图入口。','next_action: 在LVL_CommanderMassPrototype的BiZhiMaoQA区由玩家验证B→7施工、固定底座四向移动和目标炮台跟随；攻击逻辑另行制定。'),
 ('status_note: LOD_v2的LOD1达预算、LOD2/3仍超预算，Blender版本B待用户决定；UE正式资源仍v1。既有代码静态检查与地图13实体回读通过，编译/新类/玩法数据尚未加载，PIE、联机与FPS均not_run。','status_note: CommanderLOD_3Tier_v1美术B已通过、三档正式资源保存；获准Editor编译重开和数据/UI安装通过，真实13实体回读通过；建造/移动/联机/FPS未运行，不将美术B扩展为玩法通过。'),
 ('当前正式清单为 `BiZhiMao_v1_vertex`，旧骨骼VAT制作记录不再用于安装。','当前正式清单为`CommanderLOD_3Tier_v1`，正式路径`/Game/GuLiStrike/Commander/Units/BiZhiMao/LOD_3Tier_v1`；历史源不作为安装入口。'),
 ('- [ ] 按用户明确许可编译并加载新原生模块。','- [x] 用户明确“现在编译并重开 UE”，源码Editor编译并加载新字段通过。'),
 ('- [ ] 安装并保存VAT DataAsset、UE Soldiers/Buildings/GameTexts数据表、肖像与建造图标。','- [x] 安装并保存正式三档无骨骼VAT、Soldiers/Buildings/GameTexts、肖像及Building.7图标，源/原生回读通过。'),
 ('- [ ] 原型图静态样机替换为真实VAT审核Actor；安装两台工程车及施工入口配置并回读。','- [x] 原型图样机替换为真实VAT审核Actor；保存两台工程车准备参数及原有13实体，未启动游戏。'),
 ('直线144 cm/s、300cm步幅对应0.48周期/秒；','源步幅300cm乘2倍模型尺度后，正式步幅600cm；直线144 cm/s对应0.24周期/秒；'),
 ('可选编译：已询问 `GuLiStrikeEditor Win64 Development` 是否现在编译并加载，尚待答复。','可选编译：用户已明确“现在编译并重开 UE”；`GuLiStrikeEditor Win64 Development`构建成功并重开加载。'),
 ('未运行构建、Live Coding或Hot Reload，新玩法类尚未加载。','本轮构建已获准执行，新玩法类已加载；未使用Live Coding/Hot Reload，运行行为待玩家验证。'),
 ('**新类和玩法数据未安装，当前不具备可玩的新兵种入口。**','当前已替换为真实`GuLiBiZhiMaoReviewActor`，使用正式零骨骼VAT、可移动目标和两台工程车准备配置；B→7数据及UI已安装，玩法运行待玩家确认。'),
 ('获得编译许可并加载后，顺序执行 `Scripts/BiZhiMao/deploy_runtime_assets.py`、`readback_runtime_assets.py`，最后 `prepare_acceptance_scene.py`；安装脚本在新类缺失时于任何写入前拒绝，避免旧DLL静默丢失新表字段。','当前安装入口为`Scripts/CommanderLOD/configure_vertex_definition.py`、`import_formal_tables.py`、`install_bizhimao_ui.py`和`promote_review_scene.py`，结果见[正式交付](../../ArtSource/CommanderLOD_20261005/formal_delivery.json)。旧BiZhiMao生产脚本已退出默认路由，不再重写冻结成品。'),
 ('正式UE目录仍为 `BiZhiMao_v1_vertex`；下表记录该版本原预算情况。最新可审成品为下节 `BiZhiMao_LOD_v2_vertex`。','以下为此前候选阶段的历史证据；当前正式版本为`CommanderLOD_3Tier_v1`，总面数70,117／16,913／6,125，动画GPU预算97.03MiB。'),
 ('**当前实际版本B待用户决定，UE正式资产本轮没有更新。**','**该段描述此前候选阶段。当前CommanderLOD_3Tier_v1已通过B，正式三档资源已切换；几何/贴图源证据保持。**')],
 '## 2026-10-05 获准构建与正式三档加载\n\n当前实际三档成品B已通过。用户随后明确“现在编译并重开 UE”；构建退出0，源码引擎/项目/三个相关插件BuildId一致。局部MissileFixture辅助函数重名经私有重命名修复，不改变玩法行为。\n\n正式模型/VAT/施工ID8引用、三张位置和三张旋转贴图、肖像与建造图标已保存回读；运行0骨骼、同相位四向混合，直线步频配置为0.24周期/秒。审核图13实体已保存，真实样机可通过Details的CallInEditor按钮展示移动和炮台跟随；正式兵种保持默认炮台、无攻击。\n\n用户可按上文五步进入原型图进行玩法审核。本轮未运行PIE、联机或帧率测量，不将当前美术B当作兵种玩法审核通过。证据见[增量归档](../Archive/20261005-指挥官三档LOD审核放行与正式资源切换.md)。')
amend('Progress/Gameplay/彼之矛.md',[
 ('summary: 记录指挥官巨型重炮ID6的已确认参数及已实现源码；顶点动画已导入，原生编译和正式建造入口尚待安装。','summary: 指挥官巨型重炮ID6源码已获准编译加载，正式无骨骼三档VAT、施工数据和UI已安装，玩法验收待玩家。'),
 ('next_action: 经用户许可编译加载后安装玩法数据，更新原型图并进行玩家审核。','next_action: 由玩家在原型图BiZhiMaoQA区验证施工、固定朝向移动、动画及联机；攻击逻辑另行制定。'),
 ('新兵种尚不可玩，建造、移动、联机和实战性能均未运行验收。','新入口数据和真实审核Actor已保存回读，建造、移动、联机和实战性能均未运行验收。'),
 ('**原生编译尚未执行，新VAT定义、UE数据表和UI引用尚未安装，B→7当前不能作为已加载新入口。** 审核图已保存静态样机和13个相关实体，后续编译加载并安装后再替换为真实审核Actor与工程车入口。','源码Editor已按用户许可编译并重开；正式模型、零骨骼三档VAT、三张位置/三张旋转贴图、Soldiers6/Buildings8/GameTexts、肖像及Building.7图标已保存回读。原型图13实体中的样机已替换为真实VAT Actor，两台工程车准备配置已接通；未启动游戏验证B→7。'),
 ('当前三档候选区从','当前三档正式样机区从'),
 ('编译许可已询问待答复；PIE/联机运行未执行；实际成品待用户决定，攻击逻辑留待通过后另行开发。','用户已许可编译重开，当前三档美术B已通过；PIE/联机运行未执行，兵种玩法等待玩家审核，攻击逻辑仍未开发。')],
 '正式资源目录：`/Game/GuLiStrike/Commander/Units/BiZhiMao/LOD_3Tier_v1`。源步幅300cm在2倍尺寸下配置为600cm，144cm/s对应0.24周期/秒；这只是数据/源码一致性核对，不代表脚部接地运行验收。当前证据见[正式交付](../../ArtSource/CommanderLOD_20261005/formal_delivery.json)。')

arc=ROOT / 'Progress/Archive/20261005-指挥官三档LOD审核放行与正式资源切换.md'
assert not arc.exists(),'Do not overwrite a historical archive.'
arc.write_text('''---
schema: guli-progress/v1
id: ARC-20261005-004
work_id: ''
kind: archive
role: root
title: 指挥官三档LOD审核放行与正式资源切换
areas: [commander, art, assets, rendering, building]
categories: [art, gameplay]
status: recorded
verification: partial
created: '2026-10-05'
updated: '2026-10-05'
summary: 六兵种具体成品B已通过，76个正式资源组和三档引用已切换；获准源码Editor构建重开及真实彼之矛审核Actor保存，运行验收未执行。
next_action: 玩家在原型图审核彼之矛施工和固定底座移动；联机和FPS另行安排，攻击逻辑尚未开发。
relations:
  work_items: [WORK-20261005-002, WORK-20261005-001]
status_note: 美术B、导入回读及获准编译通过；玩法/联机/FPS未运行，现有预算和源剪影差额仍记录。
---

# 六兵种已审三档正式切换

用户在打开`/Game/Maps/LVL_CommanderMassPrototype`审核Map后明确“审核通过”，对应`CommanderLOD_3Tier_v1`、SHA256 `761bc5edd06b08c598771943b46ebcbd7bc8285b42d345e234fea0907d02783d`。[B决定](../../ArtSource/CommanderLOD_20261005/approval_B.json)固定此范围，53项制作产物、批准证据和冻结源仍保持原哈希。先前[候选交付](20261005-指挥官三档LOD纠正与候选资源交付.md)记录的是批准前状态。

正式六兵种为LOD0近景、LOD1中景、LOD2远景，保留已审近景、ID、尺寸、UV、挂点、配色、线稿例外、动画与玩法参数。WM01/ID2仍是重防号，玩家Ground不迁移。近景并未重新压缩。

## 资源清单

| 最终项目内文件夹 | 来源 | 内容与依赖边界 |
|---|---|---|
| `/Game/GuLiStrike/Commander/Units/DefaultSoldier/LOD_3Tier_v1` | RSGMech已审源 → 本轮三档候选 | 15资产；静态ISM、材质/纹理与7动作旧刚性VAT；44条离线变换数据保留，运行无骨骼组件 |
| `/Game/GuLiStrike/Commander/Units/WM01/LOD_3Tier_v1` | Commander/Tactical/Cel/WarMachine → 本轮候选 | 6资产；三档静态模型及机械WPO依赖；旧稳定实现路径保留 |
| `/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/LOD_3Tier_v1` | ElectromagneticMiner原车辆 → 本轮候选 | 14资产；表现蓝图、8个可见部件、材质/纹理；原11骨骼底盘的Skeleton/PhysicsAsset共享，组件变换不变 |
| `/Game/GuLiStrike/Commander/Units/ConstructionVehicle/LOD_3Tier_v1` | ConstructionVehicle原车辆 → 本轮候选 | 14资产；表现蓝图及三档部件，原兼容骨架/物理资产共享 |
| `/Game/GuLiStrike/Commander/Units/SweeperSummon/LOD_3Tier_v1` | Sweeper原静态模型 → 本轮候选 | 4资产；原机械变形与无线稿例外保留 |
| `/Game/GuLiStrike/Commander/Units/BiZhiMao/LOD_3Tier_v1` | ControlRig已审B_v4 → 本轮三档候选 | 23资产；军队/施工静态模型、三对VAT纹理、材质/状态材质、零骨骼定义及UI肖像；152骨骼仅留离线制作源，原部署资源保持 |

共76个正式资产，内部依赖不再引用`/Game/GuLiStrike/Commander/LODReview_20261005`。原包、候选、冻结源与旧正式资源保留。当前[正式清单](../../ArtSource/CommanderLOD_20261005/formal_delivery.json)包含每组目录和数据引用；审核网页仍使用被批准的真实原帧，正式副本通过几何/UV、材质区段、动画和依赖回读。

Soldiers仅更改ModelAsset/PresentationClass/VATDefinition；Buildings ID8仅更改Mesh。原始Excel/JSON和原生全字段快照保留，所有非引用源值及原生值回读一致。最初旧模块缺少新增字段，完整管线拒绝后已恢复源/原生引用；获准构建加载后重新执行完整导出、导入和回读，未留下半组切换。

## 获准构建

用户明确“现在编译并重开 UE”，授权`GuLiStrikeEditor Win64 Development`。实际引擎`D:/UnrealEngine-5.7`，构建退出码0。Unity编译发现私有辅助函数重名，将`GuLiMissileFixture.cpp`内部Allowed/Fail改为IsMissileFixtureSession/MissileFixtureFailure，行为和公开接口不变。

源码引擎、项目、UnrealMCPython、UnrealMCP及GuLiMapAuthoring的BuildId一致，为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。UE重开后已回读新的顶点VAT、ConstructionOnly/FacingPolicy/避障字段。未执行Live Coding、Hot Reload或引擎整目标重编。[构建记录](../../ArtSource/CommanderLOD_20261005/Reports/native_build.json)分别保存初次失败与成功重试。

## 验证与当前入口

六组正式模型恰有三档，静态网格近景及有源描述档位的位置/UV完整比较一致；自动生成档位的原生复制、缩减参数、渲染面数/UV通道/材质绑定一致。车辆骨架/物理及8组件装配兼容，先驱号7动作、CPU变换和枪口保持。彼之矛三组纹理和三档着色器索引校验通过，0骨骼；2倍模型600cm步幅使144cm/s对应0.24周期/秒，源测量300cm保持。

审核Map已保存18组、60个正式网格组件，EditorOnly、无碰撞/导航占位；原7个部署点不变，没有ID6开局部署。彼之矛旧审核区`(15000,70000)`cm的13实体保留，`BiZhiMaoQA_Sample`换为真实GuLiBiZhiMaoReviewActor，配可移动目标与两台工程车准备参数。GameTexts、肖像与Building.7图标已安装并回读，其他UI项保持。

[地图组件和引用回读](../../ArtSource/CommanderLOD_20261005/Reports/formal_review_scene.json) · [完整表导入](../../ArtSource/CommanderLOD_20261005/Reports/formal_table_import.json) · [静态记录](../../ArtSource/CommanderLOD_20261005/Reports/formal_static_review.json) · [玩法审核步骤](../DevelopmentDocumentation/20261005-彼之矛工程车建造与四足重炮.md#对应地图与后续操作)。

## 剩余边界

车辆远景每台超目标10面；彼之矛70,117／16,913／6,125面，近/远景分别超已有预算30,117／4,125面，动画纹理约97.03MiB；重防号源最简档的剪影/描边偏差保持并记录。用户放行具体版本，不据此判定预算或帧率通过。

本轮没有启动PIE、Standalone、联机、运行自动验收或FPS测量。建造、固定朝向移动、脚部接地、目标跟随及旧单位运行回归等待玩家审核；此处美术B不是兵种玩法终验。没有开发索敌、射击、弹丸、伤害或主动技能。
''',encoding='utf8')
(ART / 'Reports/formal_document_changes.json').write_text(json.dumps(dict(success=True,changes=changes,archive=str(arc.relative_to(ROOT))),ensure_ascii=False,indent=2),encoding='utf8')
print('FORMAL_DOCUMENTS_UPDATED',len(changes),'ARC-20261005-004')
