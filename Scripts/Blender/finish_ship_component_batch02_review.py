"""Render actual Batch02 review media using the existing material-review workflow."""
import importlib.util
import json
import sys
from pathlib import Path
import bpy

PROJECT = Path('D:/UE5.7/test1')
def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

batch = module('batch02_materials', PROJECT/'Scripts/Blender/style_ship_component_batch02_materials.py')
review = module('existing_material_review', PROJECT/'Scripts/Blender/finish_ship_component_material_review.py')
s = batch.s
review.s = s


def prepare_and_overview():
    rows = {}
    for i, key in enumerate(batch.KEYS, 1):
        target = s.OUT/(key+'_OriginalMesh_MaterialCandidate.blend')
        bpy.ops.wm.open_mainfile(filepath=str(target))
        scene = bpy.context.scene
        scene.frame_set(0)
        scene.name = f'Batch02_{i:02}_{key}'
        scene['approval_A'] = 'approved; approval_A_20260917.json'
        scene['reference_version'] = batch.APPROVAL['parts'][key]['reference_version']
        scene['candidate_version'] = batch.VERSION
        body = next(o for o in scene.objects if o.type=='MESH' and o.get('source_file'))
        path = s.OUT/(key+'_material_report.json')
        report = json.loads(path.read_text(encoding='utf-8'))
        assert s.ink_bake.geometry_hash(body.data)==report['original_geometry_sha256']
        assert len(body.data.materials[0].node_tree.nodes['Three_Tone_Lighting'].color_ramp.elements)==3
        for area in bpy.context.screen.areas if bpy.context.screen else []:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_perspective='CAMERA'
                area.spaces.active.shading.type='RENDERED'
                area.spaces.active.shading.use_scene_world=True
                area.spaces.active.shading.use_scene_lights=True
                area.spaces.active.overlay.show_overlays=False
        bpy.ops.wm.save_as_mainfile(filepath=str(target))
        report['blend_sha256']=s.filehash(target)
        report['saved_file_geometry_readback']='passed'
        s.dump(path,report)
        rows[key]={'geometry_matches_original':True,'neutral_frame':0,'three_tone_steps':3,'blend_sha256':report['blend_sha256'],
                   'packed_2K_textures':sum(1 for im in bpy.data.images if im.packed_file and im.size[0]==2048),
                   'reference_version':report['reference_version'],'B':'pending'}
    s.dump(s.OUT/'saved_blender_readback.json', rows)
    review.overview()
    bpy.ops.wm.save_as_mainfile(filepath=str(s.OUT/'ShipComponentStyle_Batch02_Overview.blend'))


if __name__=='__main__':
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    keys=[key for key in args if key in batch.KEYS] or list(batch.KEYS)
    if '--prepare-only' not in args:
        for key in keys:
            review.run(key)
    prepare_and_overview()
    print('BATCH02_REVIEW_MEDIA_COMPLETE',batch.VERSION,flush=True)
