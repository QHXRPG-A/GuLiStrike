"""Project actual world objects near the reported image defect and isolate static previews."""
import json, math
from pathlib import Path
import unreal
world = next(w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_2_' in w.get_path_name())
camera = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D) if a.actor_has_tag('GuLi.ModelRuntimeCamera'))
capture = camera.capture_component2d
position = camera.get_actor_location()
rotation = camera.get_actor_rotation()
axes = [unreal.MathLibrary.get_right_vector(rotation), unreal.MathLibrary.get_up_vector(rotation), unreal.MathLibrary.get_forward_vector(rotation)]
focal = 1400 / (2 * math.tan(math.radians(capture.fov_angle / 2)))
actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
result = []
for actor in actors:
    comps = actor.get_components_by_class(unreal.MeshComponent)
    if not comps or actor == camera:
        continue
    center, extent = actor.get_actor_bounds(False, True)
    delta = center - position
    dot = [unreal.MathLibrary.dot_vector_vector(delta, axis) for axis in axes]
    if dot[2] <= 0:
        continue
    screen = [700 + dot[0] / dot[2] * focal, 500 - dot[1] / dot[2] * focal]
    if not (0 < screen[0] < 1400 and 0 < screen[1] < 1000):
        continue
    mesh_paths = []
    for comp in comps:
        resource = comp.static_mesh if isinstance(comp, unreal.StaticMeshComponent) else None
        if resource:
            mesh_paths.append(resource.get_path_name())
    result.append({'actor': actor.get_name(), 'class': actor.get_class().get_name(), 'center': list(center.to_tuple()), 'screen': screen, 'tags': [str(t) for t in actor.tags], 'meshes': mesh_paths})
result.sort(key=lambda v: (v['screen'][0] - 580)**2 + (v['screen'][1] - 240)**2)
capture.primitive_render_mode = unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST
capture.show_only_actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor)
capture.capture_scene()
unreal.RenderingLibrary.export_render_target(world, capture.texture_target, 'D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008/Images', 'static_only_blue.png')
capture.primitive_render_mode = unreal.SceneCapturePrimitiveRenderMode.PRM_RENDER_SCENE_PRIMITIVES
Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008/scene-artifact-locations.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'nearest_objects': result[:8]}))
