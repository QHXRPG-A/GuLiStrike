"""Check the portable inputs, native exports, and compatibility height data."""
from pathlib import Path
import ast
import hashlib
import json
import numpy as np
from analyze_exports import read16

ROOT=Path(__file__).resolve().parents[1]
FINAL=ROOT.parent.parent/'Builds'/ROOT.name/'final'
project=ROOT/(ROOT.name+'.terrain')
graph=json.loads(project.read_text(encoding='utf-8-sig'))
ids=[];references=[]
def inspect(value):
    if isinstance(value,dict):
        if '$id' in value:ids.append(value['$id'])
        if '$ref' in value:references.append(value['$ref'])
        for child in value.values():inspect(child)
    elif isinstance(value,list):
        for child in value:inspect(child)
inspect(graph)
assert len(ids)==len(set(ids)), 'Duplicate serialized references'
assert set(references)<=set(ids), 'Missing serialized references'
asset=graph['Assets']['$values'][0];terrain=asset['Terrain']
assert terrain['Width']==1800 and terrain['Height']==140
assert abs(terrain['Ratio']-140/1800)<1e-9
assert asset['BuildDefinition']['Resolution']==4096
assert asset['State']['PreviewResolution']==1024
nodes=[v for v in terrain['Nodes'].values() if isinstance(v,dict) and '$type' in v]
file_nodes=[n for n in nodes if n['$type'].startswith('QuadSpinner.Gaea.Nodes.File,')]
for node in file_nodes:
    relative=Path(node['FileName'])
    assert node['RelativePath'] and not relative.is_absolute(),node['Name']
    source=(ROOT/relative).resolve()
    assert source.is_relative_to(ROOT) and source.is_file(),str(source)
    assert read16(source).shape==(4096,4096),str(source)
for node in nodes:
    if node['$type'].startswith('QuadSpinner.Gaea.Nodes.Combine,'):
        assert [p['Name'] for p in node['Ports']['$values']]==['In','Out','Input2','Mask']
for source in (ROOT/'inputs').glob('*.png'):
    assert read16(source).shape==(4096,4096),str(source)
for source in (ROOT/'scripts').glob('*.py'):
    ast.parse(source.read_text(encoding='utf-8'),filename=str(source))

validation=json.loads((FINAL/'validation.json').read_text(encoding='utf-8'))
assert validation['passed'] and validation['connectivity']['component_count']==1
build=json.loads((FINAL/'report.json').read_text(encoding='utf-8'))
assert build['Result']=='Success' and build['NodeCount']==len(nodes)
assert hashlib.sha256((FINAL/'height_4096.png').read_bytes()).hexdigest()==validation['height_sha256'], 'Native height changed since slope validation'
height=read16(FINAL/'height_4096.png')
assert height.shape==(4096,4096) and height.min()==0 and height.max()==65535
compatible=read16(FINAL/'height_4081.png')
assert compatible.shape==(4081,4081)
binary=np.fromfile(FINAL/'height_4081.r16',dtype='<u2').reshape((4081,4081))
assert np.array_equal(binary,compatible),'R16 differs from PNG height data'

mask_stats={}
for key in ['flat_ground','hills','plateau','rock','beach','water']:
    native=read16(FINAL/('mask_'+key+'.png'))
    source=read16(ROOT/'inputs'/('mask_'+key+'.png'))
    derived=read16(FINAL/('material_'+key+'.png'))
    assert native.shape==(4096,4096) and native.max()>0,key
    error=np.abs(native.astype(np.int32)-source.astype(np.int32))
    refresh=np.abs(derived.astype(np.int32)-source.astype(np.int32))
    # Native File/Export may quantize intermediate texture samples. A maximum
    # error under 64 uint16 levels is less than 0.1% of the normalized range.
    assert error.max()<=64,(key,int(error.max()))
    assert refresh.max()<=1,(key,'Source masks do not match the final height',int(refresh.max()))
    mask_stats[key]={'resolution':list(native.shape),'native_source_max_error_u16':int(error.max()),
      'native_source_mean_error_u16':float(error.mean()),'source_final_max_error_u16':int(refresh.max()),
      'normalized_coverage_km2':float(source.astype(np.float64).sum()/65535*(1800/4095)**2/1e6)}

files=[p for p in ROOT.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
files.extend(p for p in FINAL.iterdir() if p.is_file() and p.name!='delivery-checks.json')
checks={'passed':True,'project_name':ROOT.name,'node_count':len(nodes),'relative_file_nodes':len(file_nodes),
  'source_png_count':len(list((ROOT/'inputs').glob('*.png'))),'build_result':build['Result'],
  'height_bit_depth':16,'height_decode':'u16/65535*140-20 meters','sea_level_normalized':1/7,
  'compatibility_r16_bytes':(FINAL/'height_4081.r16').stat().st_size,'masks':mask_stats,
  'files':{str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else 'outputs/final/'+p.name:
       {'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files}}
(FINAL/'delivery-checks.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:checks[k] for k in ['passed','node_count','relative_file_nodes','source_png_count','build_result','compatibility_r16_bytes']}))
