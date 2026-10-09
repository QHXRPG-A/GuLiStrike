"""One-off actual-instance verification requested by the user; restore every override."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')

def run():
    world = next(w for w in unreal.ObjectIterator(unreal.World) if '/UEDPIE_2_' in w.get_path_name())
    registry = next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem) if s.get_world() == world)
    sub = next(s for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem) if s.get_world() == world)
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    def sample(mid):
        return next(a for a in actors if a.actor_has_tag('GuLi.ModelRuntimePaintSample') and a.actor_has_tag('ModelId=' + str(mid)) and a.actor_has_tag('Relation=own'))
    pioneer = sample(1001).get_components_by_class(unreal.MeshComponent)[0]
    wm = sample(1002).get_components_by_class(unreal.MeshComponent)[0]
    report = {'checks': [], 'errors': []}
    def check(name, passed, detail):
        report['checks'].append({'name': name, 'passed': bool(passed), 'actual': detail})
        if not passed: report['errors'].append(name)
    old_materials = [pioneer.get_material(i) for i in range(pioneer.get_num_materials())]
    old_wm = [wm.get_material(i) for i in range(wm.get_num_materials())]
    try:
        old = registry.get_scalar_parameter(pioneer, 1001, 'Root', 'Body', 'LineStrength')
        ok = registry.set_scalar_parameter(pioneer, 1001, 'Root', 'Body', 'LineStrength', .35)
        actual = registry.get_scalar_parameter(pioneer, 1001, 'Root', 'Body', 'LineStrength')
        check('MID scalar actual override', ok and actual is not None and abs(actual - .35) < 1e-5, {'before': old, 'after': actual})
        v = unreal.LinearColor(.08, .12, .16, 1)
        ok = registry.set_vector_parameter(wm, 1002, 'Root', 'M_WarMachine_Cel', 'InkColor', v)
        actual = registry.get_vector_parameter(wm, 1002, 'Root', 'M_WarMachine_Cel', 'InkColor')
        check('MID vector actual override', ok and actual is not None and all(abs(x-y) < 1e-5 for x,y in zip(actual.to_tuple(), v.to_tuple())), list(actual.to_tuple()) if actual else None)
        check('Team managed write denied', not registry.set_vector_parameter(wm, 1002, 'Root', '*', 'TeamPrimary', v), None)
        check('Unknown key write denied', not registry.set_scalar_parameter(wm, 1002, 'Root', 'M_WarMachine_Cel', 'UnregisteredParameter', .5), None)
        check('Wrong type read denied', registry.get_scalar_parameter(wm, 1002, 'Root', 'M_WarMachine_Cel', 'InkColor') is None, None)
    finally:
        for i, m in enumerate(old_materials): pioneer.set_material(i, m)
        for i, m in enumerate(old_wm): wm.set_material(i, m)
        sub.apply_local_team_colors()
    factory = sample(2006)
    for c in factory.get_components_by_class(unreal.MeshComponent):
        p = registry.get_vector_parameter(c, 2006, 'Root', '*', 'TeamPrimary')
        expected = unreal.GuLiModelRegistrySubsystem.parse_color if hasattr(unreal.GuLiModelRegistrySubsystem, 'parse_color') else None
        check('Factory delayed registered binding ' + c.get_name(), p is not None and p.r > .10 and p.b > .4, list(p.to_tuple()) if p else None)
    before = registry.get_vector_parameter(wm, 1002, 'Root', '*', 'TeamPrimary')
    wm.set_custom_primitive_data_vector4(8, unreal.Vector4(0, 0, 0, 0))
    # Verify the reset was actually visible; the following tool call checks tick recovery.
    reset = registry.get_vector_parameter(wm, 1002, 'Root', '*', 'TeamPrimary')
    check('CPD reset actual value read', reset is not None and reset.r == 0 and reset.b == 0, list(reset.to_tuple()) if reset else None)
    report['reset_actor'] = sample(1002).get_path_name()
    report['expected_primary'] = list(before.to_tuple()) if before else None
    report['success'] = not report['errors']
    (OUT / 'runtime-parameter-roundtrip.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    return report

unreal.MCPythonHelper.submit_result(json.dumps(run()))
