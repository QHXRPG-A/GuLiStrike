"""Save/reload model-catalog fixtures in the existing corresponding gameplay maps.

Live verification is a separate, explicitly authorized script. These editor-only
visual references use the current formal catalog; gameplay builders spawn through
the map's existing economy flow, never through Mass-only deployment points.
"""
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT=Path(r'D:/UE5.7/test1')
sys.path.insert(0,str(ROOT/'Scripts'))
from Models import model_catalog
TAG='GuLi.ModelRegistry.Review.20261008'
FOLDER='GuLiStrike/Review/ModelRegistry_20261008'
MAPS=['/Game/Maps/LVL_CommanderMassPrototype','/Game/Maps/LVL_GroundMech_Demo','/Game/Maps/LVL_ShipTest']
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
report={'maps':[],'errors':[],'native_compile':True,'new_table_import':True,
        'runtime_verified':'separate evidence in Artifacts/ModelRegistryRuntime20261008',
        'formal_model_assets_changed':True,'new_paint_selected':True}
original_map=editor.get_editor_world().get_path_name().split('.')[0]

def owned(a):return TAG in [str(t) for t in a.tags]
def make(label,cls,location,model_id=0,extra_tags=()):
    found=[a for a in actors.get_all_level_actors() if a.get_actor_label()==label]
    if len(found)>1 or (found and not owned(found[0])):raise RuntimeError('Unowned/duplicate fixture label: '+label)
    a=found[0] if found else actors.spawn_actor_from_class(cls,location)
    matches=a.get_class()==cls if isinstance(cls,unreal.Class) else isinstance(a,cls)
    if not matches:raise RuntimeError('Fixture class mismatch: '+label)
    a.modify();a.set_actor_label(label);a.set_folder_path(FOLDER)
    a.set_editor_property('tags',[TAG,*(['ModelId='+str(model_id)] if model_id else []),*extra_tags])
    a.set_actor_location_and_rotation(location,unreal.Rotator(),False,True)
    return a

def ground(x,y):
    ignored=[a for a in actors.get_all_level_actors() if owned(a)]
    hit=unreal.SystemLibrary.line_trace_single(editor.get_editor_world(),unreal.Vector(x,y,50000),unreal.Vector(x,y,-50000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignored,unreal.DrawDebugTrace.NONE)
    if not hit or not hit.to_tuple()[0]:raise RuntimeError('No existing ground at '+str((x,y)))
    return hit.to_tuple()[5]

def preview(label,mid,location,scale=1,visual_class=None,team='Unassigned',fit=False):
    definition=model_catalog.definition(mid)
    resource=unreal.load_object(None,definition['ResourcePath'])
    cls=visual_class or (resource if definition['ResourceType']=='PresentationClass' else
        unreal.SkeletalMeshActor if definition['ResourceType']=='SkeletalMesh' else unreal.StaticMeshActor)
    a=make('ModelRegistry_'+label,cls,location,mid,['ReviewActualTeam='+team,'FormalSourceOnly'])
    a.set_editor_property('is_editor_only_actor',True)
    if isinstance(a,unreal.Pawn):
        a.set_editor_property('auto_possess_player',unreal.AutoReceiveInput.DISABLED)
        a.set_editor_property('auto_possess_ai',unreal.AutoPossessAI.DISABLED)
    if not visual_class and definition['ResourceType']!='PresentationClass':
        if isinstance(a,unreal.SkeletalMeshActor):a.get_component_by_class(unreal.SkeletalMeshComponent).set_skeletal_mesh_asset(resource)
        else:a.static_mesh_component.set_static_mesh(resource)
    a.set_actor_scale3d(unreal.Vector(scale,scale,scale));a.set_actor_enable_collision(False)
    for c in a.get_components_by_class(unreal.PrimitiveComponent):
        c.modify();c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('can_ever_affect_navigation',False)
        if isinstance(c,unreal.StaticMeshComponent):c.set_evaluate_world_position_offset(False)
    if fit:
        center,extent=a.get_actor_bounds(False,True)
        a.set_actor_location(location+unreal.Vector(location.x-center.x,location.y-center.y,location.z-center.z+extent.z+2),False,True)
    return a

def observation(map_name,center,width,message):
    a=make('ModelRegistry_'+map_name+'_Camera',unreal.CameraActor,center+unreal.Vector(-width*.35,-width*.5,width*.7))
    a.set_editor_property('is_editor_only_actor',True)
    a.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(a.get_actor_location(),center),False)
    a.camera_component.set_editor_property('projection_mode',unreal.CameraProjectionMode.ORTHOGRAPHIC)
    a.camera_component.set_editor_property('ortho_width',float(width))
    note=make('ModelRegistry_'+map_name+'_Notes',unreal.TextRenderActor,center+unreal.Vector(0,width*.25,300))
    note.set_editor_property('is_editor_only_actor',True);note.text_render.set_text(message)
    note.text_render.set_world_size(90)

