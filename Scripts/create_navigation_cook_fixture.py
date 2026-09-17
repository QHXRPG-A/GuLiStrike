"""Unattended UE Python commandlet: create a small, deliberately stale test map.

Uses Save As on ShipTest; never saves back to the source map. The normal Cook
must reject the copied volume's navigation asset because it belongs to ShipTest.
"""

import json
from pathlib import Path
import unreal

source = '/Game/Maps/LVL_ShipTest'
destination = '/Game/Tests/NavBakeCookGateStale_20260915'
root = Path(unreal.Paths.project_dir()).resolve()
source_file = root / 'Content/Maps/LVL_ShipTest.umap'
destination_file = root / 'Content/Tests/NavBakeCookGateStale_20260915.umap'
assert not destination_file.exists(), 'Refusing to replace an existing test map'
source_stamp = source_file.stat().st_mtime_ns
world = unreal.EditorLoadingAndSavingUtils.load_map(source)
assert world is not None
volumes = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiFlightNavigationVolume)
assert len(volumes) == 1
volumes[0].set_editor_property('Tags', ['GuLiNavCookGateTemporary20260915'])
assert unreal.EditorLoadingAndSavingUtils.save_map(world, destination)
assert source_file.stat().st_mtime_ns == source_stamp, 'Source map was unexpectedly modified'
report = {'source': source, 'destination': destination, 'fixture_file': str(destination_file),
          'source_unchanged': True, 'expected_cook_failure': 'StaleSourceWorld',
          'note': 'Temporary map; remove this exact generated asset after the Cook rejection test.'}
(root / 'TestResults/NavigationBake-20260915/cook-fixture.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('[GULI_NAV_COOK_FIXTURE] Created deliberately stale map: ' + destination)
