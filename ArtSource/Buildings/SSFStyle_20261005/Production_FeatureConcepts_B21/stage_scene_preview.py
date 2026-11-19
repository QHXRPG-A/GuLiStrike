"""Apply the B21 component materials to the temporary B13 UE review actors."""

import hashlib
import json
import os
import struct
import time
import traceback
from collections import Counter, defaultdict
from pathlib import Path

import unreal


PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
REVIEW = PROJECT / 'ArtSource/Buildings/SSFStyle_20261005/Production_FeatureConcepts_B21'
TEMP = Path(os.environ['TEMP']) / 'GuLiStrike-SSF-B21-scene'
STATE = TEMP / 'B21_UE_stage_status.json'
TARGET = '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
PACKAGES = '/Game/Developers/SSF_B21_ComponentColorPreview_20261112'
OLD_PACKAGES = '/Game/Developers/SSF_B20_ScenePreview_20261112'
B13 = 'SSF B13 Preview · '
B21 = 'SSF B21 Preview · '
NAMES = dict(AirBase='空军基地', CloningCenter='克隆中心', CommandCenter='指挥中心',
             MilitaryFactory='军工厂', Reactor='反应堆', StrategyCenter='战略中心')
TEAMS = dict(Blue='蓝', Red='红')
SHOT = REVIEW / 'Previews' / 'SSF_B21_UE_RTS_Overview.png'
EDITOR = unreal.MaterialEditingLibrary


def loaded(name, package_path=PACKAGES):
    return unreal.find_object(None, f'{package_path}/{name}.{name}')


def asset(name, asset_type, factory_type, package_path=PACKAGES):
    result = loaded(name, package_path)
    if result is None:
        result = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, package_path, asset_type, factory_type())
    assert isinstance(result, asset_type), (name, result)
    return result


def linear(hex_color):
    srgb = [int(hex_color[i:i+2], 16) / 255. for i in (1, 3, 5)]
    return unreal.LinearColor(*(x / 12.92 if x <= .04045 else ((x + .055) / 1.055) ** 2.4 for x in srgb), 1.)


