"""Render the real promoted mesh/material in matching views, including every LOD."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/SweeperTeamColor_v1_20261008'
IMAGES = OUT / 'UE/Images'
IMAGES.mkdir(parents=True, exist_ok=True)
lib = unreal.MaterialEditingLibrary


def linear(hex_color):
    def f(x):
        x = int(x, 16) / 255
        return x / 12.92 if x <= .04045 else ((x+.055)/1.055)**2.4
    return unreal.Vector4(*[f(hex_color[i:i+2]) for i in [1,3,5]], 1)


def run():
    formal = json.loads((OUT / 'formal-ue-import.json').read_text(encoding='utf8'))
    mesh = unreal.load_object(None, formal['mesh'])
    material = unreal.load_object(None, formal['material'])
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actor = editor.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,30000), transient=True)
    camera = editor.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0,0,30000), transient=True)
    files = []
    try:
        comp = actor.static_mesh_component
        comp.set_static_mesh(mesh)
        comp.set_evaluate_world_position_offset(False)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_custom_primitive_data_float(16, 1)
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
        flags = []
        for name in ['Fog', 'Atmosphere', 'VolumetricFog', 'Bloom', 'TemporalAA', 'AntiAliasing',
                     'MotionBlur', 'AmbientOcclusion', 'GlobalIllumination', 'LocalExposure']:
            flag = unreal.EngineShowFlagsSetting()
            flag.set_editor_property('show_flag_name', name)
            flag.set_editor_property('enabled', False)
            flags.append(flag)
        capture.set_editor_property('show_flag_settings', flags)
        settings = capture.get_editor_property('post_process_settings')
        settings.set_editor_property('override_auto_exposure_min_brightness', True)
        settings.set_editor_property('override_auto_exposure_max_brightness', True)
        settings.set_editor_property('auto_exposure_min_brightness', 1.)
        settings.set_editor_property('auto_exposure_max_brightness', 1.)
        capture.set_editor_property('post_process_settings', settings)
        target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 1000,
            unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(.1,.13,.15,1), False, False)
        target.target_gamma = 2.2
        capture.texture_target = target
        views = [('hero', (1,1,.8)), ('front',(1,0,0)), ('left',(0,1,0)), ('back',(-1,0,0)), ('top',(0,0,1))]
        for side, color in [('Original','#DD6038'), ('Blue','#6AA4BE'), ('Red','#A34053'), ('Unknown','#2C3735')]:
            comp.set_custom_primitive_data_vector4(8, linear(color))
            for lod in ([0] if side in ['Original','Unknown'] else [0,1,2]):
                comp.set_forced_lod_model(lod + 1)
                for name, direction in (views if lod==0 and side!='Unknown' else views[:1]):
                    position = center + unreal.Vector(*direction) * radius * 5
                    camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
                    capture.capture_scene()
                    filename = f'{side}_LOD{lod}_{name}.png'
                    unreal.RenderingLibrary.export_render_target(world, target, str(IMAGES), filename)
                    files.append({'side':side,'lod':lod,'view':name,'file':str((IMAGES/filename).relative_to(OUT))})
        # Temporary diagnostic material renders the approved mask on the same native UV0.
        # It is never saved or bound to the mesh asset.
        diagnostic = unreal.new_object(unreal.Material)
        diagnostic.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        mask = lib.create_material_expression(diagnostic, unreal.MaterialExpressionTextureSample, 0, 0)
        mask.set_editor_property('texture', unreal.load_object(None, formal['mask']))
        mask.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
        if not lib.connect_material_property(mask,'R',unreal.MaterialProperty.MP_EMISSIVE_COLOR):
            raise RuntimeError('Diagnostic mask connection failed.')
        lib.recompile_material(diagnostic)
        comp.set_material(0, diagnostic)
        comp.set_forced_lod_model(1)
        for name, direction in views:
            position = center + unreal.Vector(*direction) * radius * 5
            camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
            capture.capture_scene()
            filename = f'Mask_LOD0_{name}.png'
            unreal.RenderingLibrary.export_render_target(world, target, str(IMAGES), filename)
            files.append({'side':'Mask','lod':0,'view':name,'file':str((IMAGES/filename).relative_to(OUT))})
        report = {'success':True,'source':'real formal native UE mesh and material, orthographic rest pose, exposure locked, gamma 2.2; atmospheric/bloom/temporal effects disabled only in comparison capture',
                  'files':files,'runtime_auto_relation_test':'separate live two-client report',
                  'mesh_snapshot_exact':json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))==formal['mesh_snapshot_after']}
        (OUT/'UE/formal-view-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        return {'success':True,'rendered_views':len(files),'mesh_snapshot_exact':report['mesh_snapshot_exact']}
    finally:
        editor.destroy_actor(actor)
        editor.destroy_actor(camera)


unreal.MCPythonHelper.submit_result(json.dumps(run()))