def readback(map_path,expected):
    if not levels.save_current_level():raise RuntimeError('Map save failed: '+map_path)
    if not unreal.EditorLoadingAndSavingUtils.load_map(map_path):raise RuntimeError('Saved map reload failed: '+map_path)
    current=[a for a in actors.get_all_level_actors() if owned(a)]
    if sorted(a.get_actor_label() for a in current)!=sorted(expected):raise RuntimeError('Owned entity set differs after map reload')
    values=[]
    for a in current:
        entry={'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'location_cm':list(a.get_actor_location().to_tuple()),
               'scale':list(a.get_actor_scale3d().to_tuple()),'tags':[str(t) for t in a.tags],
               'editor_only':bool(a.get_editor_property('is_editor_only_actor'))}
        if isinstance(a,unreal.GuLiCommanderDeploymentPoint):
            entry.update(team=str(a.team),unit_id=a.unit_type_id,count=a.rows*a.columns,automatic_fire=a.allow_automatic_fire,start_idle=a.start_idle)
            if a.unit_type_id!=4 or a.rows*a.columns!=1 or a.allow_automatic_fire or not a.start_idle:raise RuntimeError('Engineering deployment differs')
        else:
            if not entry['editor_only']:raise RuntimeError('Non-gameplay reference must be editor-only')
            entry['meshes']=[]
            for c in a.get_components_by_class(unreal.MeshComponent):
                mesh=c.get_skeletal_mesh_asset() if isinstance(c,unreal.SkeletalMeshComponent) else c.static_mesh if isinstance(c,unreal.StaticMeshComponent) else None
                if mesh:entry['meshes'].append({'component':c.get_name(),'mesh':mesh.get_path_name(),'relative_transform':str(c.get_relative_transform()),
                    'socket':str(c.get_attach_socket_name()),'materials':[c.get_material(i).get_path_name() if c.get_material(i) else '' for i in range(c.get_num_materials())]})
            mid=next((int(t.split('=',1)[1]) for t in entry['tags'] if t.startswith('ModelId=')),0)
            if mid and model_catalog.definition(mid)['ResourceType']!='PresentationClass':
                path=model_catalog.resource(mid)
                if not any(m['mesh']==path for m in entry['meshes']):raise RuntimeError('Exact ModelId source differs: '+a.get_actor_label())
        values.append(entry)
    settings=editor.get_editor_world().get_world_settings()
    mode=settings.get_editor_property('default_game_mode')
    starts=[{'label':a.get_actor_label(),'location_cm':list(a.get_actor_location().to_tuple())} for a in actors.get_all_level_actors() if isinstance(a,unreal.PlayerStart)]
    return {'map':map_path,'saved_and_reloaded':True,'actors':values,'game_mode':mode.get_path_name() if mode else 'project default',
            'existing_player_starts':starts,'native_bindings':'compiled ModelId fields and imported tables; current formal sources read back'}

