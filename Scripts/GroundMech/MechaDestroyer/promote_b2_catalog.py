"""Scoped Excel source update through excelize; use the established schema exporter."""
import json,sys,hashlib,tempfile,importlib.util,io,csv,time,subprocess
from pathlib import Path
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'Scripts'))
from Models.migrate_model_workbooks import cli as base_cli,read,letter
def cli(*args):
    for attempt in range(3):
        try:return base_cli(*args)
        except subprocess.CalledProcessError:
            if attempt==2:raise
            time.sleep(.5)
OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
BOOK=ROOT/'Data/Excel/GuLiStrikeModels.xlsx';BASE='/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2'
record=OUT/'catalog_promotion.json'
assert not record.exists(),'Already promoted; inspect existing report before rerunning'
sheets=['Models','Parts','MaterialParameters']
before={s:read(BOOK,s) for s in sheets};before_hash=hashlib.sha256(BOOK.read_bytes()).hexdigest()
baseline_path=OUT/'catalog_json_before.json'
if not baseline_path.exists():baseline_path.write_text(json.dumps({s:json.loads((ROOT/'Data/Json'/('DT_GuLiStrikeModels_'+s+'.json')).read_text(encoding='utf-8-sig')) for s in sheets},ensure_ascii=False,indent=2),encoding='utf8')
def cells(sheet,updates):
    with tempfile.TemporaryDirectory(prefix='b2-catalog-') as tmp:
        p=Path(tmp)/'cells.json';p.write_text(json.dumps(updates,ensure_ascii=False),encoding='utf8')
        cli('write',BOOK,'--sheet',sheet,'--data-file',p)
def patch_row(sheet,identity,updates):
    grid=read(BOOK,sheet);idx=next(i for i,r in enumerate(grid[3:],4) if r[0]==str(identity))
    cells(sheet,[{'cell':letter(grid[0].index(k)+1)+str(idx),'value':v,'type':'bool' if isinstance(v,bool) else 'string'} for k,v in updates.items()])
patch_row('Models',4001,{'ResourcePath':BASE+'/Meshes/SK_GroundMech_A4B2.SK_GroundMech_A4B2','DisplayName':'玩家地面机甲 A4 B2','bTeamColorEnabled':True,'Note':'用户授权导入当前 Blender 定稿；UE效果待用户验收','Description':'Ground玩家整机骨骼，三档明暗、阵营主辅色；左右枪交替，保留原移动、建造、火箭跳、升级和网络玩法。'})
patch_row('Parts',4,{'PartKey':'Body','Note':'A4 B2整机含双枪和喷气口；旧分件模型ID4002-4004保留'})
grid=read(BOOK,'Parts')
for i,r in reversed(list(enumerate(grid[3:],4))):
    if r[0] in ['1','2','3'] and r[grid[0].index('ModelId')]=='4001':cli('delete-rows',BOOK,'--sheet','Parts','--at',i,'--count',1)
grid=read(BOOK,'MaterialParameters');next_id=max(int(r[0]) for r in grid[3:] if r and r[0])+1
templates={r[grid[0].index('ParameterKey')]:dict(zip(grid[0],r)) for r in grid[3:] if r[grid[0].index('ModelId')]=='1001' and r[grid[0].index('Driver')]=='CPD' and r[grid[0].index('Scope')]=='Existing'}
remove=[i for i,r in enumerate(grid[3:],4) if r[grid[0].index('ModelId')]=='4001']
if remove:
    assert remove==list(range(min(remove),max(remove)+1))
    cli('delete-rows',BOOK,'--sheet','MaterialParameters','--at',min(remove),'--count',len(remove))
grid=read(BOOK,'MaterialParameters');updates=[]
for j,key in enumerate(['TeamPrimary','TeamSecondary','TeamEnabled']):
    row=dict(templates[key]);row.update(id=str(next_id+j),name='MaterialParameters_'+str(next_id+j),ModelId='4001',Note='A4 B2 Ground整机；沿用阵营CPD契约')
    for c,k in enumerate(grid[0],1):
        value=row.get(k,'');typ=grid[1][c-1]
        if typ=='bool':value=str(value).lower() in ['1','true']
        elif typ in ['int','float']:value=(int if typ=='int' else float)(value or 0)
        updates.append({'cell':letter(c)+str(len(grid)+j+1),'value':value,'type':typ if typ in ['bool','int','float'] else 'string'})
cells('MaterialParameters',updates)
after={s:read(BOOK,s) for s in sheets}
for s in sheets:
    assert before[s][:3]==after[s][:3]
    idx=0 if s=='Models' else before[s][0].index('ModelId')
    old={r[0]:r for r in before[s][3:] if r and r[0] and r[idx]!='4001'}
    new={r[0]:r for r in after[s][3:] if r and r[0] and r[idx]!='4001'}
    assert old==new,('Unrelated row changed',s)
spec=importlib.util.spec_from_file_location('pipeline',ROOT/'Tools/DataPipeline/export_data_from_excel.py');pipeline=importlib.util.module_from_spec(spec);spec.loader.exec_module(pipeline)
class Sheet:
    def __init__(self,name,grid):self.title=name;self.grid=grid;self.max_row=len(grid);self.max_column=len(grid[0])
    def cell(self,row,column):
        v=self.grid[row-1][column-1] if column<=len(self.grid[row-1]) else ''
        if row>3 and v!='':
            typ=self.grid[1][column-1]
            if typ in ['int','float']:v=(int if typ=='int' else float)(v)
            elif typ=='bool':v=str(v).lower() in ['true','1']
        return SimpleNamespace(value=v if v!='' else None)
manifest=json.loads((ROOT/'Data/Json/manifest.json').read_text(encoding='utf8'))
tables={n:{'rows':json.loads((ROOT/'Data/Json'/f'{n}.json').read_text(encoding='utf-8-sig'))} for n in manifest['tables']}
for s in sheets:tables['DT_GuLiStrikeModels_'+s]={'rows':pipeline.export_sheet(Sheet(s,after[s]))[0]}
pipeline.validate_model_references(tables)
for s in sheets:
    name='DT_GuLiStrikeModels_'+s;rows=tables[name]['rows'];p=ROOT/'Data/Json'/f'{name}.json'
    pipeline.write_if_changed(p,json.dumps(rows,ensure_ascii=False,indent=2))
    output=io.StringIO();w=csv.writer(output,lineterminator='\n');columns=list(rows[0]);w.writerow(columns)
    for row in rows:w.writerow([row.get(k,'') for k in columns])
    pipeline.write_if_changed(p.with_suffix('.csv'),output.getvalue())
record.write_text(json.dumps({'success':True,'before_sha256':before_hash,'after_sha256':hashlib.sha256(BOOK.read_bytes()).hexdigest(),'before':before,'after_ground':{s:[r for r in after[s][3:] if r[0 if s=='Models' else after[s][0].index('ModelId')]=='4001'] for s in sheets},'unrelated_rows_unchanged':True,'native_schema_unchanged':True},ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'success':True,'model_id':4001,'tables':sheets,'new_parameter_ids':list(range(next_id,next_id+3))}))
