"""Exercise ownership changes on an explicitly transient model through the normal registry."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')
report = {'changes': [], 'errors': []}
for sub in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem):
    w = sub.get_world()
    if not w or '/UEDPIE_' not in w.get_path_name(): continue
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    own = pc.player_state.get_team()
    enemy = unreal.GuLiTeam.BLUE if own == unreal.GuLiTeam.RED else unreal.GuLiTeam.RED
    actor = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor)
                 if a.actor_has_tag('GuLi.ModelRuntimePaintSample') and a.actor_has_tag('ModelId=2005') and a.actor_has_tag('Relation=own'))
    c = actor.get_components_by_class(unreal.MeshComponent)[0]
    outline = actor.get_component_by_class(unreal.GuLiTeamOutlineComponent)
    registry = next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem) if s.get_world() == w)
    try:
        for relation, team, expected in [('own', own, (.14412847,.37123767,.51491767)),
                                          ('enemy', enemy, (.368590, .051269, .086500)),
                                          ('unknown', unreal.GuLiTeam.UNASSIGNED, (.02518686,.03820437,.03560131))]:
            outline.set_outline_team(team)
            sub.register_model_component(c, 2005, 'Root', unreal.GuLiTeam.UNASSIGNED)
            sub.apply_local_team_colors()
            value = registry.get_vector_parameter(c, 2005, 'Root', '*', 'TeamPrimary')
            lamp = registry.get_scalar_parameter(c, 2005, 'Root', '*', 'TeamLightStrength')
            actual = list(value.to_tuple()) if value else None
            passed = value is not None and all(abs(x-y) < .005 for x,y in zip(actual, expected)) and ((lamp == 0) if relation == 'unknown' else abs(lamp-.35) < 1e-5)
            report['changes'].append({'world': w.get_path_name(), 'view_team': str(own), 'relation': relation,
                                       'owner_team': str(team), 'primary': actual, 'lamp': lamp, 'passed': passed})
            if not passed: report['errors'].append(w.get_name() + '/' + relation)
    finally:
        outline.set_outline_team(own)
        sub.register_model_component(c, 2005, 'Root', own)
        sub.apply_local_team_colors()
report['success'] = len(report['changes']) == 6 and not report['errors']
(OUT / 'ownership-color-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
