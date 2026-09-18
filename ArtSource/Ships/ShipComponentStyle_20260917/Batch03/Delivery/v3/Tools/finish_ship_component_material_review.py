"""Complete real-render review media for unchanged original component meshes."""
import bpy
import importlib.util
import json
import math
import subprocess
import sys
from pathlib import Path
from mathutils import Vector

path=Path('D:/UE5.7/test1/Scripts/Blender/style_ship_component_original_materials.py')
spec=importlib.util.spec_from_file_location('material_study',path)
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)

def save_still(path):
    bpy.context.scene.render.image_settings.file_format='PNG'
    if hasattr(bpy.context.scene.render,'media_type'):bpy.context.scene.render.media_type='IMAGE'
    s.render(path)

def run(key):
    target=s.OUT/(key+'_OriginalMesh_MaterialCandidate.blend')
    bpy.ops.wm.open_mainfile(filepath=str(target))
    scene=bpy.context.scene
    body=next(o for o in scene.objects if o.type=='MESH' and o.get('source_file'))
    arm=next((o for o in scene.objects if o.type=='ARMATURE'),None)
    shell=next(o for o in scene.objects if o.get('render_helper_only'))
    camera=scene.camera;ground=bpy.data.objects['Studio_Ground'];mat=body.data.materials[0]
    points=[body.matrix_world@v.co for v in body.data.vertices]
    original_hash=s.ink_bake.geometry_hash(body.data)
    s.aim(camera,points,(1.1,-1.7,1.65))
    # Independent switches: mask controls interior ink; the collection controls
    # the exterior hull. The pictures are real renders, not filtered thumbnails.
    shell.hide_render=True;mat.node_tree.nodes['Line_Strength'].outputs[0].default_value=0
    save_still(s.PRE/(key+'_lines_off.png'))
    shell.hide_render=False;mat.node_tree.nodes['Line_Strength'].outputs[0].default_value=.85
    light=bpy.data.objects['Studio_Key'];rest=light.rotation_euler.copy()
    light.rotation_euler=Vector((-.65,-.25,-.70)).to_track_quat('-Z','Y').to_euler()
    save_still(s.PRE/(key+'_alternate_light.png'));light.rotation_euler=rest
    # A small render proves readability at a reduced image size.
    scene.render.resolution_percentage=25
    save_still(s.PRE/(key+'_small.png'));scene.render.resolution_percentage=100
    report_path=s.OUT/(key+'_material_report.json');report=json.loads(report_path.read_text(encoding='utf-8'))
    if arm:
        pivot=s.point(s.SNAP['parts'][key]['bones'][1]['local']['location'])
        sweep=[]
        for angle in (-15,0,30,75):
            s.pose(arm,angle,pivot)
            evaluated=body.evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=evaluated.to_mesh()
            sweep.extend([evaluated.matrix_world@v.co for v in mesh.vertices]);evaluated.to_mesh_clear()
        s.aim(camera,sweep,(1.25,-1.5,1.1))
        original_ground_z=ground.location.z
        lo,hi=s.bounds(sweep)
        ground.location.z=min(original_ground_z,lo.z-max(hi-lo)*.025)
        motion_ground_z=ground.location.z
        for angle in (-15,0,30,75):
            s.pose(arm,angle,pivot)
            save_still(s.PRE/(key+'_pitch_'+str(angle)+'.png'))
            scene.render.engine='BLENDER_WORKBENCH';ground.hide_render=True;shell.hide_render=True
            scene.display.shading.color_type='SINGLE';scene.display.shading.single_color=(.57,.61,.64)
            save_still(s.PRE/(key+'_gray_pitch_'+str(angle)+'.png'))
            scene.render.engine='BLENDER_EEVEE';ground.hide_render=False;shell.hide_render=False
        bone=arm.pose.bones['BarrelPitch'];bone.rotation_mode='QUATERNION'
        s.pose(arm,0,pivot);bone.keyframe_insert(data_path='rotation_quaternion',frame=0)
        for frame in range(1,98):
            angle=-15+45*(1-math.cos((frame-1)*2*math.pi/96))
            s.pose(arm,angle,pivot);bone.keyframe_insert(data_path='rotation_quaternion',frame=frame)
        arm.animation_data.action.name=key+'_Pitch_Review_Only'
        scene.frame_start=1;scene.frame_end=97;scene.render.fps=24
        scene.render.resolution_x=960;scene.render.resolution_y=720
        # This installed Blender build exposes FFmpeg RNA but has no video codec
        # support. Render real frames and encode with a temporary local runtime.
        sys.path.insert(0,'C:/Users/a/AppData/Local/Temp/CodexShipVideo')
        import imageio_ffmpeg
        frame_dir=s.PRE/'MotionFrames'/key;frame_dir.mkdir(parents=True,exist_ok=True)
        for frame in range(1,98):
            scene.frame_set(frame);save_still(frame_dir/('frame_%04d.png'%frame))
        video=s.PRE/(key+'_pitch_motion.mp4')
        result=subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-y','-framerate','24','-start_number','1',
                '-i',str(frame_dir/'frame_%04d.png'),'-c:v','libx264','-crf','17','-pix_fmt','yuv420p',
                '-movflags','+faststart',str(video)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        if result.returncode:raise RuntimeError(result.stderr.decode('utf-8',errors='replace'))
        assert video.stat().st_size>10000
        report['motion_video']=str((s.PRE/(key+'_pitch_motion.mp4')).relative_to(s.ROOT))
        report['demo_frames']=97;report['demo_fps']=24
        scene.frame_set(0);s.pose(arm,0,pivot)
        # The studio floor must not clip lowered barrels. Its display-only
        # animation restores the hero floor at rest and lowers it for the sweep.
        ground.location.z=original_ground_z;ground.keyframe_insert(data_path='location',index=2,frame=0)
        ground.location.z=motion_ground_z;ground.keyframe_insert(data_path='location',index=2,frame=1)
        ground.keyframe_insert(data_path='location',index=2,frame=97)
        scene.frame_set(0)
    scene.render.resolution_x=1440;scene.render.resolution_y=1080
    if hasattr(scene.render,'media_type'):scene.render.media_type='IMAGE'
    scene.render.image_settings.file_format='PNG'
    s.aim(camera,points,(1.1,-1.7,1.65))
    assert s.ink_bake.geometry_hash(body.data)==original_hash
    # Preserve the neutral study on opening. Demonstration action is available
    # at frames 1..97 and is excluded from the future neutral-pose FBX export.
    scene.frame_current=0
    s.select(body)
    bpy.ops.wm.save_as_mainfile(filepath=str(target))
    report['blend_sha256']=s.filehash(target)
    report['lighting']='EEVEE Diffuse -> Shader to RGB -> luminance -> constant 3-step ramp; scene lights and self-shadow'
    report['toon_thresholds']=[0,.18,.46]
    report['toon_multipliers_linear']=[[.43,.50,.60],[.72,.79,.84],[1,1,1]]
    report['line_strength']=.85
    report['stage']='review_B_ready'
    s.dump(report_path,report)
    print('MATERIAL_REVIEW_MEDIA_READY',key,flush=True)

