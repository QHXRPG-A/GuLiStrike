"""Set and save eight miners plus eight builders per team in the existing map."""
import json
import math
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/engineering-navigation/load32-20260922'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_game_world() is None, 'End PIE before configuring the new load.'
world = editor.get_editor_world()
assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
definition = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
economy = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
assert definition and economy
assert str(definition.get_editor_property('map_package')) == MAP
assert definition.calculate_layout_hash() == definition.get_editor_property('layout_hash')
assert hasattr(economy, 'initial_mining_vehicles_per_team'), 'Compile and load the new native quantity configuration first.'
anchors = definition.get_editor_property('spawn_anchors')
clusters = definition.get_editor_property('clusters')
positions = []
for team, mirror, anchor_name in (('red', 1, 'red_assembly'), ('blue', -1, 'blue_assembly')):
    anchor = anchors.get_editor_property(anchor_name)
    for kind in ('miner', 'builder'):
        for index in range(8):
            x_offset = (1 + index % 4) * 1200 if kind == 'builder' else -(index % 4) * 1200
            x = anchor.x + mirror * x_offset
            y = anchor.y + mirror * (index // 4) * 1200
            hit = unreal.SystemLibrary.line_trace_single_for_objects(
                world, unreal.Vector(x, y, anchor.z + 100000), unreal.Vector(x, y, anchor.z - 100000),
                [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], False, [], unreal.DrawDebugTrace.NONE)
            assert hit is not None, f'No ground at {team}/{kind}/{index}'
            clearance = min(math.hypot(x - c.center.x, y - c.center.y) - c.obstacle_radius_centimeters - 130
                            for c in clusters)
            assert clearance > 0, f'Ore obstacle at {team}/{kind}/{index}'
            positions.append({'team': team, 'kind': kind, 'index': index, 'xy': [x, y],
                              'ground_hit': True, 'ore_clearance_cm': clearance})
minimum_spacing = min(math.dist(a['xy'], b['xy']) for i, a in enumerate(positions) for b in positions[i + 1:])
assert minimum_spacing >= 1200
before = {p: economy.get_editor_property(p) for p in
          ('initial_mining_vehicles_per_team', 'initial_construction_vehicles_per_team')}
with unreal.ScopedEditorTransaction('Set eight miners and eight builders per team'):
    economy.modify()
    for prop in before:
        economy.set_editor_property(prop, 8)
saved = unreal.EditorAssetLibrary.save_loaded_asset(economy, False)
assert saved
after = {p: economy.get_editor_property(p) for p in before}
assert all(v == 8 for v in after.values())
report = {'success': True, 'map': MAP, 'asset': economy.get_path_name(), 'saved': saved,
          'before': before, 'after': after, 'vehicles_per_team': 16, 'total_vehicles': 32,
          'minimum_spacing_cm': minimum_spacing, 'spawn_positions': positions, 'runtime_verified': False}
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'initial-vehicle-configuration.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
