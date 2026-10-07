"""Build the current six-unit three-tier review from actual saved candidate evidence."""
import json,hashlib,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,units,REVIEW_PACKAGE
def read(file):return json.loads(file.read_text(encoding='utf-8-sig'))
approval=read(ART/'approval_B.json') if (ART/'approval_B.json').exists() else None
delivery=read(ART/'formal_delivery.json') if (ART/'formal_delivery.json').exists() else None
approved=bool(approval and approval['approval_B']=='approved')
switched=bool(delivery and delivery['formal_switched'])
native=read(ART/'Reports/candidate_readback.json');metrics=read(ART/'Reports/metrics.json')
blender=read(ART/'Reports/blender_candidates.json');groups=read(ART/'Reports/resource_groups.json')
data=[];manifest=[];frames=[]
for unit in units():
 name=unit['Name'];folder=ART/name;report=read(folder/'Reports/ue_preview.json')
 assert report['success'] and report['saved_sample_poses_restored'],name
 for frame in report['frames']:
  image=Path(frame['file']).with_suffix('.png');assert image.is_file(),image
  frames.append(frame)
 candidate=next(x for x in native['units'] if x['name']==name);assert all(x['lod_count']==3 for x in candidate['meshes'])
 measure=next(x for x in metrics['units'] if x['name']==name)
 blend=folder/(name+'_3Tier.blend');assert blend.exists()
 source='Sources/*.fbx' if name!='BiZhiMao' else 'FBX/*.fbx + Textures/*'
 clips=sorted({x['clip'] for x in report['frames'] if x['view']=='Motion'})
 if name in ['ElectromagneticMiner','ConstructionVehicle']:clips=['Mechanism']
 screens=candidate['meshes'][0]['screen_sizes']
 artifacts=[]
 extras=[folder/'vertex_metadata.json']+sorted(folder.glob('Textures/*')) if name=='BiZhiMao' else []
 for file in [blend]+sorted(folder.glob('FBX/*.fbx'))+sorted(folder.glob('Sources/*.fbx'))+extras:
  assert file.suffix not in ['.uasset','.umap','.ubulk','.uexp','.pak']
  artifacts.append(dict(path=str(file.relative_to(ART)).replace('\\','/'),bytes=file.stat().st_size,
   sha256=hashlib.sha256(file.read_bytes()).hexdigest()))
 manifest.append(dict(name=name,id=unit['Id'],artifacts=artifacts,resource_group=next(g for g in groups['groups'] if g['name']==name)))
 data.append(dict(name=name,id=unit['Id'],display=unit['DisplayName'],triangles=candidate['triangles'],
  lods=measure['lods'],screen_sizes=screens,clips=clips,blend=str(blend.relative_to(ART)).replace('\\','/'),
  textures=next(u['textures'] for u in read(ART/'Reports/texture_budget.json')['units'] if u['name']==name)))
evidence=[]
for file in [ART/'Reports'/name for name in ['candidate_readback.json','material_section_readback.json','metrics.json','resource_groups.json','texture_budget.json','formal_after.json']]+[ROOT/'Scripts/BiZhiMao/BiZhiMaoVertexVAT.hlsl']:
 evidence.append(dict(path=str(file.relative_to(ROOT)).replace('\\','/'),sha256=hashlib.sha256(file.read_bytes()).hexdigest()))
version=dict(version='CommanderLOD_3Tier_v1',lod_count=3,approval_B='pending',formal_switched=False,
 evidence=evidence,
 art_revision='1.3',review_map='/Game/Maps/LVL_CommanderMassPrototype',candidate_package=REVIEW_PACKAGE,units=manifest)
payload=json.dumps(version,ensure_ascii=False,indent=2);version['content_sha256']=hashlib.sha256(payload.encode()).hexdigest()
if approved:
 frozen=read(ART/'review_manifest.json')
 assert frozen['content_sha256']==version['content_sha256']==approval['content_sha256'],'Approved evidence changed; create a new review version.'
 version=frozen
else:
 (ART/'review_manifest.json').write_text(json.dumps(version,ensure_ascii=False,indent=2),encoding='utf8')
 (ART/'Reports/ue_preview.json').write_text(json.dumps(dict(success=True,frames=frames,frame_count=len(frames),
  scope='Actual editor renders, isolated per unit after correcting visibility; no PIE, network or FPS validation.'),indent=2),encoding='utf8')