def overview():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    empty=bpy.context.scene
    scenes=[]
    for key in s.SOURCE_FILES:
        target=s.OUT/(key+'_OriginalMesh_MaterialCandidate.blend')
        with bpy.data.libraries.load(str(target),link=False) as (src,dst):dst.scenes=src.scenes
        scenes.extend(dst.scenes)
    bpy.context.window.scene=scenes[0]
    bpy.data.scenes.remove(empty)
    for scene in scenes:
        scene['overview_note']='Each scene keeps its original scale, origin and rig. Use the Scene selector for the three components.'
        for obj in scene.objects:
            if obj.type=='ARMATURE':
                scene.frame_set(0)
    bpy.ops.wm.save_as_mainfile(filepath=str(s.OUT/'ShipComponentStyle_OriginalMeshes_Overview.blend'))
    print('MATERIAL_REVIEW_OVERVIEW_READY',flush=True)

def prepare_saved():
    rows={}
    for key in s.SOURCE_FILES:
        target=s.OUT/(key+'_OriginalMesh_MaterialCandidate.blend')
        bpy.ops.wm.open_mainfile(filepath=str(target));scene=bpy.context.scene;scene.frame_set(0)
        body=next(o for o in scene.objects if o.type=='MESH' and o.get('source_file'))
        report_path=s.OUT/(key+'_material_report.json');report=json.loads(report_path.read_text(encoding='utf-8'))
        assert s.ink_bake.geometry_hash(body.data)==report['original_geometry_sha256']
        assert len(body.data.materials[0].node_tree.nodes['Three_Tone_Lighting'].color_ramp.elements)==3
        for area in bpy.context.screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_perspective='CAMERA'
                area.spaces.active.shading.type='RENDERED'
                area.spaces.active.shading.use_scene_world=True;area.spaces.active.shading.use_scene_lights=True
        bpy.ops.wm.save_as_mainfile(filepath=str(target))
        report['blend_sha256']=s.filehash(target);report['saved_file_geometry_readback']='passed'
        s.dump(report_path,report)
        rows[key]={'saved_geometry_matches_original':True,'neutral_frame':scene.frame_current,'three_tone_steps':3,'blend_sha256':report['blend_sha256']}
    s.dump(s.OUT/'saved_blender_readback_v4.json',rows)
    print('SAVED_MATERIAL_GEOMETRY_READBACK_COMPLETE',flush=True)

if __name__=='__main__':
    if '--prepare' in sys.argv:prepare_saved()
    else:
        keys=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else list(s.SOURCE_FILES)
        for key in keys:run(key)
    overview()
    print('ALL_MATERIAL_REVIEW_MEDIA_COMPLETE',flush=True)
