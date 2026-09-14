"""Read back the saved effect and actual Wingman components in the active PIE world."""
import json
from pathlib import Path
import traceback
import unreal

OUT = Path('D:/UE5.7/test1/TestResults/WingmanFlightVFX')
SYSTEM = '/Game/GuLiStrike/FX/WingmanFlight/NS_WingmanFlightTrail'
report = {'checks': {}, 'pawns': []}
try:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:
        raise RuntimeError('PIE world required')
    report['world'] = world.get_path_name()
    cls = unreal.load_class(None, '/Script/GuLiStrike.GuLiWingmanPawn')
    pawns = unreal.GameplayStatics.get_all_actors_of_class(world, cls)
    for pawn in pawns:
        trails = [c for c in pawn.get_components_by_class(unreal.NiagaraComponent) if c.get_name() == 'WingmanFlightTrail']
        entry = {'name': pawn.get_name(), 'hidden': pawn.get_editor_property('bHidden'), 'replicates': pawn.get_editor_property('bReplicates'),
                 'location': str(pawn.get_actor_location()), 'components': []}
        for comp in trails:
            entry['components'].append({'path':comp.get_path_name(), 'active':comp.is_active(),
                'asset':comp.get_asset().get_path_name() if comp.get_asset() else None,
                'parent':comp.get_attach_parent().get_name(), 'relative_location':str(comp.get_editor_property('relative_location')),
                'world_location':str(comp.get_world_location()),
                'tick_enabled':comp.is_component_tick_enabled()})
        report['pawns'].append(entry)
    visible = [p for p in report['pawns'] if not p['hidden']]
    report['checks']['has_flying_wingmen'] = len(visible) >= 25
    report['checks']['one_trail_per_visible_wingman'] = all(len(p['components']) == 1 for p in visible)
    report['checks']['all_visible_trails_active'] = all(p['components'] and p['components'][0]['active'] for p in visible)
    report['checks']['all_trails_use_saved_system'] = all(c['asset'].split('.')[0] == SYSTEM for p in visible for c in p['components'])
    report['checks']['attached_to_mesh'] = all(c['parent'] == 'WingmanMesh' for p in visible for c in p['components'])
    report['checks']['client_presentation_not_replicated'] = all(not p['replicates'] for p in report['pawns'])
    report['checks']['hidden_pawns_have_no_active_trails'] = all(not c['active'] for p in report['pawns'] if p['hidden'] for c in p['components'])
    report['success'] = all(report['checks'].values())
except Exception:
    report['success'] = False
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT/'pie.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