template=(ROOT/'Scripts/CommanderLOD/review_template.html').read_text(encoding='utf8')
template=template.replace('__DATA__',json.dumps(data,ensure_ascii=False)).replace('__VERSION__',version['content_sha256'][:12])
template=template.replace('__APPROVAL__','实际版本 B 已通过' if approved else '实际版本 B 待审核')
template=template.replace('__CURRENT_STATE__','六种单位的正式三档资源已切换并保存回读。原生编译已通过、UE已重开；PIE、联机、移动/建造运行验收及帧率测量未运行。' if switched else '正式资源尚未切换；静态检查、美术回读和实际渲染已记录，运行验收另记。')
template=template.replace('__BUDGET_DISPOSITION__','当前版本已放行，预算差额继续登记。' if approved else '差额等待审核。')
template=template.replace('__VISUAL_DISPOSITION__','用户已放行当前具体版本；源档的剪影与描边偏差仍保留在记录中。' if approved else '不将该偏差登记为通过。')
review=ART/'Review';review.mkdir(exist_ok=True);(review/'index.html').write_text(template,encoding='utf8')
for unit in data:
 folder=ART/unit['name'];screen=' / '.join(f'{v:g}' for v in unit['screen_sizes'])
 notes=['# '+unit['display']+' · 三档候选','',
  '版本：`CommanderLOD_3Tier_v1`；'+('实际版本 B 已通过，正式资源已切换。' if switched else '实际版本 B 待审核，正式资源未切换。'),
  '总共 LOD0 近景、LOD1 中景、LOD2 远景；UE面数：'+' / '.join(f'{n:,}' for n in unit['triangles'])+'。',
  '屏幕尺寸：`'+screen+'`。保留已审近景与原变形路径。','',
  '[当前审核网页](../Review/index.html) · [版本与哈希](../review_manifest.json) · [UE回读](../Reports/candidate_readback.json)',
  '', '可编辑源：`'+unit['name']+'_3Tier.blend`；真实效果与动作在 `Review/UE`，离线几何/机构预览在 `Review/Blender`（彼之矛在 `Review`）。',
  '预算与差额见 `../Reports/metrics.json`。资源预算不等于帧率；'+('原生编译已通过，PIE/联机/FPS未运行。' if switched else '原生编译、PIE和联机未执行。')]
 if switched:
  formal=next(g for g in delivery['groups'] if g['name']==unit['name'])
  notes.extend(['','正式目录：`'+formal['formal_root']+'`。','[B决定](../approval_B.json) · [正式交付](../formal_delivery.json) · [正式回读](../'+formal['readback']+')'])
 (folder/'README.md').write_text('\n'.join(notes)+'\n',encoding='utf8')
if switched:
 roots='\n'.join('- `'+g['formal_root']+'`' for g in delivery['groups'])
 (ART/'README.md').write_text('''# 指挥官三档 LOD 当前入口

版本 `CommanderLOD_3Tier_v1`，规范 v1.3。六种单位总共 LOD0 近景、LOD1 中景、LOD2 远景；已审近景保留。

用户已通过本具体版本 B，六组正式资源已切换并保存回读。源码版 Editor 编译通过、UE 已重开；PIE、联机、移动/建造运行验收和 FPS 未运行。彼之矛攻击逻辑未实现。

[成品及动作](Review/index.html) · [B决定](approval_B.json) · [正式交付和当前引用](formal_delivery.json) · [冻结审核哈希](review_manifest.json) · [预算差额](Reports/metrics.json)

正式入口：

'''+roots+'''

当前脚本入口 `Scripts/CommanderLOD`。正式回读使用 `verify_formal_group.py --arguments '{"unit":"<Soldiers.Name>"}'` 经 `ue_rpc.py` 调用编辑器。文档/网页更新使用 `build_review.py`，沿用已批准哈希，不重写审核证据。

本目录的 Blender、FBX、VAT 纹理和审核证据已冻结；后续几何或动画修订必须建立新版本后再审核。原始源包和历史机器回读保持，旧脚本不是当前制作入口。

审核Map `/Game/Maps/LVL_CommanderMassPrototype`：`CommanderLOD_` 为18组三档正式样机；`BiZhiMaoQA_` 为13个已保存玩法审核实体，样机使用真实无骨骼VAT。先前阶段记录保留，当前状态以正式交付清单为准。
''',encoding='utf8')
else:
 (ART/'README.md').write_text('''# 指挥官三档 LOD 当前制作入口

版本 `CommanderLOD_3Tier_v1`，规范 v1.3。六种单位总共 LOD0 近景、LOD1 中景、LOD2 远景；近景保留。

[实际成品审核](Review/index.html) · [版本与哈希](review_manifest.json) · [预算与差额](Reports/metrics.json) · [资源组](Reports/resource_groups.json)

制作入口 `Scripts/CommanderLOD`。原始冻结模型和机器回读是证据，不作为当前生产输入的默认入口。

流程：只读源查询 → `stage_native.py` → `preserve_vehicle_uv.py` → `refine_vehicle_lods.py` → Blender `prepare_blender.py`、`prepare_bizhimao.py` → `stage_bizhimao.py` → `stage_resource_groups.py` → 导出与回读 → Blender/UE实际预览 → `build_review.py`。彼之矛后续再烘焙使用 `Scripts/BiZhiMao/bake_vertex_vat.py`，默认本目录的三档源。

UE脚本经 `python -X utf8 Scripts/CommanderLOD/ue_rpc.py <脚本>` 调用已打开编辑器；Blender脚本用后台 Blender `--python` 执行。模型候选位于 `/Game/GuLiStrike/Commander/LODReview_20261005`。对应审核样机保存在 `/Game/Maps/LVL_CommanderMassPrototype`，标记 `CommanderLOD_`，坐标从 `(48000,-72000,600)`cm 开始，三档列间距6000cm、兵种行间距7500cm。

本版本 B 待用户决定，正式 Soldiers、建筑、玩家 Ground 和原资源组未切换。审核通过后按资源组执行切换与回读，失败恢复该组。原生编译需用户许可，PIE/联机/帧率测试本轮未运行。勘误原文和旧脚本保存在 Reports 的ZIP证据中。
''',encoding='utf8')
print('REVIEW_READY',version['version'],version['content_sha256'],len(frames),flush=True)
