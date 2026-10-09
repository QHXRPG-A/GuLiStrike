"""Actual unmodified Sweeper views for the new reference and Blender review."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/SweeperTeamColor_v1_20261008'
IMAGES = OUT / 'Sources'
IMAGES.mkdir(parents=True, exist_ok=True)


def run():
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    original = json.loads((OUT / 'original-source-readback.json').read_text(encoding='utf8'))
    mesh = unreal.load_object(None, original['model']['ResourcePath'])
    actor = editor.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,30000), transient=True)
    camera = editor.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0,0,30000), transient=True)
    files = []
    try:
        comp = actor.static_mesh_component
        comp.set_static_mesh(mesh)
        comp.set_evaluate_world_position_offset(False)
        comp.set_forced_lod_model(1)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        muzzle = comp.get_socket_transform('FX_Muzzle_Basic_01', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        front = (1,0,0) if abs(muzzle.x) >= abs(muzzle.y) else (0,1,0)
        if muzzle.x < 0 and front[0]: front = (-1,0,0)
        if muzzle.y < 0 and front[1]: front = (0,-1,0)
        center, extent = actor.get_actor_bounds(False, True)
        radius = max(extent.x, extent.y, extent.z, 1)
        capture = camera.capture_component2d
        capture.capture_every_frame = False
        capture.capture_on_movement = False
        capture.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        capture.projection_type = unreal.CameraProjectionMode.ORTHOGRAPHIC
        capture.ortho_width = radius * 3.1
        capture.primitive_render_mode = unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST
        capture.show_only_component(comp)
        target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 1000,
            unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(.1,.13,.15,1), False, False)
        target.target_gamma = 2.2
        capture.texture_target = target
        left = (-front[1], front[0], 0)
        views = [('hero', (front[0]+left[0], front[1]+left[1], .8)), ('front', front),
                 ('left', left), ('back', (-front[0],-front[1],0)), ('top',(0,0,1))]
        for name, direction in views:
            position = center + unreal.Vector(*direction) * radius * 5
            camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
            capture.capture_scene()
            unreal.RenderingLibrary.export_render_target(world,target,str(IMAGES),name+'.png')
            files.append(str(IMAGES/(name+'.png')))
        base = unreal.load_asset(original['materials'][0]['parent'])
        textures = [{'name': e.get_editor_property('texture').get_path_name(),
                     'source': str(e.get_editor_property('texture').get_editor_property('asset_import_data').get_first_filename())}
                    for e in unreal.ObjectIterator(unreal.MaterialExpressionTextureSample)
                    if e.get_outer()==base and e.get_editor_property('texture')]
        report = {'success': True, 'files': files, 'front_axis': list(front), 'left_axis': list(left),
                  'muzzle_component_cm': list(muzzle.to_tuple()), 'textures': textures,
                  'geometry_materials_unchanged': True, 'source': 'actual formal UE source, LOD0 rest pose, gamma 2.2'}
        (OUT/'source-views-readback.json').write_text(json.dumps(report,indent=2),encoding='utf8')
        return report
    finally:
        editor.destroy_actor(actor)
        editor.destroy_actor(camera)


unreal.MCPythonHelper.submit_result(json.dumps(run()))