def material_graphs(report, revision='B21', package_path=PACKAGES):
    base = asset(f'M_SSF_ComponentColor_{revision}', unreal.Material, unreal.MaterialFactoryNew, package_path)
    base.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    base.set_editor_property('two_sided', True)
    EDITOR.delete_all_material_expressions(base)
    color = EDITOR.create_material_expression(base, unreal.MaterialExpressionVectorParameter, -720, -210)
    color.set_editor_property('parameter_name', 'ComponentColor')
    color.set_editor_property('default_value', linear(report['palette']['Ivory']))
    shade = EDITOR.create_material_expression(base, unreal.MaterialExpressionCustom, -320, 0)
    shade.set_editor_property('description', 'Component color, discrete light bands, and model edge ink')
    settings = report.get('ink', {})
    ink = linear(settings.get('color', report['palette']['Recess']))
    gain = settings.get('gain', 1.)
    fade_in, fade_out = settings.get('fade', (.32, 1.12))
    opacity = settings.get('opacity', .88)
    shade.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    dark, mid, light = report['light_bands']
    threshold_mid, threshold_light = report['thresholds']
    lx, ly, lz = report['light']
    shade.set_editor_property('code', f"""
float3 distance = float3(UV0.x, 1.0 - UV0.y, UV1.x);
float3 start = float3(1.0 - UV1.y, UV2.x, 1.0 - UV2.y);
float3 end = float3(UV3.x, 1.0 - UV3.y, UV4.x);
float3 normal = normalize(float3(1.0 - UV4.y, UV5.x, 1.0 - UV5.y));
float facing = dot(normal, float3({lx:.9f}, {ly:.9f}, {lz:.9f}));
float band = facing > {threshold_light} ? {light} : (facing > {threshold_mid} ? {mid} : {dark});
float3 width = max(distance, max(start, end));
float ink = {opacity} * (1.0 - smoothstep({fade_in}, {fade_out}, min(width.x, min(width.y, width.z))));
return lerp(Color.rgb * band, float3({gain * ink.r:.9f}, {gain * ink.g:.9f}, {gain * ink.b:.9f}), ink);
""")
    inputs = []
    for name in ['Color', *(f'UV{i}' for i in range(6))]:
        input_ = unreal.CustomInput()
        input_.set_editor_property('input_name', name)
        inputs.append(input_)
    shade.set_editor_property('inputs', inputs)
    assert EDITOR.connect_material_expressions(color, '', shade, 'Color')
    for i in range(6):
        coord = EDITOR.create_material_expression(base, unreal.MaterialExpressionTextureCoordinate, -720, 20 + 135 * i)
        coord.set_editor_property('coordinate_index', i)
        assert EDITOR.connect_material_expressions(coord, '', shade, f'UV{i}')
    assert EDITOR.connect_material_property(shade, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDITOR.recompile_material(base)
    assert not EDITOR.get_used_textures(base)

    mats = {}
    for role in ('Outline', 'Hidden'):
        mat = asset(f'M_SSF_{role}_{revision}', unreal.Material, unreal.MaterialFactoryNew, package_path)
        mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property('two_sided', role == 'Hidden')
        EDITOR.delete_all_material_expressions(mat)
        if role == 'Outline':
            value = EDITOR.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -320, 0)
            strength = settings.get('outline_strength', .94)
            value.set_editor_property('constant', unreal.LinearColor(strength * ink.r, strength * ink.g,
                                                                      strength * ink.b, 1.))
            assert EDITOR.connect_material_property(value, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        else:
            mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
            value = EDITOR.create_material_expression(mat, unreal.MaterialExpressionConstant, -320, 0)
            value.set_editor_property('r', 0.)
            assert EDITOR.connect_material_property(value, '', unreal.MaterialProperty.MP_OPACITY_MASK)
        EDITOR.recompile_material(mat)
        mats[role, 'Fixed'] = mat
    for role, choice in report['palette'].items():
        for team, color in (choice.items() if isinstance(choice, dict) else [('Fixed', choice)]):
            mat = asset(f'MI_SSF_{role}_{team}_{revision}', unreal.MaterialInstanceConstant,
                        unreal.MaterialInstanceConstantFactoryNew, package_path)
            EDITOR.set_material_instance_parent(mat, base)
            EDITOR.set_material_instance_vector_parameter_value(mat, 'ComponentColor', linear(color))
            actual = EDITOR.get_material_instance_vector_parameter_value(mat, 'ComponentColor')
            assert all(abs(a - b) < 1.e-7 for a, b in zip(actual.to_tuple(), linear(color).to_tuple()))
            mats[role, team] = mat
    return base, mats


def import_model(world, key, row, revision='B21', package_path=PACKAGES, source=TEMP):
    name = f'SM_{key}_{revision}'
    if mesh := loaded(name, package_path):
        return mesh
    filename = source / row['preview_fbx']
    assert hashlib.sha256(filename.read_bytes()).hexdigest() == row['preview_fbx_sha256']
    task = unreal.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = package_path
    task.destination_name = name
    task.automated = True
    task.save = False
    task.replace_existing = False
    task.factory = unreal.FbxFactory()
    options = unreal.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    options.import_as_skeletal = False
    options.import_materials = False
    options.import_textures = False
    options.import_animations = False
    settings = options.static_mesh_import_data
    settings.combine_meshes = True
    settings.generate_lightmap_u_vs = False
    settings.auto_generate_collision = False
    settings.build_nanite = False
    settings.remove_degenerates = False
    settings.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    task.options = options
    flag = 'Interchange.FeatureFlags.Import.Enable'
    previous = unreal.SystemLibrary.get_console_variable_int_value(flag)
    unreal.SystemLibrary.execute_console_command(world, f'{flag} 0')
    try:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    finally:
        unreal.SystemLibrary.execute_console_command(world, f'{flag} {previous}')
    mesh = next((obj for obj in task.get_objects() if isinstance(obj, unreal.StaticMesh)), None)
    assert mesh is not None, (name, list(task.imported_object_paths))
    return mesh


def slot_roles(mesh, revision='B21'):
    prefix = f'SSF_{revision}_'
    slots = [str(m.get_editor_property('imported_material_slot_name')) for m in mesh.static_materials]
    assert all(name.startswith(prefix) for name in slots), slots
    return [name.removeprefix(prefix) for name in slots]


def verify_source_mesh(mesh, row, sections):
    desc = mesh.get_static_mesh_description(0)
    assert desc.get_triangle_count() == row['triangles'], (mesh.get_name(), desc.get_triangle_count(), row['triangles'])
    assert desc.get_polygon_group_count() == len(sections) and set(sections) == set(row['roles'])
    counts = Counter()
    fingerprints = defaultdict(list)
    for i, role in enumerate(sections):
        group = unreal.PolygonGroupID()
        assert group.import_text(f'(IDValue={i})') and desc.is_polygon_group_valid(group)
        for poly in desc.get_polygon_group_polygons(group):
            instances = desc.get_polygon_vertex_instances(poly)
            assert len(instances) == 3
            corners = []
            for vertex in instances:
                values = [value or 0. for j in range(6) for value in desc.get_vertex_instance_uv(vertex, j).to_tuple()]
                corners.append(struct.pack('<12f', *values))
            fingerprints[role].append(b''.join(sorted(corners)))
            counts[role] += 1
    assert counts == row['role_triangles'], (mesh.get_name(), counts, row['role_triangles'])
    observed = {role: hashlib.sha256(b''.join(sorted(data))).hexdigest() for role, data in fingerprints.items()}
    assert observed == row['preview_uv_sha256'], (mesh.get_name(), sorted(role for role, sha in observed.items()
                                                                          if row['preview_uv_sha256'][role] != sha))


def mesh_readback(mesh, row, revision='B21'):
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    build = subsystem.get_lod_build_settings(mesh, 0)
    if not build.use_full_precision_u_vs:
        build.use_full_precision_u_vs = True
        subsystem.set_lod_build_settings(mesh, 0, build)
    assert subsystem.get_lod_build_settings(mesh, 0).use_full_precision_u_vs
    roles = slot_roles(mesh, revision)
    indices = [subsystem.get_lod_material_slot(mesh, 0, i) for i in range(len(roles))]
    assert sorted(indices) == list(range(len(roles))), (mesh.get_name(), indices)
    verify_source_mesh(mesh, row, [roles[i] for i in indices])
    assert mesh.get_num_triangles(0) == row['triangles']
    return roles


def actor_label(prefix, key, team):
    return prefix + TEAMS[team] + ' · ' + NAMES[key]


def current_models(world):
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor)
    result = {a.get_actor_label(): a for a in actors if a.get_actor_label().startswith(B13)}
    assert set(result) == {actor_label(B13, key, team) for key in NAMES for team in TEAMS}
    return result


