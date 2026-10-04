"""Read-only inspection before the reference flame revision."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/WarMachineHover_20260930')
OUT.mkdir(parents=True, exist_ok=True)
BASE = '/Game/GuLiStrike/FX/WarMachineHover/'
SYSTEM = BASE + 'NS_WarMachineHoverPool'
ns, em, sp = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
r = {'map':world.get_path_name(), 'pie':unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),
     'summary':str(ns.summarize(SYSTEM)), 'emitters':{}, 'materials':{},
     'actors':[{'name':a.get_actor_label(), 'path':a.get_path_name()} for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if 'Hover' in a.get_actor_label()]}
for e in ns.list_emitters(SYSTEM):
    name=str(e.emitter_name)
    r['emitters'][name]={'properties':str(em.get_emitter_properties(SYSTEM,name)),
        'renderers':str(em.get_renderer_details(SYSTEM,name,0)),
        'modules':[str(x) for x in em.list_modules(SYSTEM,name)], 'hlsl':{}}
    for m in sp.list_scratch_modules(SYSTEM,name):
        for n in sp.list_nodes(SYSTEM,name,m):
            if str(n.node_type)=='CustomHlsl':
                r['emitters'][name]['hlsl'][str(m)]=sp.get_custom_hlsl_code(SYSTEM,name,m,str(n.node_id))
for name in ['M_WarMachineHoverJet','M_WarMachineHoverTrail']:
    r['materials'][name]=json.loads(unreal.MaterialNodeService.export_material_graph(BASE+name))
r['api']={name:getattr(unreal.NiagaraComponent,name).__doc__ for name in ['advance_simulation','set_force_solo','set_system_fixed_bounds']}
r['array_api']=[x for x in dir(unreal.NiagaraDataInterfaceArrayFunctionLibrary) if x.startswith('set_niagara')]
r['compile']=unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(unreal.load_asset(SYSTEM))
(OUT/'before.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'map':r['map'],'pie':r['pie'],'actors':r['actors'],'compile':r['compile'],'api':r['api'],'array_api':r['array_api']}))
