"""Read the actual saved production, textures and seven encoded movies."""
import bpy
import json
from pathlib import Path
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'Production_B_v1'
rig=bpy.data.objects['Armature']
manifest=json.loads((ROOT/'Source/source_manifest.json').read_text(encoding='utf-8'))
expected={b['name']:b['parent'] or None for b in manifest['bones']}
actual={b.name:b.parent.name if b.parent else None for b in rig.data.bones}
assert actual==expected
lods=[]
for lod in range(4):
    objects=[o for o in bpy.data.collections[f'03_LOD{lod}_Review'].objects if o.type=='MESH']
    values=[]
    for o in objects:
        o.data.calc_loop_triangles()
        assert len(o.data.materials)==1
        assert any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers)
        assert all(len([g for g in v.groups if g.weight>1e-5])==1 for v in o.data.vertices)
        values.append({'object':o.name,'triangles':len(o.data.loop_triangles),'sections':1,'UV_channels':len(o.data.uv_layers)})
    lods.append({'LOD':lod,'meshes':values,'total':sum(v['triangles'] for v in values)})
movies=[]
previews=json.loads((OUT/'animation_preview_report.json').read_text(encoding='utf-8'))
for record in previews['videos']:
    path=OUT/'AnimationPreviews'/record['file']
    clip=bpy.data.movieclips.load(str(path))
    assert tuple(clip.size)==(1080,1080),(record['file'],clip.size)
    assert clip.frame_duration==record['frames'],(record['file'],clip.frame_duration)
    movies.append({'file':record['file'],'decoded_size':list(clip.size),'decoded_frames':clip.frame_duration,
                   'file_bytes':path.stat().st_size})
textures=[]
for name in ('T_RSGMech_BaseColor','T_RSGMech_LineMask'):
    im=bpy.data.images[name]
    assert tuple(im.size)==(2048,2048)
    assert im.packed_file
    path=OUT/'Textures'/(name+'.png')
    textures.append({'file':path.name,'resolution':list(im.size),'file_bytes':path.stat().st_size,
                     'packed':True,'colorspace':im.colorspace_settings.name})
report={'Blender':bpy.app.version_string,'actual_production_file':bpy.data.filepath,
        'rig_bones':len(rig.data.bones),'exact_source_names_and_parents':True,
        'actions':sorted(a.name for a in bpy.data.actions if a.name.startswith('A_FPS_Mech')),
        'LODs':lods,'movies':movies,'textures':textures,
        'textures_rgba8_uncompressed_base_levels_MiB':32,
        'textures_rgba8_uncompressed_with_full_mips_MiB':32*4/3,
        'UE_GPU_compression_and_runtime_fps':'deferred to B-approved UE delivery',
        'B_approval':'pending'}
(OUT/'review_technical_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report),flush=True)
