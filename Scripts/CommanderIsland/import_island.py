"""Replace only the commander Landscape, then apply its independent sea and views."""
from pathlib import Path
import json,unreal

REPO=Path(unreal.Paths.project_dir()).resolve()
# Reuse the established six-layer import/readback implementation.
source=(REPO/'Scripts/import_combat_island.py').read_text(encoding='utf-8').split('\nactions = {')[0]
source=source.replace('GuLiStrike_CombatIsland_2300m_v1','GuLiStrike_CommanderIsland_1800m_v1')
source=source.replace('CombatIsland_2300m_v1','CommanderIsland_1800m_v1').replace('CombatIsland.2300m.v1','CommanderIsland.1800m.v1')
source=source.replace('2300, 2300','1800, 1800').replace('CombatIsland/','CommanderIsland/')
exec(compile(source,'Scripts/import_combat_island.py','exec'),globals())
LAYOUT=json.loads((FILES/'GaeaExports/layout.json').read_text(encoding='utf-8'))

def setup():
    assert not LEVELS.is_in_play_in_editor()
    assert not dirty()['maps'] and not dirty()['assets'],dirty()
    assert LEVELS.load_level(MAP)
    value=world()
    old=[a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.Landscape)]
    assert len(old)==1 and old[0].get_actor_label()=='Landscape_2300'
    assert (FILES.parent/'Before/LVL_CommanderMassPrototype.umap').is_file()
    mode=value.get_world_settings().get_editor_property('default_game_mode')
    assert ACTORS.destroy_actor(old[0])
    mark(value)
    LIB.set_metadata_tag(value,'GuLi.CommanderIsland.HeightSHA256',MANIFEST['height_sha256'])
    LIB.set_metadata_tag(value,'GuLi.CommanderIsland.SourceRevision','GuLiStrike_CombatIsland_2300m_v1')
    LIB.set_metadata_tag(value,'GuLi.CommanderIsland.LayoutVersion','6')
    return {'success':True,'map':MAP,'game_mode_preserved':mode.get_path_name() if mode else 'Project default','replaced_landscape':'Landscape_2300'}

original_scene=scene
def scene():
    result=original_scene()
    start=next(a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.PlayerStart) and a.get_actor_label()=='CommanderPlayerStart_Red')
    h=unreal.LandscapeService.get_height_at_location(LABEL,0,69000)
    assert h.valid and h.height>0
    capsule=start.get_component_by_class(unreal.CapsuleComponent)
    start.set_actor_location(unreal.Vector(0,69000,h.height+capsule.get_scaled_capsule_half_height()+10),False,False)
    anchors=[]
    for team,sign in [('Red',1),('Blue',-1)]:
        for label,y in [('Factory',74000),('Assembly',69000)]:
            h=unreal.LandscapeService.get_height_at_location(LABEL,0,y*sign);assert h.valid and h.height>0
            a=ACTORS.spawn_actor_from_class(unreal.TargetPoint,unreal.Vector(0,y*sign,h.height))
            a.set_actor_label('CommanderIsland_'+team+'_'+label+'_Ground')
            a.set_folder_path('CommanderIsland/Review/Anchors');a.set_editor_property('tags',[OWNER])
            anchors.append({'label':a.get_actor_label(),'location_cm':list(a.get_actor_location().to_tuple())})
    result.update(anchors=anchors,player_start_cm=list(start.get_actor_location().to_tuple()))
    return result

def water_navigation():
    value=owned_world()
    data=json.loads((FILES/'water_navigation_boxes.json').read_text(encoding='utf-8'))
    assert data['water_cells_covered'] and data['outpost_cells_clear']
    tag='GuLi.CommanderIsland.Water'
    for a in ACTORS.get_all_level_actors():
        if tag in [str(t) for t in a.tags]:assert ACTORS.destroy_actor(a)
    boxes=[]
    with unreal.ScopedEditorTransaction('Commander island water navigation'):
        for i,b in enumerate(data['boxes']):
            v=ACTORS.spawn_actor_from_class(unreal.NavModifierVolume,unreal.Vector(*b['center_cm']))
            _,e=v.get_actor_bounds(False)
            assert e.x>0 and e.y>0 and e.z>0
            v.set_actor_scale3d(unreal.Vector(b['extent_cm'][0]/e.x,b['extent_cm'][1]/e.y,b['extent_cm'][2]/e.z))
            v.set_actor_label('CommanderIsland_WaterNav_%03d'%i)
            v.set_folder_path('CommanderIsland/Navigation/Water');v.set_editor_property('tags',[tag])
            v.set_editor_property('area_class',unreal.NavArea_Null)
            center,extent=v.get_actor_bounds(False)
            assert all(abs(actual-expected)<1 for actual,expected in zip(extent.to_tuple(),b['extent_cm']))
            boxes.append({'label':v.get_actor_label(),'center':list(center.to_tuple()),'extent':list(extent.to_tuple()),'area':v.get_editor_property('area_class').get_path_name()})
    return {'success':True,'map':value.get_path_name(),'boxes':boxes}

VIEWS={
 '01_topdown':{'location':[0,0,270000],'rotation':{'pitch':-90,'yaw':90,'roll':0},'fov':65},
 '02_overview':{'location':[0,-175000,160000],'target':[0,0,1300],'fov':65},
 '03_commander_SW':{'location':[-39000,-56000,31000],'target':[-39000,-35000,900],'fov':65},
 '04_east_canyon':{'location':[83000,-40000,42000],'target':[40000,5000,2600],'fov':65},
 '05_northwest_ridge':{'location':[-76000,65000,27000],'target':[-35000,38000,4500],'fov':60}}
if ISLAND_ACTION.startswith('view:'):
    result=view(ISLAND_ACTION.split(':',1)[1],ISLAND_CAPTURE)
else:
    result={'setup':setup,'materials':materials,'create_terrain':create_terrain,'import_height':import_height,
        'import_weights':import_weights,'scene':scene,'finalize_sea':finalize_sea,'water_navigation':water_navigation,
        'collision_checks':collision_checks,'verify':verify,'save':save,'reopen':reopen}[ISLAND_ACTION]()
unreal.MCPythonHelper.submit_result(json.dumps(result,ensure_ascii=False))