def bounds(actor):
    return [v.to_tuple() for v in actor.get_actor_bounds(False)]


def apply_models(world, meshes, mats):
    actors = current_models(world)
    records = []
    pending = []
    for key, (mesh, roles) in meshes.items():
        for team in TEAMS:
            label = actor_label(B13, key, team)
            actor = actors[label]
            comp = actor.static_mesh_component
            old_mesh = comp.get_editor_property('static_mesh')
            old_mats = list(comp.get_materials())
            assert old_mesh.get_path_name() == f'{OLD_PACKAGES}/SM_{key}_{team}_B20.SM_{key}_{team}_B20'
            new_mats = [mats[role, team if (role, team) in mats else 'Fixed'] for role in roles]
            pending.append((actor, comp, label, key, team, mesh, old_mesh, new_mats, old_mats, bounds(actor)))
    changed = []
    try:
        for actor, comp, label, key, team, mesh, old_mesh, new_mats, old_mats, old_bounds in pending:
            changed.append((actor, comp, label, old_mesh, old_mats))
            assert comp.set_static_mesh(mesh)
            for index, material in enumerate(new_mats):
                comp.set_material(index, material)
            actor.set_actor_label(actor_label(B21, key, team), False)
            observed = bounds(actor)
            assert max(abs(a-b) for p,q in zip(old_bounds, observed) for a,b in zip(p,q)) < 1., (label, old_bounds, observed)
            assert list(comp.get_materials()) == new_mats and comp.get_editor_property('static_mesh') == mesh
            records.append(dict(actor=actor.get_actor_label(), mesh=mesh.get_path_name(), bounds_cm=observed,
                                materials=[mat.get_name() for mat in new_mats]))
    except Exception:
        for actor, comp, label, old_mesh, old_mats in reversed(changed):
            for i in range(len(old_mats), comp.get_num_materials()):
                comp.set_material(i, None)
            comp.set_static_mesh(old_mesh)
            for i, mat in enumerate(old_mats):
                comp.set_material(i, mat)
            actor.set_actor_label(label, False)
        raise
    return records


