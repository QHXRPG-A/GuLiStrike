"""Build scoped project copies; marketplace effects remain read-only."""
import json
import re
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/BurningRogue_20261029'
BASE='/Game/GuLiStrike/FX/GroundMech/Burning'
STATUS=BASE+'/NS_GroundBurningStatus'
BULLET=BASE+'/NS_GroundBurningBullet'
MATERIAL=BASE+'/M_GroundBurningBullet'

def require(value,message):
    if not value:raise RuntimeError(message)
    return value
def copy(source,dest):
    require(dest.startswith(BASE+'/'),'Owned destination required')
    return require(unreal.load_asset(dest) or unreal.EditorAssetLibrary.duplicate_asset(source,dest),'Copy failed: '+dest)

def main():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'Do not author during PIE')
    require(hasattr(unreal.GuLiCombatEffectAuthoringLibrary,'configure_buff_status_system'),'Rebuild/reload the approved native authoring helper first')
    dirty={str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not any(p in dirty for p in [STATUS,BULLET,MATERIAL]),'Preserve unsaved VFX work')
    report={'success':False,'source_status':'/Game/Assets/VFX/Movement/AdvancedDashVFX/NiagaraVFX/NS_Dash_06','assets':[],'assistant_visual_test':False,'PIE':False}
    status=copy(report['source_status'],STATUS)
    emitters=list(unreal.NiagaraService.summarize(STATUS).get_editor_property('emitter_names'))
    for emitter in emitters:
        name=str(emitter)
        modules=unreal.NiagaraEmitterService.list_modules(STATUS,name)
        for mod in modules:
            if 'SkeletalMeshLocation' in str(mod.get_editor_property('script_asset_path')):
                require(unreal.NiagaraEmitterService.remove_module(STATUS,name,str(mod.get_editor_property('module_name'))),'Remove mesh-only source binding')
        modules=unreal.NiagaraEmitterService.list_modules(STATUS,name)
        if not any('/Location/V2/ShapeLocation' in str(m.get_editor_property('script_asset_path')) for m in modules):
            require(unreal.NiagaraEmitterService.add_module(STATUS,name,'/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation','ParticleSpawn'),'Add generic body volume')
        # This engine enum is user defined; Niagara requires its internal name.
        require(unreal.NiagaraEmitterService.set_module_input(STATUS,name,'EmitterState','Loop Behavior','NewEnumerator0'),'Loop burning emitter '+name)
        require(unreal.NiagaraEmitterService.get_emitter_properties(STATUS,name).get_editor_property('loop_behavior')=='Infinite','Loop readback '+name)
    require(unreal.GuLiCombatEffectAuthoringLibrary.configure_buff_status_system(status) is not None,'Configure local-space status')
    status.set_editor_property('max_pool_size',128)
    status.set_editor_property('pool_prime_size',0)
    compiled=unreal.NiagaraService.compile_with_results(STATUS)
    require(compiled.success and compiled.error_count==0,str(compiled))
    require(unreal.NiagaraService.save_system(STATUS),'Save status')
    report['assets'].append({'path':STATUS,'compile':str(compiled),'emitters':[str(e) for e in emitters],'mesh_dependency_removed':True,
        'emitter_properties':[str(unreal.NiagaraEmitterService.get_emitter_properties(STATUS,str(e))) for e in emitters],
        'diagnostics':unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(status)})
    rows=json.loads((ROOT/'data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf-8'))
    if isinstance(rows,dict):rows=rows['rows']
    player=next(r for r in rows if r['Name']=='PlayerBullet')
    source=player['ResourcePath'].split('.')[0]
    bullet=copy(source,BULLET)
    source_material=source.rsplit('/',1)[0]+'/M_GroundBulletTrail_Orange'
    material=copy(source_material,MATERIAL)
    shader=next(n for n in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if n.get_outer()==material and 'HeadDistance' in str(n.get_editor_property('code')) and 'ChunkOffset' in str(n.get_editor_property('code')))
    code=str(shader.get_editor_property('code'))
    if 'GULI_BURNING_BULLET_V1' not in code:
        marker='''// GULI_BURNING_BULLET_V1: fire rides the same frozen world-space trail.
float flameTime=max(HeadDistance,0.0)/max(Speed,1.0)+max(EndAge,0.0);
float tongueWave=sin(along*0.027-flameTime*23.0)+0.6*sin(along*0.071+flameTime*17.0);
'''
        code=marker+code
        code=re.sub(r'(float d\s*=.*?;)',r'\1\nd=max(0.0,d-abs(tongueWave)*max(Width*0.7,0.2));',code,count=1)
        code=code.replace('float body =','float tongues=exp(-pow(d/max(Width*2.2,0.2),2.0))*saturate(tongueWave*0.5+0.5)*0.5;\nfloat body =',1)
        require('float tongues=' in code,'Unknown Ground trail material contract')
        code=code.replace('return float4(rgb,','rgb+=Emission*TailColor.rgb*tongues;\nreturn float4(rgb,',1).replace('saturate(body+glow)','saturate(body+glow+tongues)')
        shader.set_editor_property('code',code)
    unreal.MaterialEditingLibrary.recompile_material(material)
    diagnostic=unreal.MaterialNodeService.get_material_diagnostics(MATERIAL)
    require(diagnostic.success and diagnostic.is_compiled_ok,str(diagnostic))
    require(unreal.EditorAssetLibrary.save_loaded_asset(material,False),'Save burning material')
    for emitter in unreal.NiagaraService.summarize(BULLET).get_editor_property('emitter_names'):
        for i,_ in enumerate(unreal.NiagaraEmitterService.list_renderers(BULLET,str(emitter))):
            require(unreal.NiagaraEmitterService.set_renderer_property(BULLET,str(emitter),i,'Material',MATERIAL),'Bind burning renderer material')
    compiled=unreal.NiagaraService.compile_with_results(BULLET)
    require(compiled.success and compiled.error_count==0,str(compiled))
    require(unreal.NiagaraService.save_system(BULLET),'Save burning bullet')
    report['assets'].append({'path':BULLET,'source':source,'material':MATERIAL,'material_diagnostics':str(diagnostic),'compile':str(compiled),
        'renderers':{str(e):[str(r) for r in unreal.NiagaraEmitterService.list_renderers(BULLET,str(e))] for e in unreal.NiagaraService.summarize(BULLET).get_editor_property('emitter_names')}})
    report['success']=True
    (OUT/'vfx_authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'success':True,'assets':[a['path'] for a in report['assets']],'report':str(OUT/'vfx_authoring.json')}))
try:main()
except Exception:
    report={'success':False,'error':traceback.format_exc()}
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'vfx_authoring_error.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report))
