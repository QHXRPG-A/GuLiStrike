"""Compare native texture-source exports with the authored vertex atlas samples."""
import bpy,json,numpy as np
from pathlib import Path
ART=Path('D:/UE5.7/test1/ArtSource/CommanderLOD_20261005/BiZhiMao')
META=json.loads((ART/'vertex_metadata.json').read_text(encoding='utf8'))
report=dict(success=False,lods=[])
for lod in META['lods']:
    errors=[]
    for role in ['Position','Rotation']:
        filename=ART/'Reports/NativeTextureSource'/f'T_BiZhiMao_Vertex{role}_LOD{lod["lod"]}.{ "exr" if role=="Position" else "tga"}'
        im=bpy.data.images.load(str(filename),check_existing=False);im.colorspace_settings.name='Non-Color'
        raw=np.asarray(im.pixels[:],dtype=np.float32).reshape((lod['height'],lod['width'],4))
        if role=='Rotation':raw=raw[::-1] # PNG/TGA source row zero is top; Blender pixels start at bottom.
        maximum=0
        for sample in lod['samples']:
            x,y=sample['pixel'];expected=sample['delta_cm'] if role=='Position' else np.asarray(sample['rotation_rgba'])/255
            maximum=max(maximum,float(np.max(np.abs(raw[y,x,:len(expected)]-expected))))
        assert maximum<(.13 if role=='Position' else .0001),(lod['lod'],role,maximum)
        errors.append(dict(role=role,maximum_source_error=maximum));bpy.data.images.remove(im)
    report['lods'].append(dict(lod=lod['lod'],samples=len(lod['samples']),errors=errors))
report['success']=True
(ART/'Reports/vertex_texture_source_readback.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report),flush=True)
