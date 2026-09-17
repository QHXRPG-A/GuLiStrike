"""Read-only editor scene/asset audit for the Commander GPU diagnosis."""
import json
from collections import Counter
from pathlib import Path
import unreal

OUTPUT = Path('D:/UE5.7/test1/TestResults/CommanderGpuAnalysis-20260916/scene-audit.json')


def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return None


def path(obj):
    return obj.get_path_name() if obj else None


def scalar(value):
    return value if value is None or isinstance(value, (int, float, bool, str)) else str(value)


def properties(obj, names):
    return {name: scalar(prop(obj, name)) for name in names}


def mesh_info(mesh):
    return {'path': path(mesh), 'nanite_enabled': scalar(prop(prop(mesh, 'nanite_settings'), 'enabled')),
            'lods': [{'lod': i, 'triangles': mesh.get_num_triangles(i), 'sections': mesh.get_num_sections(i)}
                     for i in range(mesh.get_num_lods())],
            'materials': [path(x.material_interface) for x in mesh.static_materials]}


subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = subsystem.get_editor_world()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
classes = Counter()
meshes = {}
lights, primitives, effects = [], [], []
for actor in actors:
    classes[actor.get_class().get_name()] += 1
    for component in actor.get_components_by_class(unreal.PrimitiveComponent):
        record = {'actor': actor.get_actor_label(), 'actor_class': actor.get_class().get_name(),
                  'component': component.get_name(), 'class': component.get_class().get_name(),
                  **properties(component, ('cast_shadow', 'cast_dynamic_shadow', 'mobility', 'visible',
                     'hidden_in_game', 'visible_in_ray_tracing', 'bounds_scale', 'render_in_main_pass'))}
        if isinstance(component, unreal.StaticMeshComponent):
            mesh = prop(component, 'static_mesh')
            record['mesh'] = path(mesh)
            record['materials'] = [path(component.get_material(i)) for i in range(component.get_num_materials())]
            if isinstance(component, unreal.InstancedStaticMeshComponent):
                record['instances'] = component.get_instance_count()
            if mesh and path(mesh) not in meshes:
                meshes[path(mesh)] = mesh_info(mesh)
        primitives.append(record)
    for component in actor.get_components_by_class(unreal.LightComponentBase):
        lights.append({'actor': actor.get_actor_label(), 'class': component.get_class().get_name(),
                       **properties(component, ('mobility', 'intensity', 'cast_shadows', 'cast_dynamic_shadows',
                          'cast_volumetric_shadow', 'cast_cloud_shadows', 'real_time_capture', 'visible'))})
    if any(x in actor.get_class().get_name() for x in ('Fog', 'PostProcess', 'SkyAtmosphere', 'VolumetricCloud', 'Landscape')):
        effects.append({'actor': actor.get_actor_label(), 'class': actor.get_class().get_name()})

unit_mesh = unreal.load_asset('/Game/Commander/Units/SM_CommanderFourFRobot_Crowd')
if unit_mesh:
    meshes[path(unit_mesh)] = mesh_info(unit_mesh)
unit_material = unreal.load_asset('/Game/Commander/Units/M_CommanderFourFRobot_Crowd')
material = {'path': path(unit_material), **properties(unit_material, ('blend_mode', 'shading_model', 'two_sided', 'dithered_lod_transition'))}
if unit_material:
    material['textures'] = [path(t) for t in unreal.MaterialEditingLibrary.get_used_textures(unit_material)]
table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
table_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)) if table else []
cvars = {}
for key in ('r.AntiAliasingMethod', 'r.TSR.History.ScreenPercentage', 'r.TSR.AsyncCompute', 'r.Lumen.AsyncCompute',
            'r.Lumen.HardwareRayTracing', 'r.RayTracing', 'r.Shadow.Virtual.Enable', 'r.EarlyZPass',
            'r.ScreenPercentage', 'r.OcclusionCullParallelPrimFetch', 'r.Visibility.TaskSchedule'):
    try:
        cvars[key] = unreal.SystemLibrary.get_console_variable_float_value(key)
    except Exception as error:
        cvars[key] = str(error)
result = {'scope': 'current editor world; no PIE; scene contents are supporting evidence, not a capture-frame snapshot',
          'editor_world': path(world), 'game_world': path(subsystem.get_game_world()), 'actor_classes': dict(classes),
          'lights': lights, 'effects': effects, 'primitives': primitives, 'meshes': meshes,
          'unit_material': material, 'soldier_rows': table_rows, 'editor_cvars_not_capture_overrides': cvars,
          'dirty_maps': [path(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
          'dirty_content': [path(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
OUTPUT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'output': str(OUTPUT), 'actors': len(actors), 'primitives': len(primitives),
                  'lights': lights, 'unit_material': material, 'dirty_maps': result['dirty_maps'],
                  'dirty_content': result['dirty_content']}, ensure_ascii=False))
