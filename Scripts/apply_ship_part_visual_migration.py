"""Cold-editor startup stage for the socket-preserving ship part migration."""
import sys
import traceback
import unreal

sys.path.insert(0, 'D:/UE5.7/test1/Scripts')
import migrate_ship_part_visuals as migration

try:
    report = migration.migrate()
    migration.write('editor-bootstrap.json', {'success': True, 'report': report})
    unreal.log('SHIP_PART_VISUAL_MIGRATION_COMPLETE')
except Exception:
    error = traceback.format_exc()
    migration.write('editor-bootstrap.json', {'success': False, 'traceback': error})
    unreal.log_error(error)
