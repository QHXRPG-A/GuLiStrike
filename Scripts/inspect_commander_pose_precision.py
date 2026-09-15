"""Read the smallest Commander unit's asset scale for network precision review.

Run through the source UE Python commandlet. Does not edit or save UE assets.
"""
import json
from pathlib import Path

import unreal


table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
row = next(item for item in rows if item['Name'] == 'DefaultSoldier')
mesh = unreal.load_asset('/Game/Commander/Units/SM_CommanderFourFRobot_Crowd')
bounds = mesh.get_bounds()
extent = bounds.box_extent
output = {
    'source_engine': 'D:/UnrealEngine-5.7',
    'mesh': mesh.get_path_name(),
    'row': row,
    'mesh_bounds_size_cm': [extent.x * 2, extent.y * 2, extent.z * 2],
    'mesh_bounds_origin_cm': [bounds.origin.x, bounds.origin.y, bounds.origin.z],
    'asset_edits': False,
}
path = Path('D:/UE5.7/test1/TestResults/CommanderMove10Hz/precision-review/asset-scale.json')
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(output, ensure_ascii=True))
