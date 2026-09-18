"""Produce actual material and pitch evidence using the established renderer."""
import importlib.util
import json
import sys
from pathlib import Path
import bpy

PROJECT=Path('D:/UE5.7/test1')
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod
batch=module('batch04_surface',PROJECT/'Scripts/Blender/style_ship_component_batch04_materials.py')
s=batch.s
review=module('existing_review',PROJECT/'Scripts/Blender/finish_ship_component_material_review.py')
review.s=s

if __name__=='__main__':
    if '--rebuild-flame' in sys.argv:
        batch.build('Incendiary_Bomb_LaunchBay')
    if '--rebuild-bottom' in sys.argv:
        batch.build('Bottom_Twin_Barrel_Turret')
    keys=[k for k in batch.KEYS if k in sys.argv] or list(batch.KEYS)
    for key in keys:
        review.run(key)
    review.prepare_saved()
    review.overview()
    for key in batch.KEYS:
        path=s.OUT/f'{key}_material_report.json'
        row=json.loads(path.read_text(encoding='utf-8'))
        row['user_B']='no_separate_visual_pass; direct_formal_release_authorized'
        params={'key':key,'art_revision':'1.0','scope':'original mesh surface treatment',
            'textures':{'BaseColor':{'file':f'T_SC_{key}_BaseColor_2K.png','color_space':'sRGB','UV_index_FBX':0},
                'ORM':{'file':f'T_SC_{key}_ORM_2K.png','color_space':'Non-Color'},
                'LineMask':{'file':f'T_SC_{key}_LineMask_2K.png','color_space':'Non-Color','UV_index_FBX':1}},
            'palette_sRGB':s.PALETTE,'toon_thresholds':row['toon_thresholds'],
            'toon_multipliers_linear':row['toon_multipliers_linear'],'line_strength':.85,
            'outline_width_m':row['outline_width_m'],'geometry_unchanged':True,
            'UE_shader':'rebuild three-tone surface and switchable outline overlay; preserve gameplay mesh'}
        s.dump(s.OUT/f'{key}_material_parameters.json',params)
        s.dump(path,row)
    print('BATCH04_ACTUAL_REVIEW_MEDIA_COMPLETE',flush=True)
