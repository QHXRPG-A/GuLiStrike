"""Scoped six-building client review; only transient PIE actors are changed."""
import gc
import json
import math
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/outputs/construction-vfx')
OUT.mkdir(parents=True, exist_ok=True)
XS = [-6500, -3800, -500, 3000, 5700, 9200]


def authority():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def client():
    return next(p for p in unreal.ObjectIterator(unreal.GuLiCommanderPlayerController)
                if 'Default__' not in p.get_name() and p.get_viewport_size()[0] > 0)


def samples(world):
    result = []
    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        life = a.get_component_by_class(unreal.GuLiBuildingLifecycleComponent)
        if life and any(abs(a.get_actor_location().x - x) < 1 for x in XS) and abs(a.get_actor_location().y - 76000) < 1:
            result.append(a)
    return sorted(result, key=lambda a: a.get_component_by_class(unreal.GuLiBuildingLifecycleComponent).get_state().definition_id)


def prepare(yaw_offset=0):
    w = authority()
    assert w, 'Start PIE first'
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.GuLiConstructionVehiclePawn):
        a.destroy_actor()
    for a in samples(w):
        a.destroy_actor()
    unreal.GameplayStatics.set_global_time_dilation(w, 1)
    for i, x in enumerate(XS, 1):
        a = unreal.GuLiConstructionReviewLibrary.spawn_sample(w, i, unreal.Vector(x, 76000, -1762), yaw_offset, False)
        assert a, 'Spawn failed: ' + str(i)
        unreal.GuLiConstructionReviewLibrary.advance_sample(a.get_component_by_class(unreal.GuLiBuildingLifecycleComponent), .55)


def capture(a, label):
    w = a.get_world()
    life = a.get_component_by_class(unreal.GuLiBuildingLifecycleComponent)
    shape = unreal.load_asset('/Game/GuLiStrike/Buildings/Construction/Shapes/DA_ConstructionShape_' + str(life.get_state().definition_id))
    ground = unreal.Vector(a.get_actor_location().x, 76000, -1762)
    height = shape.bounds.max.z
    camera = unreal.GuLiTeleportQALibrary.create_capture(w)
    try:
        c = camera.capture_component2d
        c.set_editor_property('capture_every_frame', False)
        c.set_editor_property('capture_on_movement', False)
        c.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        c.set_editor_property('fov_angle', 50)
        target = unreal.RenderingLibrary.create_render_target2d(w, 1400, 1000,
            unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(0, 0, 0, 1), False, False)
        c.set_editor_property('texture_target', target)
        distance = max(1600, height * 1.05)
        pos = ground + unreal.Vector(distance, -distance, distance * .95)
        camera.set_actor_location_and_rotation(pos,
            unreal.MathLibrary.find_look_at_rotation(pos, ground + unreal.Vector(0, 0, height * .4)), False, True)
        c.capture_scene()
        unreal.RenderingLibrary.export_render_target(w, target, str(OUT), label + '.png')
    finally:
        camera.destroy_actor()
    return str(OUT / (label + '.png'))


def snapshot(label='paused', images=True):
    pc = client()
    result = []
    for a in samples(pc.get_world()):
        life = a.get_component_by_class(unreal.GuLiBuildingLifecycleComponent)
        v = a.get_component_by_class(unreal.GuLiBuildingConstructionVisualComponent)
        assert v
        state = life.get_state()
        ground = unreal.Vector(a.get_actor_location().x, 76000, -1762)
        spans = []
        for step in range(16):
            angle = step * math.pi / 8
            s = v.find_nearest_construction_span(ground + unreal.Vector(2000 * math.cos(angle), 2000 * math.sin(angle), 20))
            spans.append(None if s is None else {'contour': s.contour, 'length': s.length, 'start': s.start})
        top = next((n for n in unreal.ObjectIterator(unreal.NiagaraComponent)
            if n.get_world() == pc.get_world() and n.get_asset()
            and n.get_asset().get_name() == 'NS_ConstructionTopLoop'
            and (n.get_world_location() - ground).length() < 10), None)
        entry = {'id': state.definition_id, 'phase': str(state.phase), 'yaw': a.get_actor_rotation().yaw,
            'display': v.get_displayed_progress(), 'active_builders': state.has_active_builders,
            'contours': [list(p.to_tuple()) for p in v.get_construction_contour()],
            'height': v.get_construction_height(), 'spans': spans, 'top_active': bool(top and top.is_active())}
        if images:
            entry['image'] = capture(a, f'type-{state.definition_id}-{label}')
        result.append(entry)
    (OUT / (label + '-readback.json')).write_text(json.dumps(result, indent=2), encoding='utf-8')
    return {'count': len(result), 'details': [{'id': r['id'], 'top': r['top_active'], 'spans': sum(s is not None for s in r['spans'])} for r in result]}


unreal.MCPythonHelper.submit_result(json.dumps({'loaded': True}))
gc.collect()
