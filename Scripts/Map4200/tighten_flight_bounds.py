"""Refine this map's flight envelope after inspecting formation/recovery semantics."""
import json
import math
from pathlib import Path
import unreal


def main():
    out=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map4200/20260922/flight-bounds-refinement'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    dirty=list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    assert not dirty,[p.get_path_name() for p in dirty]
    assert (out/'backup/LVL_CommanderMassPrototype.umap').is_file()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    volume=next(a for a in actors if a.get_actor_label()=='FlightNav_LVL_CommanderMassPrototype')
    land=next(a for a in actors if isinstance(a,unreal.Landscape))
    land_origin,land_extent=land.get_actor_bounds(False)
    origin,extent=volume.get_actor_bounds(False)
    assert abs(extent.x-278300)<1 and abs(extent.y-278300)<1,'Refinement already applied; inspect rather than repeat.'
    formations=[]
    for name in ('DoubleRing','SwarmOrbit'):
        path='/Game/GuLiStrike/Ship/Abilities/Formations/DA_WingmanFormation_'+name
        asset=unreal.load_asset(path)
        inner=asset.get_editor_property('inner_ring_radius_centimeters')
        outer=asset.get_editor_property('outer_ring_radius_centimeters')
        inner_height=asset.get_editor_property('inner_ring_height_centimeters')
        outer_height=asset.get_editor_property('outer_ring_height_centimeters')
        swarm=asset.get_editor_property('swarm_orbit')
        swarm_radius=swarm.get_editor_property('outer_soft_radius_centimeters')
        swarm_height=swarm.get_editor_property('vertical_half_extent_centimeters')
        radius=asset.get_editor_property('agent_radius_centimeters')
        # Recovery is measured from the carrier, just like the ring radius.
        # The explicit recovery heading probe in FlightMovement is at least 20 m.
        probe=max(2000,asset.get_editor_property('obstacle_look_ahead_centimeters'),radius*2)
        recovery=asset.get_editor_property('recovery_distance_centimeters')
        radial=max(inner,outer,swarm_radius,recovery)
        # Include arbitrary carrier orientation when clipping the old lower Z.
        vertical=max(math.hypot(inner,inner_height),math.hypot(outer,outer_height),math.hypot(swarm_radius,swarm_height))
        formations.append({'asset':path,'radial_envelope_cm':radial,'rotated_formation_extent_cm':vertical,
                           'agent_radius_cm':radius,'recovery_probe_cm':probe,
                           'xy_padding_cm':radial+radius+probe,'lower_padding_cm':vertical+radius+probe})
    half=max(land_extent.x,land_extent.y)+max(f['xy_padding_cm'] for f in formations)
    low=math.floor((land_origin.z-land_extent.z-max(f['lower_padding_cm'] for f in formations))/1000)*1000
    high=origin.z+extent.z # Keep the established upper attack/flight allowance.
    scale=volume.get_actor_scale3d()
    data=volume.get_editor_property('navigation_data')
    def stats():
        return {name:len(data.get_editor_property(name)) for name in ('nodes','cells','portals','links')}
    before={'origin':list(origin.to_tuple()),'extent':list(extent.to_tuple()),'graph_counts':stats()}
    with unreal.ScopedEditorTransaction('Tighten Commander FlightNav to current formation envelopes'):
        volume.set_actor_location(unreal.Vector(0,0,(low+high)/2),False,False)
        volume.set_actor_scale3d(unreal.Vector(scale.x*half/extent.x,scale.y*half/extent.y,scale.z*(high-low)/2/extent.z))
    result=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(world,False)
    assert result.success,result.message
    assert result.ground_rebuilds==0 and result.flight_rebuilds==1
    resources=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    assert resources.success,str(resources.issues)
    actual_origin,actual_extent=volume.get_actor_bounds(False)
    payload={'success':True,'before':before,'after':{'origin':list(actual_origin.to_tuple()),'extent':list(actual_extent.to_tuple()),'graph_counts':stats()},
             'formations':formations,'landscape_min_z':land_origin.z-land_extent.z,'ground_rebuilds':result.ground_rebuilds,
             'flight_rebuilds':result.flight_rebuilds,'resource_layout_hash':resources.layout_hash,
             'navigation':[{'kind':e.kind,'status':e.status,'source_hash':e.source_hash,'object':e.object_path} for e in result.entries],
             'pie_run':False,'saved':False}
    (out/'refinement.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
    return payload


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
