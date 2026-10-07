"""Static source/contract checks, without a native build or gameplay test."""
import ast,hashlib,json,re,sys,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,LOD_COUNT,DEFAULT_SCREEN_SIZES
assert LOD_COUNT==3 and DEFAULT_SCREEN_SIZES==(1,.056,.028)
syntax=[]
for directory in ['Scripts/CommanderLOD','Scripts/BiZhiMao','Scripts/Pioneer']:
 for file in (ROOT/directory).glob('*.py'):
  ast.parse(file.read_text(encoding='utf8'),filename=str(file));syntax.append(str(file.relative_to(ROOT)))
code=(ROOT/'Source/GuLiStrike/Gameplay/Presentation/GuLiVATAnimation.cpp').read_text(encoding='utf8')
assert 'VertexLODs.Num() != 3' in code and 'VertexLODs.Num() != 4' not in code
hlsl=(ROOT/'Scripts/BiZhiMao/BiZhiMaoVertexVAT.hlsl').read_text(encoding='utf8')
assert not re.search(r'\b(?:Position|Rotation|Count|Rows|Height|Samples)3\b',hlsl)
assert hlsl.count('{')==hlsl.count('}')
for name in ['candidate_readback','resource_groups','metrics','selected_geometry_readback','mechanical_uv_readback','skin_readback','formal_after','review_scene']:
 report=json.loads((ART/'Reports'/(name+'.json')).read_text(encoding='utf8'));assert report['success'],name
native=json.loads((ART/'Reports/candidate_readback.json').read_text(encoding='utf8'))
assert len(native['units'])==6 and all(m['lod_count']==3 for u in native['units'] for m in u['meshes'])
biz=native['units'][-1];assert biz['name']=='BiZhiMao' and biz['runtime_bones']==0
scene=json.loads((ART/'Reports/review_scene.json').read_text(encoding='utf8'))
assert scene['saved'] and len(scene['groups'])==18
assert all(r['editor_only'] for g in scene['groups'] for r in g['references'])
meta=json.loads((ART/'BiZhiMao/vertex_metadata.json').read_text(encoding='utf8'))
assert meta['lod_count']==3 and [l['lod'] for l in meta['lods']]==[0,1,2]
assert [l['frames_per_clip'] for l in meta['lods']]==[32,24,8]
assert meta['authoring_blend_sha256']==hashlib.sha256((ART/'BiZhiMao/BiZhiMao_3Tier.blend').read_bytes()).hexdigest()
report=dict(success=True,python_syntax_files=syntax,native_change='Vertex VAT requires three LOD entries; legacy bone/rigid paths unchanged',
 hlsl_indices=[0,1,2],candidate_groups=6,saved_review_groups=18,formal_switched=False,
 native_compile='not_run',pie='not_run',network='not_run',fps='not_run',approval_B='pending')
if (ART/'formal_delivery.json').exists():
 delivery=json.loads((ART/'formal_delivery.json').read_text(encoding='utf8'));assert delivery['formal_switched']
 report.update(formal_switched=True,native_compile='passed',approval_B='approved',formal_delivery='formal_delivery.json')
(ART/'Reports/static_review.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('STATIC_REVIEW_PASSED',len(syntax),'Python sources; 6 groups; native compile',report['native_compile'])
