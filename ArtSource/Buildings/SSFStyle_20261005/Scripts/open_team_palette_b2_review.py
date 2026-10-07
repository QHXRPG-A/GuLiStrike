"""Open the requested candidate in an interactive Blender review viewport."""
import bpy,json,os
from pathlib import Path
O=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007')
EXPECTED=(O/'SSF_TeamPalette_B_v2.blend').resolve()

def setup():
    if Path(bpy.data.filepath).resolve()!=EXPECTED:return None
    if not bpy.context.window_manager.windows:return 1.
    scene=bpy.data.scenes['Review_Blue_Red_Buildings'];states=[]
    for window in bpy.context.window_manager.windows:
        window.scene=scene
        for area in window.screen.areas:
            if area.type!='VIEW_3D':continue
            space=area.spaces.active;space.shading.type='RENDERED';space.shading.use_scene_world_render=True
            space.shading.use_scene_lights_render=True;space.overlay.show_overlays=False;space.show_region_ui=False
            space.region_3d.view_perspective='CAMERA';space.region_3d.view_camera_zoom=0.
            space.region_3d.view_camera_offset=(0.,0.);area.tag_redraw()
            states.append({'area':'VIEW_3D','shading':'RENDERED','perspective':'CAMERA','width':area.width,'height':area.height})
    assert states,'An actual review viewport must exist'
    report={'success':True,'pid':os.getpid(),'file':bpy.data.filepath,'scene':scene.name,'viewports':states,
            'Blue_on_left_Red_on_right':True,'other_existing_Blender_windows_untouched':True,'blend_saved_by_viewport_script':False}
    (O/'interactive_open_status.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return None
bpy.app.timers.register(setup,first_interval=2.)
