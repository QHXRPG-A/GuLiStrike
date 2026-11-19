"""Restore the SSF tactical formation with the B22 review meshes."""

import json
import math
import os
import runpy
import struct
import time
import traceback
from pathlib import Path

import unreal


PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
ROOT = PROJECT / 'ArtSource/Buildings/SSFStyle_20261005'
REVIEW = ROOT / 'Production_FeatureConcepts_B22'
TEMP = Path(os.environ['TEMP']) / 'GuLiStrike-SSF-B22-scene'
STATE = TEMP / 'B22_UE_stage_status.json'
PACKAGES = '/Game/Developers/SSF_B22_StructureInkPreview_20261113'
PREFIX = 'SSF B22 Preview · '


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def ground(world, x, y):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 30000.), unreal.Vector(x, y, -15000.),
                                                  unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
                                                  unreal.DrawDebugTrace.NONE, True)
    assert hit is not None
    row = hit.to_tuple()
    assert row[0] and row[9] and 'Landscape' in row[9].get_class().get_name(), (x, y, row)
    return float(row[5].z)


def camera_pose(rows, pitch, yaw, fov, aspect=16 / 9):
    corners = []
    for row in rows:
        (x, y, z), (a, b, c) = row['bounds_cm']
        corners.extend((x + dx*a, y + dy*b, z + dz*c) for dx in (-1, 1) for dy in (-1, 1) for dz in (-1, 1))
    center = [(.5 * (min(p[i] for p in corners) + max(p[i] for p in corners))) for i in range(3)]
    p, y = math.radians(pitch), math.radians(yaw)
    forward = (math.cos(p)*math.cos(y), math.cos(p)*math.sin(y), math.sin(p))
    right = (-math.sin(y), math.cos(y), 0.)
    up = (-math.sin(p)*math.cos(y), -math.sin(p)*math.sin(y), math.cos(p))

    def dot(point, axis):
        return sum((value - center[i]) * axis[i] for i, value in enumerate(point))

    horizontal = math.tan(math.radians(fov / 2)) * .86
    vertical = horizontal / aspect
    distance = max(max(abs(dot(point, right)) / horizontal, abs(dot(point, up)) / vertical) - dot(point, forward)
                   for point in corners)
    location = unreal.Vector(*(value - direction*distance for value, direction in zip(center, forward, strict=True)))
    rotation = unreal.Rotator(pitch=pitch, yaw=yaw, roll=0.)
    return location, rotation


def apply(world, models, meshes, materials, layout, names, teams):
    actor_service = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor)
                if a.get_actor_label().startswith(PREFIX)]
    by_label = {a.get_actor_label(): a for a in existing}
    assert len(existing) == len(by_label)
    plan = []
    for key, (mesh, roles) in meshes.items():
        model = models[key]
        box = mesh.get_bounding_box()
        lo, hi = box.min.to_tuple(), box.max.to_tuple()
        dimensions = [b - a for a, b in zip(lo, hi, strict=True)]
        expected = [100*(b-a) for a, b in zip(*model['preview_bounds_m'], strict=True)]
        ratios = [b / a for a, b in zip(sorted(dimensions), sorted(expected), strict=True)]
        scale = sum(ratios) / len(ratios)
        assert max(abs(a-b) for a, b in zip(sorted(expected), sorted(v*scale for v in dimensions), strict=True)) < 2.
        for team, label_team in teams.items():
            label = PREFIX + label_team + ' · ' + names[key]
            x, y, previous_ground = layout[f'{key}:{team}']
            z = ground(world, x, y)
            assert abs(z - previous_ground) < 80., (key, team, z, previous_ground)
            location = unreal.Vector(x - .5*(lo[0]+hi[0])*scale, y - .5*(lo[1]+hi[1])*scale, z+1. - lo[2]*scale)
            mats = [materials[role, team if (role, team) in materials else 'Fixed'] for role in roles]
            plan.append((key, team, label, location, scale, mesh, mats))
    assert set(by_label) <= {item[2] for item in plan}
    created = []
    records = []
    try:
        for key, team, label, location, scale, mesh, mats in plan:
            actor = by_label.get(label)
            if actor is None:
                actor = actor_service.spawn_actor_from_class(unreal.StaticMeshActor, location, transient=True)
                assert actor is not None
                created.append(actor)
                actor.set_actor_label(label, False)
                actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
                comp = actor.static_mesh_component
                assert comp.set_static_mesh(mesh)
                for i, mat in enumerate(mats):
                    comp.set_material(i, mat)
            comp = actor.static_mesh_component
            assert comp.get_editor_property('static_mesh') == mesh and list(comp.get_materials()) == mats
            actor.set_actor_enable_collision(False)
            comp.set_collision_profile_name('NoCollision')
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            comp.set_editor_property('cast_shadow', True)
            assert comp.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
            assert not actor.get_actor_enable_collision()
            assert comp.get_editor_property('cast_shadow')
            records.append(dict(key=key, team=team, actor=label, mesh=mesh.get_path_name(),
                                bounds_cm=[v.to_tuple() for v in actor.get_actor_bounds(False)],
                                materials=[mat.get_name() for mat in mats]))
    except Exception:
        if created:
            actor_service.destroy_actors(created)
        raise
    assert len(records) == 12
    return records