try:
    if editor.get_game_world() or levels.is_in_play_in_editor():raise RuntimeError('Leave the active game session untouched')
    for path in MAPS:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):raise RuntimeError('Existing corresponding map missing: '+path)
    static=json.loads((ROOT/'Data/Models/static-source-review.json').read_text(encoding='utf8'))
    if static['errors'] or static['model_constant_errors']:raise RuntimeError('Finish source static review before scene preparation')
    if not levels.save_current_level():raise RuntimeError('Could not preserve current editor map before switching')
    for path in MAPS:
        if editor.get_editor_world().get_path_name().split('.')[0]!=path:
            if not levels.load_level(path):raise RuntimeError('Could not open corresponding map: '+path)
        expected=[]
        def keep(a):expected.append(a.get_actor_label());return a
        if path==MAPS[0]:
            units=json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf8'))
            for i,uid in enumerate((1,2,5,6)):
                row=next(r for r in units if r['Id']==uid)
                keep(preview('Mass_'+row['Name'],row['ModelId'],ground(-28000+i*4500,63500),row['PresentationScale'],fit=True))
            for i,key in enumerate(('ShieldGenerator','ManualOutpost','SSF_AirBase','ResourceFactory')):
                mid=model_catalog.model_id(key)
                keep(preview('Mass_'+key,mid,ground(-28000+i*5000,59500),.2 if key=='ResourceFactory' else 1,fit=True))
            for side,x,team in [('Blue',-24000,unreal.GuLiTeam.BLUE),('Red',-14000,unreal.GuLiTeam.RED)]:
                label='ModelRegistry_Engineer_'+side
                for old in actors.get_all_level_actors():
                    if old.get_actor_label()==label and owned(old) and isinstance(old,unreal.GuLiCommanderDeploymentPoint):
                        if not actors.destroy_actor(old):raise RuntimeError('Could not remove invalid actor-based unit from Mass deployment')
                keep(preview('Engineer_'+side,1004,ground(x,70500),team=side,fit=True))
            center=ground(-20500,61500)
            observation('Mass',center,24000,'MODEL ID / FORMAL LOCAL TEAM PAINT\n1/2: PIONEER + WM01; Q: SWEEPER; B: BI ZHI MAO\nSCENE UI: OWN BLUE / ENEMY RED / WORLD 20cm\nCOMPILED + TABLES IMPORTED; BUILDERS USE ECONOMY SPAWN')
        elif path==MAPS[1]:
            cls=unreal.load_class(None,'/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light_C')
            center=ground(17000,-6500)
            keep(preview('Ground_Assembly',4001,center,visual_class=cls,fit=True))
            observation('Ground',center,6000,'MODEL ID 4001 / GROUND PLAYER SEAT\nLEGS + ARMOR + SHOULDER + MACHINEGUN\nORIGINAL POSE / MATERIALS / SOCKETS\nTEAM PAINT DISABLED; COMPILED + TABLES IMPORTED')
        else:
            baseline=json.loads((ROOT/'Data/Models/migration-baseline-20261008.json').read_text(encoding='utf8'))
            center=unreal.Vector(-24000,-20000,8000)
            for i,ship in enumerate(baseline['ships']):
                mid=5001+i;cls=unreal.load_class(None,ship['class'])
                keep(preview('Ship_Hull_'+str(mid),mid,center+unreal.Vector(i*14000,0,0),visual_class=cls))
            for i,mid in enumerate(range(5101,5114)):
                keep(preview('Ship_Part_'+str(mid),mid,center+unreal.Vector((i%5)*3600,6000+(i//5)*3600,0),.2))
            observation('Ship',center+unreal.Vector(6500,6000,0),31000,'MODEL IDS 5001/5002 + 5101..5113\nCURRENT HULL/PART CLASSES + SOCKETS RETAINED\nORIGINAL PAINT; NO TEAM REPAINT FOR DIY SHIPS\nCOMPILED + TABLES IMPORTED')
        expected.extend(a.get_actor_label() for a in actors.get_all_level_actors() if owned(a) and a.get_actor_label().endswith(('_Camera','_Notes')))
        report['maps'].append(readback(path,expected))
except Exception:
    report['errors'].append(traceback.format_exc())
finally:
    if editor.get_editor_world().get_path_name().split('.')[0]!=original_map:
        if not levels.load_level(original_map):report['errors'].append('Could not restore original map '+original_map)
    report['original_map_restored']=editor.get_editor_world().get_path_name().split('.')[0]==original_map
(ROOT/'Data/Models/acceptance-scenes-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'maps_saved':len(report['maps']),'owned_entities':[len(m['actors']) for m in report['maps']],
                  'errors':report['errors'],'original_map_restored':report['original_map_restored']}))
