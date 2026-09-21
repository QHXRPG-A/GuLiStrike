"""Scoped installer / rollback, no weapon tables or gameplay assets rewritten.

Run through ue_exec.py. Default installs 'reference'; set WTE_INSTALL_VARIANT='old'
before exec to restore the original Big_17 + Shockwave visual contract.
"""
import json
import traceback
from pathlib import Path
import unreal

import sys
from pathlib import Path
sys.path.insert(0,str(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/"Scripts/Vfx"))
from vfx_registry import vfx_id, resource as vfx_resource, scale as vfx_scale, require_id, visual_variant

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/FX/WingmanGroundExplosion_Toon'
BASE='/Game/GuLiStrike/FX/WingmanWeapons/'
FIELD=BASE+'DA_WingmanGroundExplosion'

def signature(field):
    return [{'system':vfx_resource(v.vfx_id),'scale':vfx_scale(v.vfx_id)[0],'scale_parameter_name':str(v.scale_parameter_name),'random_yaw':v.random_yaw,
             'maximum_lifetime':v.maximum_lifetime,'additional_layers':[{'system':vfx_resource(l.vfx_id),'scale':vfx_scale(l.vfx_id)[0]} for l in v.additional_layers]}
            for v in field.activation_variants]

def install(variant):
    assert variant in ('old','toon','reference')
    assert not unreal.WidgetService.is_pie_running(),'Stop PIE before asset installation'
    lib=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    field=unreal.load_asset(FIELD)
    assert isinstance(field,unreal.GuLiSpellFieldDefinition)
    before=signature(field)
    originals=OUT/'original-visual.json'
    if not originals.exists():
        assert before[0]['system'].split('.')[0]==BASE+'NS_WingmanGroundExplosion_Big_17'
        originals.write_text(json.dumps(before,indent=2),encoding='utf8')
    immutable={n:str(field.get_editor_property(n)) for n in ['config_id','radius','timing','dissipation_seconds']}
    visual=unreal.GuLiEffectVisualVariant()
    path=BASE+('StylizedExplosion/NS_WingmanGroundExplosion_Toon' if variant=='toon' else 'NS_WingmanGroundExplosion_Big_17')
    if variant=='reference':path='/Game/GuLiStrike/FX/CombatExplosions/NS_WingmanBombardment_01'
    system=unreal.load_asset(path);assert system
    result=unreal.NiagaraService.compile_with_results(path)
    assert result.success and not result.errors,str(result)
    visual.set_editor_property('vfx_id',require_id(path,vfx_scale('WingmanBombardment')))
    visual.set_editor_property('random_yaw',True);visual.set_editor_property('maximum_lifetime',5.25 if variant=='reference' else (2. if variant=='toon' else 3.))
    visual.set_editor_property('scale_parameter_name','User.Area_Scale' if variant=='reference' else 'None')
    layers=[]
    if variant=='old':
        layer=unreal.GuLiEffectVisualLayer();layer.set_editor_property('vfx_id',require_id(BASE+'NS_WingmanGroundShockwave_Big_17',3.1));layers=[layer]
    visual.set_editor_property('additional_layers',layers)
    field.set_editor_property('activation_variants',[visual])
    assert immutable=={n:str(field.get_editor_property(n)) for n in immutable}
    assert lib.save_loaded_asset(field,only_if_is_dirty=True)
    after=signature(unreal.load_asset(FIELD))
    assert after[0]['system'].split('.')[0]==path and after[0]['scale']==5
    assert after[0]['scale_parameter_name']==('User.Area_Scale' if variant=='reference' else 'None')
    report={'success':True,'variant':variant,'before':before,'after':after,'unchanged_gameplay':immutable,'saved':[FIELD]}
    (OUT/('install-'+variant+'.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
    return report

try:r=install(globals().get('WTE_INSTALL_VARIANT','reference'))
except Exception:r={'success':False,'error':traceback.format_exc()}
unreal.MCPythonHelper.submit_result(json.dumps(r))