def main():
    helpers = runpy.run_path(str(ROOT / 'Production_FeatureConcepts_B21' / 'stage_scene_preview.py'))
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    assert world and not editor.get_game_world() and world.get_path_name() == helpers['TARGET']
    manifest_name = 'B22_Structure_Ink_Manifest.json'
    report = read(TEMP / manifest_name)
    assert report['blend_sha256'] == read(REVIEW / manifest_name)['blend_sha256']
    models = report['models']
    names, teams = helpers['NAMES'], helpers['TEAMS']
    assert set(models) == set(names)
    placement = read(REVIEW / 'UE_Preview_Layout.json')
    assert placement['map'] == world.get_path_name()
    layout = placement['ground_centers_cm']
    assert set(layout) == {f'{key}:{team}' for key in names for team in teams}
    actor_service = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    state = dict(phase='preparing', map=world.get_path_name(), blend_sha256=report['blend_sha256'], models={},
                 actors=[], screenshots=[])
    runtime = dict(busy=False, materials=None, camera=None, capture=None, finished_at=0.)
    meshes = {}
    was_alive = unreal.EditorPythonScripting.get_keep_python_script_alive()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)

    def write():
        STATE.write_text(json.dumps(state, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def capture(name, rows, pitch, yaw, fov, width, height):
        camera = runtime['camera']
        loc, rot = camera_pose(rows, pitch, yaw, fov, width / height)
        camera.set_actor_location(loc, False, False)
        camera.set_actor_rotation(rot, False)
        camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(fov)
        editor.set_level_viewport_camera_info(loc, rot)
        unreal.ViewportService.set_fov(fov)
        path = REVIEW / 'Previews' / f'SSF_B22_UE_{name}.png'
        runtime['capture'] = unreal.AutomationLibrary.take_high_res_screenshot(width, height, str(path), camera,
                                                                               delay=2., force_game_view=True)
        runtime['path'], runtime['size'] = path, (width, height)
        runtime['deadline'] = time.monotonic() + 40.

    def tick(_):
        if runtime['busy']:
            return
        runtime['busy'] = True
        try:
            assert editor.get_editor_world() == world and not editor.get_game_world(), 'The editor world changed'
            if runtime['materials'] is None:
                master, runtime['materials'] = helpers['material_graphs'](report, 'B22', PACKAGES)
                state['material_texture_count'] = len(unreal.MaterialEditingLibrary.get_used_textures(master))
            elif len(meshes) < len(models):
                key = next(key for key in models if key not in meshes)
                row = models[key]
                mesh = helpers['import_model'](world, key, row, 'B22', PACKAGES, TEMP)
                roles = helpers['mesh_readback'](mesh, row, 'B22')
                mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
                for section in range(len(roles)):
                    role = roles[mesh_editor.get_lod_material_slot(mesh, 0, section)]
                    if role in ('Outline', 'Hidden'):
                        if mesh_editor.is_section_cast_shadow_enabled(mesh, 0, section):
                            mesh_editor.enable_section_cast_shadow(mesh, False, 0, section)
                        assert not mesh_editor.is_section_cast_shadow_enabled(mesh, 0, section)
                meshes[key] = (mesh, roles)
                state['models'][key] = dict(triangles=row['triangles'], slots=roles, edge_data_verified=True)
                write()
            elif not state['actors']:
                state['actors'] = apply(world, models, meshes, runtime['materials'], layout, names, teams)
                cameras = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
                           if a.get_actor_label() == PREFIX + '战术机位']
                assert len(cameras) <= 1
                camera = cameras[0] if cameras else actor_service.spawn_actor_from_class(
                    unreal.CameraActor, unreal.Vector(), transient=True)
                assert camera
                camera.set_actor_label(PREFIX + '战术机位', False)
                runtime['camera'] = camera
                actor_service.set_selected_level_actors([])
                assert unreal.ViewportService.set_view_mode('lit')
                unreal.ViewportService.set_game_view(True)
                state['phase'] = 'capture'
                write()
                capture('RTS_Overview', state['actors'], -55., -90., 54., 2400, 1350)
            elif runtime['capture'] is not None:
                if time.monotonic() >= runtime['deadline']:
                    raise RuntimeError(f"Capture timed out for {runtime['path'].name}")
                path = runtime['path']
                if not runtime['capture'].is_task_done() or not path.is_file():
                    return
                with path.open('rb') as stream:
                    header = stream.read(24)
                assert header[:8] == b'\x89PNG\r\n\x1a\n'
                assert struct.unpack('>2I', header[16:24]) == runtime['size']
                state['screenshots'].append(str(path))
                runtime['capture'] = None
                runtime['finished_at'] = time.monotonic()
                write()
            elif time.monotonic() - runtime['finished_at'] >= .8:
                if len(state['screenshots']) == 1:
                    front = [row for row in state['actors'] if row['key'] in ('CommandCenter', 'MilitaryFactory')]
                    capture('Command_Factory', front, -37., -94., 45., 1920, 1080)
                else:
                    loc, rot = camera_pose(state['actors'], -55., -90., 54.)
                    camera = runtime['camera']
                    camera.set_actor_location(loc, False, False)
                    camera.set_actor_rotation(rot, False)
                    camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(54.)
                    editor.set_level_viewport_camera_info(loc, rot)
                    unreal.ViewportService.set_fov(54.)
                    state['phase'] = 'complete'
        except Exception:
            state['phase'] = 'error'
            state['traceback'] = traceback.format_exc()
        finally:
            runtime['busy'] = False
            if state['phase'] in ('complete', 'error'):
                write()
                unreal.unregister_slate_post_tick_callback(handle)
                unreal.EditorPythonScripting.set_keep_python_script_alive(was_alive)

    write()
    handle = unreal.register_slate_post_tick_callback(tick)
    unreal.MCPythonHelper.submit_result(json.dumps(dict(phase='preparing', status=str(STATE))))


if __name__ == '__main__':
    main()
