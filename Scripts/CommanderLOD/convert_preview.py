"""Convert captured linear render targets without changing their rendered content."""
import bpy,json
from pathlib import Path
ART=Path(__file__).resolve().parents[2]/'ArtSource/CommanderLOD_20261005'
scene=bpy.context.scene
scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
scene.view_settings.exposure=0;scene.view_settings.gamma=1
scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB';scene.render.image_settings.color_depth='8'
converted=[]
for folder in ART.glob('*/Review/UE'):
    for file in folder.glob('*.exr'):
        target=file.with_suffix('.png')
        if target.exists() and target.stat().st_mtime>=file.stat().st_mtime:continue
        try:image=bpy.data.images.load(str(file),check_existing=False)
        except RuntimeError:continue
        image.colorspace_settings.name='Linear Rec.709'
        image.save_render(str(target),scene=scene)
        bpy.data.images.remove(image);converted.append(str(target))
(ART/'Reports/preview_conversion.json').write_text(json.dumps(dict(success=True,converted=converted),indent=2),encoding='utf8')
print('PNG_CONVERTED',len(converted),flush=True)
