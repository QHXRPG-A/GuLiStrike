"""Scale only ground destruction rendering by 0.6; keep simulation and air FX intact."""
import unreal,json,traceback,ast,re
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/StyleAdjust_20260917'
SYSTEM='/Game/GuLiStrike/FX/CombatExplosions/NS_GroundDestruction_03'
NS,EM,SP=unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService
LIB=unreal.EditorAssetLibrary
REPORT={'success':False,'system':SYSTEM,'visual_multiplier':0.6,'emitters':[]}
def need(v,msg):
    if not v:raise RuntimeError(msg)
    return v
tree=ast.parse((ROOT/'Scripts/build_wingman_toon_explosion.py').read_text(encoding='utf8'))
helper=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
exec(compile(ast.Module(body=[helper],type_ignores=[]),'<scratch graph authoring>','exec'))

def run():
    need(not unreal.WidgetService.is_pie_running(),'PIE active')
    system=unreal.load_asset(SYSTEM);need(system,'Ground system')
    need(LIB.get_metadata_tag(system,'GuLi.VFX.Owner')=='GuLi.ReferenceExplosions.20260917','Asset owner')
    backup='/Game/GuLiStrike/FX/CombatExplosions/Rollback/NS_GroundDestruction_03_Before060'
    if not LIB.does_asset_exist(backup):
        old=need(LIB.duplicate_asset(SYSTEM,backup),'Rollback copy');need(LIB.save_loaded_asset(old,False),'Save rollback')
    else:need(LIB.save_loaded_asset(unreal.load_asset(backup),False),'Save existing rollback')
    if LIB.get_metadata_tag(system,'GuLi.VFX.GroundVisualScale')=='0.6':
        REPORT.update(success=True,already_applied=True);return
    if not NS.parameter_exists(SYSTEM,'User.PresentationScale'):
        need(NS.add_user_parameter(SYSTEM,'User.PresentationScale','float','0.6'),'Visual scale parameter')
    else:need(NS.set_parameter(SYSTEM,'User.PresentationScale','0.6'),'Set visual scale')
    renderers=[]
    for o in unreal.ObjectIterator():
        path=unreal.Object.get_path_name(o)
        if path.startswith(SYSTEM+'.') and 'RendererProperties' in unreal.Object.get_class(o).get_name():renderers.append(o)
    for e in NS.list_emitters(SYSTEM):
        name=str(e.emitter_name);ep=EM.get_emitter_properties(SYSTEM,name)
        rs=[o for o in renderers if re.fullmatch(re.escape(name)+r'_\d+',o.get_outer().get_name())]
        need(len(rs)==1,'Expected one renderer for '+name)
        obj=rs[0];kind=obj.get_class().get_name();mesh='MeshRenderer' in kind
        need(mesh or 'SpriteRenderer' in kind,'Renderer type '+kind)
        bindings=[('PositionBinding','Position','PresentationPosition')]
        bindings.append(('ScaleBinding','Scale','PresentationScale') if mesh else ('SpriteSizeBinding','SpriteSize','PresentationSpriteSize'))
        size_type='Vector' if mesh else 'vec2';size_attr='Scale' if mesh else 'SpriteSize';out_attr='PresentationScale' if mesh else 'PresentationSpriteSize'
        ins=[('P','Position','Particles.Position'),('Size',size_type,'Particles.'+size_attr),('Factor','float','User.PresentationScale')]
        code='OutPos=P*Factor; OutSize=Size*Factor;'
        if not ep.local_space:
            ins.append(('Origin','Position','Engine.Owner.Position'));code='OutPos=Origin+(P-Origin)*Factor; OutSize=Size*Factor;'
        outs=[('OutPos','Position','Particles.PresentationPosition'),('OutSize',size_type,'Particles.'+out_attr)]
        for stage,label in [('ParticleSpawn','PresentationScaleSpawn'),('ParticleUpdate','PresentationScaleUpdate')]:
            names=[str(m.module_name) for m in EM.list_modules(SYSTEM,name)]
            need(not any(label in n for n in names),'Partial prior operation in '+name)
            scratch(name,stage,label,ins,outs,code)
        changed=[]
        for prop,old,new in bindings:
            binding=obj.get_editor_property(prop);before=binding.export_text()
            need('Particles.'+old in before,'Unexpected binding '+before)
            binding.import_text(before.replace(old,new));obj.set_editor_property(prop,binding)
            after=obj.get_editor_property(prop).export_text();need('Particles.'+new in after,'Binding readback')
            changed.append({'property':prop,'before':before,'after':after})
        REPORT['emitters'].append({'name':name,'local_space':ep.local_space,'bindings':changed})
    result=NS.compile_with_results(SYSTEM)
    REPORT['compile']={'success':result.success,'errors':list(result.errors),'warnings':list(result.warnings)}
    need(result.success and not result.errors,'Compile '+str(result))
    LIB.set_metadata_tag(system,'GuLi.VFX.GroundVisualScale','0.6')
    need(LIB.save_loaded_asset(system,False),'Save ground effect')
    REPORT.update(success=True,rollback=backup,component_scale_unchanged=True,area_parameter_unchanged=True,particle_counts_and_lifetimes_unchanged=True,air_asset_untouched=True)

try:run()
except Exception:REPORT['error']=traceback.format_exc()
(OUT/'ground_scale.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(REPORT))
