"""Maintain stable model 2003 through Excelize; do not rebuild the catalogue."""
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts'))
from Models.migrate_model_workbooks import cli,read,letter
OUT=ROOT/'ArtSource/Buildings/OutpostSymmetry_20261029/UE_Delivery_v1'
BOOK=ROOT/'Data/Excel/GuLiStrikeModels.xlsx'
RESOURCE='/Game/GuLiStrike/Buildings/OutpostSymmetric/Meshes/SM_OutpostSymmetric.SM_OutpostSymmetric'

def canonical(grid,row):
    out=[]
    for i,value in enumerate(row):
        typ=grid[1][i]
        if typ=='bool':value=str(value).lower() in ('true','1')
        elif typ=='float' and value!='':value=float(value)
        elif typ=='int' and value!='':value=int(value)
        out.append(value)
    return out

def mutate(book,sheet,updates,deleted=(),appended=None):
    before=read(book,sheet)
    original={int(r[0]):canonical(before,r) for r in before[3:] if r and r[0]}
    for i,row in reversed(list(enumerate(before[3:],4))):
        if row and row[0] and int(row[0]) in deleted:
            cli('delete-rows',book,'--sheet',sheet,'--at',i,'--count',1)
    current=read(book,sheet);cells=[]
    for i,row in enumerate(current[3:],4):
        if row and row[0] and int(row[0]) in updates:
            for field,value in updates[int(row[0])].items():
                col=current[0].index(field)
                cells.append({'cell':f'{letter(col+1)}{i}','value':value,'type':
                    'bool' if isinstance(value,bool) else 'int' if isinstance(value,int) else 'float' if isinstance(value,float) else 'string'})
    if appended:
        for i,field in enumerate(current[0],1):
            value=appended.get(field,'')
            cells.append({'cell':f'{letter(i)}{len(current)+1}','value':value,'type':
                'bool' if isinstance(value,bool) else 'int' if isinstance(value,int) else 'float' if isinstance(value,float) else 'string'})
    with tempfile.TemporaryDirectory(prefix='guli-outpost-cells-') as tmp:
        file=Path(tmp)/'cells.json';file.write_text(json.dumps(cells,ensure_ascii=False),encoding='utf8')
        cli('write',book,'--sheet',sheet,'--data-file',file)
    actual=read(book,sheet);by_id={int(r[0]):canonical(actual,r) for r in actual[3:] if r and r[0]}
    for rid,old in original.items():
        if rid in deleted:
            assert rid not in by_id
            continue
        expected=list(old)
        for field,value in updates.get(rid,{}).items():expected[actual[0].index(field)]=value
        assert by_id[rid]==expected,(sheet,rid)
    assert before[:3]==actual[:3]
    return {'sheet':sheet,'changed_ids':list(updates),'deleted_ids':list(deleted),
        'appended_id':appended['id'] if appended else None,'unrelated_rows_exact':True}

