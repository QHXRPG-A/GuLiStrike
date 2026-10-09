"""Real UE renders of formal paint, two palettes and four matching views per model."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008'
IMAGES = Path(globals().pop('GULI_CAPTURE_IMAGE_DIR', str(OUT / 'Images/FormalModels')))
REPORT = Path(globals().pop('GULI_CAPTURE_REPORT_PATH', str(OUT / 'formal-render-captures.json')))
IMAGES.mkdir(parents=True, exist_ok=True)


def run():
    sub = next(s for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem) if s.get_world() and 'UEDPIE_2' in s.get_world().get_path_name())
    world = sub.get_world()
    samples = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor) if a.actor_has_tag('GuLi.ModelRuntimePaintSample')]
    selected = globals().pop('GULI_CAPTURE_IDS', None)
    if selected:
        samples = [a for a in samples if any(a.actor_has_tag('ModelId=' + str(mid)) for mid in selected)]
    catalog = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8'))
    names = {r['Id']: r['Name'] for r in catalog}
    camera = unreal.GuLiTeleportQALibrary.create_capture(world)
    report = {'renders': [], 'errors': [], 'source': 'actual UE formal resources in PIE, static resting pose', 'render_target_gamma': 2.2}
    try:
        capture = camera.capture_component2d
        capture.capture_every_frame = False
        capture.capture_on_movement = False
        capture.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        capture.primitive_render_mode = unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST
        target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(.10, .13, .15, 1), False, False)
        target.target_gamma = 2.2
        capture.texture_target = target
        for actor in samples:
            tags = [str(t) for t in actor.tags]
            mid = int(next(t.split('=', 1)[1] for t in tags if t.startswith('ModelId=')))
            palette = 'blue' if 'Relation=own' in tags else 'red'
            center, extent = actor.get_actor_bounds(False, True)
            radius = max(extent.x, extent.y, extent.z, 1)
            capture.show_only_actors = [actor]
            capture.ortho_width = radius * 3.25
            views = [('hero', (1, 1, .8)), ('front', (0, 1, 0)), ('left', (-1, 0, 0)), ('back', (0, -1, 0))]
            if mid in (1001, 1002, 1006, 2006, 2008):
                views = [('hero', (1, 1, .8)), ('front', (1, 0, 0)), ('left', (0, -1, 0)), ('back', (-1, 0, 0))]
            for view, direction in views:
                capture.projection_type = unreal.CameraProjectionMode.ORTHOGRAPHIC
                position = center + unreal.Vector(*direction) * radius * 5
                camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
                capture.capture_scene()
                filename = f'{names[mid]}_{palette}_{view}.png'
                unreal.RenderingLibrary.export_render_target(world, target, str(IMAGES), filename)
                report['renders'].append({'model_id': mid, 'model': names[mid], 'palette': palette, 'view': view, 'file': str(IMAGES / filename)})
    finally:
        camera.destroy_actor()
    report['success'] = len(report['renders']) == len(samples) * 4 and bool(samples) and not report['errors']
    if REPORT.exists() and selected:
        previous = json.loads(REPORT.read_text(encoding='utf8'))
        previous['renders'] = [r for r in previous['renders'] if r['model_id'] not in selected] + report['renders']
        previous['errors'] = report['errors']
        previous['success'] = report['success'] and len(previous['renders']) == 120
        report = previous
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    return {'success': report['success'], 'render_count': len(report['renders']), 'errors': report['errors']}


unreal.MCPythonHelper.submit_result(json.dumps(run()))
