"""Install Ground's alternating fire animation, stylized explosive flight and data tables."""
import json,math,re,runpy,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/GroundAlternatingLoop_20261108'
ANIMS='/Game/GuLiStrike/GroundMech/Animations/A4B2'
FX='/Game/GuLiStrike/FX/GroundMech/ExplosivePackage'
FLIGHT=FX+'/NS_GroundExplosiveFlight_Toon'
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

def require(v,message):
    if not v:raise RuntimeError(message)
    return v

def animation():
    bp=require(ASSETS.load_asset(ANIMS+'/ABP_GroundMech_A4B2'),'Formal animation blueprint')
    source=require(ASSETS.load_asset(ANIMS+'/Anim_Mecha_Shoot'),'Formal firing source')
    skeleton=source.get_editor_property('skeleton')
    loop=ASSETS.load_asset(ANIMS+'/Anim_GroundFire_AlternatingLoop')
    if loop is None:loop=require(ASSETS.duplicate_asset(source.get_path_name(),ANIMS+'/Anim_GroundFire_AlternatingLoop'),'Duplicate loop')
    name='AM_GroundFire_AlternatingLoop';montage=ASSETS.load_asset(ANIMS+'/'+name)
    if montage is None:
        factory=unreal.AnimMontageFactory();factory.set_editor_property('target_skeleton',skeleton);factory.set_editor_property('source_animation',loop)
        montage=require(unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,ANIMS,unreal.AnimMontage,factory),'Create loop montage')
    result=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.configure_fire_animation(bp,source,loop,montage))
    require(result['success'],'Author alternating loop: '+str(result))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    component=unreal.new_object(unreal.SkeletalMeshComponent)
    instance=unreal.new_object(bp.generated_class(),outer=component)
    require(instance.get_editor_property('FireLoopMontage')==montage,'Fresh animation instance inherits loop')
    for obj in [skeleton,loop,montage,bp]:require(unreal.EditorAssetLibrary.save_loaded_asset(obj,False),'Save '+obj.get_path_name())
    result=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.read_fire_animation(bp))
    require(result['success'],'Saved alternating loop')
    return result

def missile():
    path = ROOT/'Scripts/GroundMech/Rogue/author_explosive_toon_flight.py'
    author = runpy.run_path(str(path), init_globals={
        'GULI_EXPLOSIVE_TOON_BUILD': False,
        'GULI_EXPLOSIVE_TOON_RETRY': globals().get('GULI_FIRE_OWNED_RETRY', False),
    })
    require(author['SYSTEM']==FLIGHT, 'Explosive flight authoring target changed')
    return author['build']()


def tables(names=('DT_GuLiStrikeMech_Gait','DT_GuLiStrikeRogueCards_Effects','DT_GuLiStrikeRogueCards_GroundWeaponEffects','DT_GuLiStrikeVfx_Effects')):
    metadata=json.loads((ROOT/'Data/Json/manifest.json').read_text(encoding='utf8'))['tables'];report=[]
    for name in names:
        source=(ROOT/'Data/Json'/f'{name}.json').read_text(encoding='utf-8-sig');table=require(unreal.load_asset('/Game/GuLiStrike/Data/'+name),'Existing '+name)
        require(table.get_editor_property('row_struct')==unreal.find_object(None,metadata[name]['struct']),'Loaded schema '+name)
        require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,source),'Import '+name)
        expected=json.loads(source);actual=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table));by_key={r['Name']:r for r in actual}
        require(set(by_key)=={r['Name'] for r in expected},'Row identity '+name)
        for row in expected:
            for key,value in row.items():
                got=by_key[row['Name']].get(key)
                # UE's table JSON writer emits six significant digits for float properties.
                if isinstance(value,float):require(isinstance(got,(int,float)) and math.isclose(value,got,rel_tol=5e-6,abs_tol=1e-5),'Number '+key)
                elif isinstance(value,dict):
                    parts=dict(re.findall(r'(\w+)=([^,)]+)',str(got)));require(set(parts)==set(value) and all(math.isclose(float(parts[k]),v,rel_tol=1e-6,abs_tol=1e-5) for k,v in value.items()),'Struct '+key)
                else:
                    if isinstance(got,str) and "'" in got and got.endswith("'"):got=got.split("'",1)[1][:-1]
                    if got=='None':got=''
                    require(got==value,'Field '+name+'/'+row['Name']+'.'+key)
        require(unreal.EditorAssetLibrary.save_loaded_asset(table,False),'Save '+name)
        require(json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))==actual,'Saved data '+name)
        report.append({'table':name,'rows':len(actual),'saved_readback':True})
    return report

def vfx_table():
    name = 'DT_GuLiStrikeVfx_Effects'
    source = json.loads((ROOT/'Data/Json'/f'{name}.json').read_text(encoding='utf-8-sig'))
    missile = next(row for row in source if row['Id'] == 63)
    require(missile['ResourcePath'] == FLIGHT+'.'+FLIGHT.rsplit('/',1)[-1], 'Export the stylized explosive flight VFX row first')
    require(ASSETS.load_asset(FLIGHT), 'Save the stylized explosive flight before changing the registry')
    return tables((name,))

def main():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'Preserve active play')
    stage=globals().get('GULI_FIRE_STAGE','all')
    require(stage in ('all','animation','missile','tables','vfx'),'Unknown install stage '+stage)
    dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if stage=='missile':
        dirty.intersection_update({FLIGHT, FX+'/M_GroundExplosiveToonShell', FX+'/M_GroundExplosiveToonPlume'})
    elif stage=='vfx':
        dirty.intersection_update({'/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects'})
    require(not dirty or globals().get('GULI_FIRE_OWNED_RETRY',False),'Preserve unsaved content')
    report={'success':True,'stage':stage,'assistant_visual_test':False}
    if stage=='vfx':
        report[stage]=vfx_table()
        return report
    for name,fn in [('animation',animation),('missile',missile),('tables',tables)]:
        if stage in ('all',name):report[name]=fn()
    return report
try:report=main()
except Exception:report={'success':False,'error':traceback.format_exc()}
if not globals().get('GULI_FIRE_NO_ARTIFACT',False):
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/('asset_install_'+globals().get('GULI_FIRE_STAGE','all')+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report,ensure_ascii=False))