def main():
    global BOOK
    source_book=BOOK
    source_buildings=ROOT/'Data/Excel/GuLiStrikeBuildings.xlsx'
    source_hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (source_book,source_buildings)}
    # A read-only desktop preview maps the source XLSX on Windows. Author a
    # temporary Excelize working file and atomically replace the unchanged source.
    working=tempfile.TemporaryDirectory(prefix='outpost-catalog-',dir=OUT)
    BOOK=Path(working.name)/source_book.name
    working_buildings=Path(working.name)/source_buildings.name
    shutil.copyfile(source_book,BOOK);shutil.copyfile(source_buildings,working_buildings)
    current=read(BOOK,'Models');resource_index=current[0].index('ResourcePath')
    assert next(r for r in current[3:] if int(r[0])==2003)[resource_index] != RESOURCE, 'Already promoted; inspect the previous report instead of appending again.'
    report={'success':False,'changes':[]}
    report['changes'].append(mutate(BOOK,'Models',{2003:{
        'ResourcePath':RESOURCE,'CandidateResourcePath':'','CandidateVATDefinition':'','bTeamColorEnabled':True,
        'Description':'上下镜像的科幻悬浮据点；固定外壳色，中央光柱中立白/己方蓝/敌方红；地图据点三轴为护盾发生器3倍。',
        'Note':'2026-10-29 用户放行 B_v1 正式导入并删除旧据点模型；4824三角面；CPD18控制中央光柱白/阵营色。'}}))
    params=read(BOOK,'MaterialParameters');new_id=max(int(r[0]) for r in params[3:] if r and r[0])+1
    base=dict(zip(params[0],next(r for r in params[3:] if int(r[0])==2810)))
    base.update({'id':new_id,'name':f'MaterialParameters_{new_id}','Note':'据点专属核心归属开关；本地阵营系统：未知/中立0=白，有归属1=阵营色；独立于灯强度。',
        'ParameterKey':'CoreUseTeamColor','ParameterName':'GuLi_CoreUseTeamColor','CustomDataIndex':18,'DefaultScalar':1.0,
        'bRuntimeWritable':False,'bTeamManaged':True})
    for key in ['ModelId','CustomDataIndex']:base[key]=int(base[key])
    for key in ['DefaultScalar','DefaultR','DefaultG','DefaultB','DefaultA']:base[key]=float(base[key])
    for key in ['bRuntimeWritable','bTeamManaged']:base[key]=str(base[key]).lower() in ('1','true')
    report['changes'].append(mutate(BOOK,'MaterialParameters',{},deleted=[2808],appended=base))
    report['changes'].append(mutate(BOOK,'ColorRegions',{
        3056:{'RegionKey':'FixedShellAndAccent','DisplayName':'固定外壳与装饰灯','PaintRole':0,'MaskId':0,
              'Note':'B_v1 已审固定色；UV1.x 0..5 对应源材质分类，外壳与装饰灯不随阵营改变。'},
        3057:{'RegionKey':'CentralOwnershipLight','DisplayName':'中央归属光柱','PaintRole':7,'MaskId':7,
              'Note':'B_v1 已审核心；顶点Alpha=7/255；UV1.x 6/7区分光柱表面/高光；中立白，归属后使用本地阵营主色。'}},deleted=list(range(3058,3077))))
    # Model 2003 is also used by the existing constructed building. Keep that
    # building's displayed envelope/ground alignment when changing source pivot.
    building_book=working_buildings
    rows=read(building_book,'Buildings');row=dict(zip(rows[0],next(r for r in rows[3:] if int(r[0])==3)))
    old_size=[7103.08642578125,6619.80517578125,30000.]
    old_origin=[-10.58935546875,252.5859375,15000.]
    size=json.loads((OUT/'ue-asset-import.json').read_text(encoding='utf8'))['size_cm']
    # Vector source fields are flat Excel columns.
    fields={}
    for i,axis in enumerate('XYZ'):
        old_scale=float(row['MeshScale'+axis]);offset=float(row['VisualOffset'+axis])
        fields['MeshScale'+axis]=old_scale*old_size[i]/size[i]
        fields['VisualOffset'+axis]=offset+old_scale*old_origin[i]
    report['changes'].append(mutate(building_book,'Buildings',{3:fields}))
    for file,digest in source_hashes.items():
        assert hashlib.sha256(file.read_bytes()).hexdigest()==digest,'Source workbook changed during authoring: '+str(file)
    os.replace(BOOK,source_book)
    os.replace(working_buildings,source_buildings)
    BOOK=source_book
    working.cleanup()
    report.update(success=True,model_id=2003,resource=RESOURCE,new_core_parameter_id=new_id,
        stable_ids_preserved=True,collision_economy_and_gameplay_fields_unchanged=True)
    (OUT/'catalog-source-update.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(report,ensure_ascii=True))

if __name__=='__main__':main()
