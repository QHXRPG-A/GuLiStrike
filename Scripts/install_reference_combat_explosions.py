"""Install only the approved destruction and wingman-bombing visuals."""
import unreal,json,traceback,sys,ast

import sys
from pathlib import Path
sys.path.insert(0,str(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/"Scripts/Vfx"))
from vfx_registry import vfx_id, resource as vfx_resource, scale as vfx_scale, require_id, visual_variant
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/StylePass_20260917'
FX='/Game/GuLiStrike/FX/CombatExplosions/'
LIB=unreal.EditorAssetLibrary
REPORT={'success':False}

def run():
    assert not unreal.WidgetService.is_pie_running()
    paths={k:FX+n for k,n in [('ground','NS_GroundDestruction_03'),('air','NS_WingmanDestruction_05'),('bomb','NS_WingmanBombardment_01')]}
    for p in paths.values():
        r=unreal.NiagaraService.compile_with_results(p);assert r.success and not r.errors,str(r)
    field=unreal.load_asset('/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundExplosion')
    # The visual-variant struct gained a field in this session. Reload its clean
    # serialized DataAsset after native re-instancing before reading or saving.
    dirty=set(str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    if field.get_package().get_name() not in dirty:
        unreal.EditorLoadingAndSavingUtils.reload_packages([field.get_package()],unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE)
        field=unreal.load_asset('/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundExplosion')
    # Load definitions only: running the full deployment would also touch weapons.
    tree=ast.parse((ROOT/'Scripts/deploy_wingman_attack_assets.py').read_text(encoding='utf8'))
    scope={'unreal':unreal,'Path':Path,'json':json,'copy':__import__('copy'),'re':__import__('re')}
    nodes=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.FunctionDef,ast.Assign))]
    exec(compile(ast.Module(body=nodes,type_ignores=[]),'<deployment contract>','exec'),scope)
    before=scope['variant_signature'](field.activation_variants[0]);REPORT['before']=before
    guard={k:str(field.get_editor_property(k)) for k in ['config_id','radius','timing','dissipation_seconds']}
    ini=ROOT/'Config/DefaultGame.ini';text=ini.read_text(encoding='utf8')
    marker='[/Script/GuLiStrike.GuLiUnitFeedbackSettings]';start=text.index(marker);end=text.find('\n[',start+1)
    if end<0:end=len(text)
    old=text[start:end];backup=OUT/'explosion_rollback.json'
    if not backup.exists():backup.write_text(json.dumps({'ini_section':old,'bomb_variant':before},ensure_ascii=False,indent=2),encoding='utf8')
    keep=[line for line in old.splitlines() if not any(line.startswith(s) for s in ['+ExplosionVfxIds=','+WingmanExplosionVfxIds=','GroundExplosionScaleParameter=','WingmanExplosionScaleParameter=','; Wingman deaths use'])]
    keep+=['; Approved reference explosions: range is passed once; component scale stays one.',
           'GroundExplosionScaleParameter=User.Area_Scale','WingmanExplosionScaleParameter=User.Area_Scale',
           '+ExplosionVfxIds='+str(vfx_id('GroundDestruction')),
           '+WingmanExplosionVfxIds='+str(vfx_id('WingmanDestruction'))]
    ini.write_text(text[:start]+'\n'.join(keep)+'\n'+text[end:],encoding='utf8')
    c=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiUnitFeedbackSettings'))
    c.set_editor_property('ExplosionVfxIds',[vfx_id('GroundDestruction')])
    c.set_editor_property('WingmanExplosionVfxIds',[vfx_id('WingmanDestruction')])
    c.set_editor_property('GroundExplosionScaleParameter','User.Area_Scale');c.set_editor_property('WingmanExplosionScaleParameter','User.Area_Scale')
    v=visual_variant('WingmanBombardment','User.Area_Scale',True,5.25)
    v.set_editor_property('random_yaw',True);v.set_editor_property('maximum_lifetime',5.25);v.set_editor_property('additional_layers',[])
    field.set_editor_property('activation_variants',[v]);assert guard=={k:str(field.get_editor_property(k)) for k in guard}
    scope['require_wingman_impact_contract'](field)
    assert LIB.save_loaded_asset(field,False)
    after=scope['variant_signature'](unreal.load_asset(field.get_path_name()).activation_variants[0])
    assert scope['variant_signatures_match'](after,scope['reference_variant_signature']())
    REPORT.update(success=True,after=after,settings={n:str(c.get_editor_property(n)) for n in ['ExplosionVfxIds','WingmanExplosionVfxIds','GroundExplosionScaleParameter','WingmanExplosionScaleParameter']},unchanged_gameplay=guard,paths=paths)

try:run()
except Exception:REPORT['error']=traceback.format_exc()
(OUT/'explosion_install.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(REPORT))
