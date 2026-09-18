"""Patch existing project-owned scratch graphs without rebuilding their look."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
BASE=json.loads((ROOT/'TestResults/Scale020/fx-blueprint-baseline.json').read_text(encoding='utf-8'))['systems']
NS,EM,SP=unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService
FLIGHT='/Game/GuLiStrike/FX/WingmanFlight/NS_WingmanFlightTrail'
MISSILE='/Game/GuLiStrike/FX/CommanderWeapons/NS_WM01_MissileFlight'
report={'version':1,'nodes':[],'compiled':[],'saved':[]}

def require(result,why):
    if not result:
        raise RuntimeError(why)
    return result

def pin(path,emitter,module,node,direction,kind,name):
    if not any(str(p.pin_name)==name and str(p.direction).lower()==direction.lower()
               for p in SP.get_node_pins(path,emitter,module,node)):
        require(SP.add_pin(path,emitter,module,node,direction,kind,name).success,'add pin '+name)

def scalar_input(path,emitter,module,node):
    nodes=SP.list_nodes(path,emitter,module)
    reads=[n for n in nodes if 'MapGet' in str(n.node_type)]
    if reads:
        read=str(reads[0].node_id)
    else:
        created=SP.add_node(path,emitter,module,'MapGet')
        require(created.success,'create MapGet')
        read=str(created.node_id)
        src=next(n for n in nodes if str(n.node_type) in ('Input','NiagaraNodeInput') or str(n.node_type).endswith('NodeInput'))
        src_id=str(src.node_id)
        src_pin=next(str(p.pin_name) for p in SP.get_node_pins(path,emitter,module,src_id) if str(p.direction).lower()=='output')
        dest_pin=next(str(p.pin_name) for p in SP.get_node_pins(path,emitter,module,read) if str(p.direction).lower()=='input')
        require(SP.connect_pins(path,emitter,module,src_id,src_pin,read,dest_pin),'wire map')
    pin(path,emitter,module,read,'Output','float','User.VisualScale')
    pin(path,emitter,module,node,'Input','float','EffectScale')
    require(SP.connect_pins(path,emitter,module,read,'User.VisualScale',node,'EffectScale'),'wire effect scale')

def main():
    jobs=[]
    for path in (FLIGHT,MISSILE):
        for emitter,erow in BASE[path]['emitters'].items():
            for module,nodes in erow['scratch'].items():
                for node in nodes:
                    old=node['code']
                    target=old
                    if path==FLIGHT:
                        target=target.replace('1000*Throttle','200*Throttle').replace(',150)',',30)').replace('Width=110','Width=22')
                    elif module=='MissileParticleShape':
                        target=target.replace('Size=float2(110,110)','Size=float2(110,110)*EffectScale')
                        target=target.replace('Size=float2(85,85)','Size=float2(85,85)*EffectScale')
                        target=target.replace('Size=float2(95,95)','Size=float2(95,95)*EffectScale')
                        target=target.replace('*155.0;','*155.0*EffectScale;')
                        if emitter=='Core':
                            target+=' MeshScale=float3(EffectScale,EffectScale,EffectScale);'
                    elif module=='TrailWidth':
                        target=target.replace('Width=35;','Width=35*EffectScale;')
                    if old==target:
                        continue
                    current=SP.get_custom_hlsl_code(path,emitter,module,node['id'])
                    require(current in (old,target),'HLSL changed since baseline: '+path+'/'+emitter+'/'+module)
                    jobs.append((path,emitter,module,node['id'],old,target))
    # Finish all source preflight before mutation.
    if not any(str(p.parameter_name)=='User.VisualScale' for p in NS.list_parameters(MISSILE)):
        require(NS.add_user_parameter(MISSILE,'VisualScale','Float','1'),'create VisualScale')
    require(NS.set_parameter(MISSILE,'User.VisualScale','1'),'default art scale')
    for path,emitter,module,node,old,target in jobs:
        if path==MISSILE:
            scalar_input(path,emitter,module,node)
            if emitter=='Core' and module=='MissileParticleShape':
                pins=SP.get_node_pins(path,emitter,module,node)
                if not any(str(p.pin_name)=='MeshScale' for p in pins):
                    pin(path,emitter,module,node,'Output','Vector','MeshScale')
                    output=SP.add_module_output(path,emitter,module,'Particles.Scale','Vector')
                    require(output.success,'mesh scale output')
                    require(SP.connect_pins(path,emitter,module,node,'MeshScale',str(output.node_id),'Particles.Scale'),'wire mesh scale')
        require(SP.set_custom_hlsl_code(path,emitter,module,node,target),'write HLSL')
        report['nodes'].append({'system':path,'emitter':emitter,'module':module,'node':node,'old':old,'target':target})
    require(NS.set_parameter(FLIGHT,'User.NozzleSpacing','34.8'),'nozzle spacing')
    trail=unreal.load_asset(FLIGHT)
    trail.set_editor_property('FixedBounds',unreal.Box(min=unreal.Vector(-5000,-5000,-5000),max=unreal.Vector(5000,5000,5000)))
    for path in (FLIGHT,MISSILE):
        require(unreal.GuLiCombatEffectAuthoringLibrary.finalize_scratch_pins(unreal.load_asset(path)),'finalize '+path)
        require(SP.apply_changes(path),'apply graph '+path)
        result=NS.compile_with_results(path)
        report['compiled'].append({'system':path,'result':str(result)})
        require(result.success,'Niagara compile '+str(result))
        require(unreal.EditorAssetLibrary.save_asset(path,only_if_is_dirty=False),'save '+path)
        report['saved'].append(path)
    (ROOT/'TestResults/Scale020/niagara-migration-v1.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return {'nodes':len(jobs),'compiled':report['compiled'],'saved':report['saved']}

unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
