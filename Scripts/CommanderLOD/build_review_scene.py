"""Persist editor-only three-tier samples in the existing commander prototype Map."""
import json,sys,math
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,REVIEW_PACKAGE,units
from common import require_unapproved_candidate
require_unapproved_candidate()
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world=editor.get_editor_world();assert not editor.get_game_world()
assert world.get_path_name()=='/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tag='CommanderLODReview20261005'
existing={actor.get_actor_label():actor for actor in actors.get_all_level_actors() if tag in [str(value) for value in actor.tags]}
native={row['name']:row for row in json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))['units']}
groups=[]

def spawn(cls,label,pos):
    actor=existing.get(label) or actors.spawn_actor_from_class(cls,pos)
    if not isinstance(actor,cls):raise RuntimeError('Review fixture must use its static reference class: '+label)
    actor.set_actor_label(label);actor.tags=[unreal.Name(tag)]
    actor.set_editor_property('is_editor_only_actor',True)
    actor.set_actor_location(pos,False,True)
    return actor

def configure(component,mesh,lod,materials=None):
    if isinstance(component,unreal.SkeletalMeshComponent):
        component.set_skeletal_mesh_asset(mesh);component.set_forced_lod(lod+1)
    else:component.set_static_mesh(mesh);component.set_forced_lod_model(lod+1)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    component.set_editor_property('can_ever_affect_navigation',False)
    if materials:
        for i,path in enumerate(materials):
            if path:component.set_material(i,unreal.load_asset(path))

for row_index,unit in enumerate(units()):
    name=unit['Name'];prefix=f'CommanderLOD_{name}'
    data=native.get(name)
    for lod in range(3):
        origin=unreal.Vector(48000+lod*6000,-72000+row_index*7500,600)
        group=dict(name=name,id=unit['Id'],lod=lod,actors=[],components=[],origin=list(origin.to_tuple()),scale=unit['PresentationScale'])
        if data and data['components']:
            mesh_map={item['source']:item['asset'] for item in data['meshes']}
            for index,part in enumerate(data['components']):
                cls=unreal.SkeletalMeshActor if part['mesh_type']=='SkeletalMesh' else unreal.StaticMeshActor
                actor=spawn(cls,f'{prefix}_LOD{lod}_{part["name"]}_{index}',origin+unreal.Vector(*part['location'])*unit['PresentationScale'])
                actor.set_actor_rotation(unreal.Rotator(*part['rotation']),True)
                actor.set_actor_scale3d(unreal.Vector(*part['scale'])*unit['PresentationScale'])
                component=actor.get_component_by_class(unreal.MeshComponent)
                configure(component,unreal.load_asset(mesh_map[part['mesh']]),lod,part['materials'])
                group['actors'].append(actor.get_actor_label());group['components'].append(component.get_path_name())
        else:
            path=REVIEW_PACKAGE+'/BiZhiMao/Meshes/SM_BiZhiMao_VAT' if name=='BiZhiMao' else data['meshes'][0]['asset']
            actor=spawn(unreal.StaticMeshActor,f'{prefix}_LOD{lod}',origin)
            actor.set_actor_rotation(unreal.Rotator(0,0,0),True)
            component=actor.static_mesh_component
            configure(component,unreal.load_asset(path),lod)
            actor.set_actor_scale3d(unreal.Vector(*([unit['PresentationScale']]*3)))
            component.set_evaluate_world_position_offset(False)
            group['actors'].append(actor.get_actor_label());group['components'].append(component.get_path_name())
        label=spawn(unreal.TextRenderActor,f'{prefix}_LOD{lod}_Label',origin+unreal.Vector(0,-2100,300))
        text=label.get_component_by_class(unreal.TextRenderComponent)
        text.set_text(f'{name} / ID{unit["Id"]} / LOD{lod}')
        text.set_world_size(90);text.set_text_render_color(unreal.Color(213,192,156,255))
        label.set_actor_rotation(unreal.Rotator(0,90,0),True)
        groups.append(group)
# Give the review bay a stable ground without changing the original lighting or gameplay.
floor_actor=spawn(unreal.StaticMeshActor,'CommanderLOD_ReviewFloor',unreal.Vector(54000,-53250,560))
floor_actor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
floor_actor.set_actor_scale3d(unreal.Vector(210,470,1))
floor_actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
floor_actor.static_mesh_component.set_editor_property('can_ever_affect_navigation',False)
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
# Read only the related saved sample components and their actual mesh references.
saved=[]
for group in groups:
    refs=[]
    for actor_label in group['actors']:
        actor=next(a for a in actors.get_all_level_actors() if a.get_actor_label()==actor_label)
        component=actor.get_component_by_class(unreal.MeshComponent)
        mesh=component.get_editor_property('skeletal_mesh_asset') if isinstance(component,unreal.SkeletalMeshComponent) else component.get_editor_property('static_mesh')
        refs.append(dict(actor=actor_label,component=component.get_path_name(),mesh=mesh.get_path_name(),editor_only=actor.get_editor_property('is_editor_only_actor')))
    saved.append(dict(**group,references=refs))
report=dict(success=True,map=world.get_path_name(),saved=True,tag=tag,groups=saved,native_compile='not_run',gameplay='not_run',approval='pending')
(ART/'Reports/review_scene.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,map=report['map'],groups=len(groups),sample_actors=sum(len(g['actors']) for g in groups))))
