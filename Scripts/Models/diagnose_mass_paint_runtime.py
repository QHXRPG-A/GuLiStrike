"""Read actual Mass batches and isolate their animated render, without changing assets."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')
world = next(w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_2_' in w.get_path_name())
report = {'world': world.get_path_name(), 'actors': [], 'materials': [], 'captures': []}
seen = set()
for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiCommanderPresentationActor):
    entry = {'actor': actor.get_path_name(), 'batches': []}
    for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
        if '_Team_' not in comp.get_name() or not comp.get_instance_count():
            continue
        poses = []
        for i in range(min(comp.get_instance_count(), 12)):
            pose = comp.get_instance_transform(i, True)
            poses.append({'index': i, 'position': list(pose.translation.to_tuple()), 'scale': list(pose.scale3d.to_tuple())})
        values = list(comp.get_editor_property('per_instance_sm_custom_data'))
        stride = comp.get_editor_property('num_custom_data_floats')
        entry['batches'].append({'name': comp.get_name(), 'mesh': comp.static_mesh.get_path_name(),
                                 'count': comp.get_instance_count(), 'stride': stride,
                                 'data0': values[:stride], 'poses': poses})
        mat = comp.get_material(0)
        parent = mat
        while isinstance(parent, unreal.MaterialInstanceConstant):
            parent = parent.get_editor_property('parent')
        if parent and parent.get_path_name() not in seen:
            seen.add(parent.get_path_name())
            nodes = []
            for node in unreal.ObjectIterator(unreal.MaterialExpression):
                if node.get_outer() != parent:
                    continue
                detail = {'name': node.get_name(), 'type': node.get_class().get_name()}
                if isinstance(node, unreal.MaterialExpressionMaterialFunctionCall):
                    f = node.get_editor_property('material_function')
                    detail['function'] = f.get_path_name() if f else None
                    detail['inputs'] = [str(v) for v in unreal.MaterialEditingLibrary.get_material_expression_input_names(node)]
                if isinstance(node, unreal.MaterialExpressionCustom):
                    detail['code'] = node.get_editor_property('code')
                nodes.append(detail)
            report['materials'].append({'path': parent.get_path_name(), 'nodes': nodes})
        # Capture one real, non-hidden instance for each type/team in its live batch.
        key = comp.get_name()
        if any(v['component'] == key for v in report['captures']):
            continue
        pose = next((p for p in poses if max(p['scale']) > 0 and abs(p['position'][0]) > 1), None)
        if not pose:
            continue
        center = unreal.Vector(*pose['position']) + unreal.Vector(0, 0, 160)
        camera = unreal.GuLiTeleportQALibrary.create_capture(world)
        position = center + unreal.Vector(0, -1200, 850)
        camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
        capture = camera.capture_component2d
        capture.capture_every_frame = False
        capture.capture_on_movement = False
        capture.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        capture.projection_type = unreal.CameraProjectionMode.ORTHOGRAPHIC
        capture.ortho_width = 1400
        capture.primitive_render_mode = unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST
        capture.show_only_component(comp)
        target = unreal.RenderingLibrary.create_render_target2d(world, 1000, 850, unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(.08, .08, .08, 1), False, False)
        capture.texture_target = target
        capture.capture_scene()
        filename = 'live_' + key + '.png'
        unreal.RenderingLibrary.export_render_target(world, target, str(OUT / 'Images'), filename)
        report['captures'].append({'component': key, 'instance': pose, 'file': str(OUT / 'Images' / filename)})
        camera.destroy_actor()
    report['actors'].append(entry)
(OUT / 'mass-visual-diagnostic.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'actors': len(report['actors']), 'captures': report['captures'], 'materials': len(report['materials'])}))
