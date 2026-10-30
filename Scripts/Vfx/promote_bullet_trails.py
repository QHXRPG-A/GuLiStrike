"""Apply bullet trails directly to the formal Excel and UE presentation references."""
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/FX/BulletTrails_20261028'
BOOK=ROOT/'Data/Excel/GuLiStrikeVfx.xlsx'
CLI=Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
BASE='/Game/GuLiStrike/FX/BulletTrails/Review_A1'


def run_ue(code):
    completed=subprocess.run([sys.executable,str(ROOT/'Scripts/ue_exec.py'),'-'],input=code,text=True,encoding='utf8',capture_output=True,check=True)
    response=json.loads(completed.stdout);assert response.get('success'),response
    result=json.loads(response['result']);assert result.get('success'),result
    return result


def column(index):
    value=''
    while index:
        index,remainder=divmod(index-1,26);value=chr(65+remainder)+value
    return value


def main():
    if len(sys.argv)!=1:raise SystemExit('Run without switches to apply the formal bullet-trail configuration.')
    rows=json.loads(subprocess.check_output([str(CLI),'read',str(BOOK),'--sheet','Effects','--format','json'],encoding='utf8'))['rows']
    headers=rows[0]
    entries={row[headers.index('name')]:(index,row) for index,row in enumerate(rows[3:],4) if row and row[0]}
    assert int(entries['PlayerBullet'][1][0])==8 and int(entries['GroundMachineGunTracer'][1][0])==38
    light_id=int(entries['BulletLightPool'][1][0]) if 'BulletLightPool' in entries else max(int(r[0]) for r in rows[3:] if r and r[0])+1
    changes={
        'PlayerBullet':dict(ResourcePath=BASE+'/NS_GroundBulletTrail_Orange.NS_GroundBulletTrail_Orange',
            Note='Ground固定橙黄曲线；保留24cm线宽和0.12s停留/0.38s淡出；独立限额照明'),
        'GroundMachineGunTracer':dict(ResourcePath=BASE+'/NS_MassBulletTrail_GPU.NS_MassBulletTrail_GPU',
            Note='Mass GPU固定细曲线；线宽6cm；按点残留；独立共享照明',
            ScaleX=1.,ScaleY=1.,ScaleZ=1.,TrailLengthCm=1200.,TrailHoldSeconds=.06,TrailFadeSeconds=.18,TrailBendCm=12.),
        'BulletLightPool':dict(id=light_id,name='BulletLightPool',Note='客户端共享6槽CPU粒子灯；无阴影、不影响透明或体积雾',
            ResourcePath=BASE+'/NS_BulletLights_CPU.NS_BulletLights_CPU',ScaleX=1.,ScaleY=1.,ScaleZ=1.,
            TrailLengthCm=0.,TrailHoldSeconds=0.,TrailFadeSeconds=0.,TrailBendCm=0.)}
    cells=[]
    for name,values in changes.items():
        line=entries[name][0] if name in entries else len(rows)+1
        for key,value in values.items():
            cells.append({'cell':f'{column(headers.index(key)+1)}{line}','value':value,
                          'type':'string' if isinstance(value,str) else 'int' if isinstance(value,int) else 'float'})
    OUT.mkdir(parents=True,exist_ok=True)
    cell_file=OUT/'formal-reference-cells.json';cell_file.write_text(json.dumps(cells,ensure_ascii=False,indent=2),encoding='utf8')
    proposal={'source':str(BOOK),'changes':changes,'light_id':light_id,'applied':False,
              'application_authorization':'User explicitly instructed direct application and removal of the opt-in switch',
              'visual_approval':'not_claimed','performance':'not_measured'}
    report=OUT/'formal-reference-proposal.json';report.write_text(json.dumps(proposal,ensure_ascii=False,indent=2),encoding='utf8')
    run_ue("""
import unreal,json,runpy
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
author=runpy.run_path('D:/UE5.7/test1/Scripts/Vfx/author_bullet_trails.py',run_name='formal_resource_preflight')
for key,gpu in [('MASS',True),('LIGHTS',False),('GROUND',False)]:
    assert bool(author['NS'].summarize(author[key]).has_gpu_emitters)==gpu,key
unreal.MCPythonHelper.submit_result(json.dumps({'success':True}))
""")
    before={name:dict(zip(headers,entry[1])) for name,entry in entries.items()}
    if not (OUT/'formal-reference-before.json').exists():
        (OUT/'formal-reference-before.json').write_text(json.dumps(before,ensure_ascii=False,indent=2),encoding='utf8')
    subprocess.run([str(CLI),'write',str(BOOK),'--sheet','Effects','--data-file',str(cell_file)],check=True)
    with (OUT/'formal-export.log').open('w',encoding='utf8') as log:
        subprocess.run([sys.executable,'-X','utf8',str(ROOT/'Tools/DataPipeline/export_data_from_excel.py')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    code="""
import json,unreal
from pathlib import Path
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(),'Preserve unrelated unsaved content'
table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
source=Path(unreal.Paths.project_dir())/'Data/Json/DT_GuLiStrikeVfx_Effects.json'
assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,source.read_text(encoding='utf8'))
assert unreal.EditorAssetLibrary.save_loaded_asset(table,False)
catalog=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')
catalog.modify()
catalog.set_editor_property('bullet_light_vfx_id',LIGHT_ID)
catalog.set_editor_property('mass_trail_width',6.)
assert unreal.EditorAssetLibrary.save_loaded_asset(catalog,False)
rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
selected={r['Name']:r for r in rows if r['Name'] in ('PlayerBullet','GroundMachineGunTracer','BulletLightPool')}
assert selected['PlayerBullet']['ResourcePath'].endswith('NS_GroundBulletTrail_Orange.NS_GroundBulletTrail_Orange')
assert selected['GroundMachineGunTracer']['ResourcePath'].endswith('NS_MassBulletTrail_GPU.NS_MassBulletTrail_GPU')
assert selected['BulletLightPool']['Id']==LIGHT_ID
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'table':table.get_path_name(),'catalog':catalog.get_path_name(),'light_id':catalog.get_editor_property('bullet_light_vfx_id'),'mass_width':catalog.get_editor_property('mass_trail_width'),'rows':selected}))
""".replace('LIGHT_ID',str(light_id))
    result=run_ue(code)
    after=json.loads(subprocess.check_output([str(CLI),'read',str(BOOK),'--sheet','Effects','--format','json'],encoding='utf8'))['rows']
    current={r[1]:dict(zip(headers,r)) for r in after[3:] if r and r[0]}
    for name,row in before.items():
        if name not in changes:assert current[name]==row,('Unrelated Excel row changed',name)
    proposal.update(applied=True,ue_readback=result,unrelated_excel_rows_unchanged=True)
    report.write_text(json.dumps(proposal,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(proposal,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
