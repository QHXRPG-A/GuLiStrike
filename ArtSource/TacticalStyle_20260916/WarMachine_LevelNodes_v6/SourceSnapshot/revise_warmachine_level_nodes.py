"""v6: level only the two front ankle housings; retain down-sloping links.

Geometry checks only. User requested no assistant preview inspection.
"""
import bpy
import json
import sys
from pathlib import Path
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).parent))
import revise_warmachine_level_feet as level
review,design=level.review,level.design
review.SOURCE=review.ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelFeet_v5'
review.SOURCE_EDITABLE=review.SOURCE/'WarMachine_LevelFeet_Editable.blend'
review.OUT=review.ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6'
review.PREVIEWS=review.OUT/'Previews'
review.EDITABLE=review.OUT/'WarMachine_LevelNodes_Editable.blend'
review.REVIEW=review.OUT/'WarMachine_LevelNodes_Review.blend'
review.REPORT=json.loads((review.SOURCE/'WarMachine_handbuilt_report.json').read_text(encoding='utf8'))
review.SCALE=review.REPORT['source_scale']
review.OFFSET=Vector(review.REPORT['source_offset'])
review.REVISION_DESCRIPTION='v6: front ankle blocks/cuffs/lamps horizontal on level hub seats; v5 downward links, equal beams and all four coplanar discs unchanged'


def prepare():
    bpy.ops.wm.open_mainfile(filepath=str(review.SOURCE_EDITABLE))
    signatures={o.name:review.signature(o) for o in review.mesh_parts()}
    level.prepare()
    parts=review.mesh_parts()
    changed=[o.name for o in parts if review.signature(o)!=signatures[o.name]]
    allowed=('WM suspension toe ','WM toe amber optic_','WM suspension hub seat_')
    assert len(changed)==14 and all('_F_' in n and n.startswith(allowed) for n in changed),changed
    boxes=[]
    for obj in parts:
        if '_F_' in obj.name and obj.name.startswith('WM suspension toe '):
            # Read baked geometry normals, not object Euler values, because
            # earlier revisions baked their tilt into mesh vertex coordinates.
            obj.data.update()
            axis_error=max(1-max(abs(n) for n in p.normal) for p in obj.data.polygons)
            boxes.append({'object':obj.name,'mesh_normal_axis_error':axis_error})
    assert len(boxes)==8 and all(b['mesh_normal_axis_error']<1e-5 for b in boxes)
    data=json.loads((review.OUT/'geometry-check.json').read_text(encoding='utf8'))
    data['previous_version']='WarMachine_LevelFeet_v5'
    data['front_foot_move_from_source_m']=data.pop('front_foot_move_from_v4_m')
    data.update(changed_parts=changed,unchanged_parts_count=len(parts)-len(changed),
                horizontal_box_checks=boxes,front_ankle_block_tilt_degrees=0,
                tilted_clevis_parts_unchanged=True,all_hover_disc_geometry_and_positions_unchanged=True)
    review.write_json(review.OUT/'geometry-check.json',data)
    report=json.loads((review.OUT/'WarMachine_handbuilt_report.json').read_text(encoding='utf8'))
    report['front_ankle_block_tilt_degrees']=0
    review.write_json(review.OUT/'WarMachine_handbuilt_report.json',report)
    bpy.context.scene.name='WarMachine_LevelNodes_Editable'
    bpy.context.scene['front_ankle_block_tilt_degrees']=0.0
    bpy.ops.wm.save_as_mainfile(filepath=str(review.EDITABLE),check_existing=False)
    review.log('v6: eight front block/cuff meshes axis-aligned; 14 changed node/seat pieces, 265 unchanged parts')


def main():
    review.OUT.mkdir(parents=True,exist_ok=True);review.PREVIEWS.mkdir(exist_ok=True)
    stage=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'all'
    if stage in ('prepare','all'):
        prepare()
    if stage in ('render','all'):
        review.package_review('After')
    review.log('LevelNodes completed '+stage)


if __name__=='__main__':
    main()
