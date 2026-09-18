"""Restore authored collision from the pre-import copies, without mesh edits."""
import json
import sys
import traceback
from pathlib import Path
import unreal

PROJECT=Path('D:/UE5.7/test1')
OUT=PROJECT/'ArtSource/Ships/ShipComponentStyle_20260917/UE_Integration'
sys.path.insert(0,str(PROJECT/'Scripts'))
from import_ship_component_styled_assets import restore_static_collision
from validate_saved_ship_component_assets import collision

def run():
    assert '-ShipComponentCollisionRestoreWorker' in unreal.SystemLibrary.get_command_line()
    source=json.loads((OUT/'formal_import_report.json').read_text(encoding='utf-8'))
    report={'passed':False,'parts':{}}
    for key,row in source['parts'].items():
        mesh=unreal.load_asset(row['mesh'])
        if not isinstance(mesh,unreal.StaticMesh):continue
        original=unreal.load_asset(source['rollback'][row['mesh']])
        before=collision(mesh)
        restore_static_collision(mesh,original)
        assert collision(mesh)==collision(original)
        assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
        report['parts'][key]={'before':before,'restored':collision(mesh),'exact_aggregate_geometry_equal':True}
    assert len(report['parts'])==6
    report['passed']=True
    return report

if __name__=='__main__':
    try:result=run()
    except Exception:result={'passed':False,'error':traceback.format_exc()};unreal.log_error(result['error'])
    (OUT/'collision_restoration.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.SystemLibrary.quit_editor()