def queue_after_play(ed):
    utility = unreal.get_editor_subsystem(unreal.EditorUtilitySubsystem)
    event = utility.get_editor_property('on_end_pie')
    key = '_ssf_b21_on_end_pie'
    pending = getattr(unreal, key, None)
    if pending is not None and event.contains_callable(pending):
        return

    def on_end(_is_simulating):
        event.remove_callable(on_end)
        delattr(unreal, key)
        deadline = time.monotonic() + 15.

        def ready(_delta):
            if ed.get_game_world() and time.monotonic() < deadline:
                return
            unreal.unregister_slate_post_tick_callback(when_ready)
            try:
                start(publish=False)
            except Exception:
                STATE.write_text(json.dumps({'phase': 'error', 'traceback': traceback.format_exc()}), encoding='utf-8')

        when_ready = unreal.register_slate_post_tick_callback(ready)

    event.add_callable_unique(on_end)
    assert event.contains_callable(on_end)
    setattr(unreal, key, on_end)
    STATE.write_text(json.dumps({'phase': 'waiting_for_current_pie', 'map': TARGET}), encoding='utf-8')


def start(publish=True):
    report = json.loads((REVIEW / 'B21_Component_Color_Manifest.json').read_text(encoding='utf-8'))
    assert set(report['models']) == set(NAMES)
    ed = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = ed.get_editor_world()
    if game := ed.get_game_world():
        assert publish and game.get_name() == TARGET.rsplit('.', 1)[1]
        queue_after_play(ed)
        unreal.MCPythonHelper.submit_result(json.dumps({'phase': 'waiting_for_current_pie', 'map': TARGET,
                                                       'status_file': str(STATE)}))
        return
    assert world and world.get_path_name() == TARGET
    current_models(world)
    camera = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
                  if a.get_actor_label() == B13 + '战术机位')
    state = dict(phase='preparing', map=TARGET, source=report['blend_sha256'], models={}, actors=[])
    runtime = dict(busy=False, stage=None, capture=None, deadline=0.)
    meshes = {}
    was_alive = unreal.EditorPythonScripting.get_keep_python_script_alive()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)

    def write():
        STATE.write_text(json.dumps(state, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def tick(_):
        if runtime['busy']:
            return
        runtime['busy'] = True
        try:
            active_world = ed.get_editor_world()
            if ed.get_game_world() or not active_world or active_world != world:
                raise RuntimeError('The editor world changed during preview preparation')
            if runtime['stage'] is None:
                runtime['stage'] = material_graphs(report)
            elif len(meshes) < len(NAMES):
                key = next(key for key in NAMES if key not in meshes)
                row = report['models'][key]
                mesh = import_model(world, key, row)
                roles = mesh_readback(mesh, row)
                meshes[key] = mesh, roles
                state['models'][key] = dict(triangles=row['triangles'], roles=roles, six_uv_channels_verified=True)
                write()
            elif runtime['capture'] is None:
                master, mats = runtime['stage']
                state['actors'] = apply_models(world, meshes, mats)
                state['scene_verified'] = len(state['actors']) == 12
                state['material_texture_count'] = len(EDITOR.get_used_textures(master))
                camera.set_actor_label(B21 + '战术机位', False)
                state['phase'] = 'capture'
                write()
                SHOT.parent.mkdir(exist_ok=True)
                runtime['capture'] = unreal.AutomationLibrary.take_high_res_screenshot(
                    2400, 1350, str(SHOT), camera, delay=2., force_game_view=True)
                runtime['deadline'] = time.monotonic() + 40.
            elif runtime['capture'].is_task_done() and SHOT.is_file():
                with SHOT.open('rb') as stream:
                    header = stream.read(24)
                assert header[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>2I', header[16:24]) == (2400, 1350)
                state['phase'] = 'complete'
                state['screenshot'] = str(SHOT)
                state['shader_instructions'] = EDITOR.get_statistics(runtime['stage'][0]).num_pixel_shader_instructions
            elif time.monotonic() > runtime['deadline']:
                raise RuntimeError('The scene changed successfully but the screenshot timed out')
        except Exception:
            state['phase'] = 'error'
            state['traceback'] = traceback.format_exc()
        finally:
            runtime['busy'] = False
            if state['phase'] in ('complete', 'error'):
                write()
                unreal.unregister_slate_post_tick_callback(callback)
                unreal.EditorPythonScripting.set_keep_python_script_alive(was_alive)

    write()
    callback = unreal.register_slate_post_tick_callback(tick)
    if publish:
        unreal.MCPythonHelper.submit_result(json.dumps({'phase': 'preparing', 'status_file': str(STATE)}))


if __name__ == '__main__':
    start()
