"""Read full 30fps source bone poses for Blender production; no UE asset writes."""
import unreal, json, traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
O=R/'Production_B_v1/AnimationSource'
FLAG='-ControlRigMechAnimationReadWorker'

def main():
    assert FLAG in unreal.SystemLibrary.get_command_line()
    O.mkdir(parents=True,exist_ok=True)
    manifest=[]
    for name in ('Mech_Deploy','Mech_Idle','Mech_Walk'):
        path='/Game/Assets/ControlRig/Characters/Mech/Animations/'+name
        info=unreal.AnimSequenceService.get_anim_sequence_info(path)
        fps=float(info.frame_rate); duration=float(info.duration)
        names=None; frames=[]
        count=int(info.frame_count)
        for index in range(count):
            t=min(index/fps,duration)
            pose=unreal.AnimSequenceService.get_pose_at_time(path,t,False)
            ordered=sorted(pose,key=lambda b:int(b.bone_index))
            now_names=[str(b.bone_name) for b in ordered]
            if names is None: names=now_names
            assert now_names==names and len(names)==152
            values=[]
            for b in ordered:
                tr=b.transform; v=tr.translation; q=tr.rotation; s=tr.scale3d
                values.append([float(v.x),float(v.y),float(v.z),float(q.x),float(q.y),float(q.z),float(q.w),float(s.x),float(s.y),float(s.z)])
            frames.append({'time_s':t,'local_tqs':values})
        result={'purpose':'actual full source animation samples for Blender B preview',
                'name':name,'path':path,'fps':fps,'duration_s':duration,'frame_count':len(frames),
                'bone_names':names,'space':'UE local cm; xyzw quaternion; original scale preserved',
                'frames':frames,'ue_assets_saved':False}
        f=O/(name+'_FullPose.json')
        f.write_text(json.dumps(result,separators=(',',':')),encoding='utf-8')
        manifest.append({'name':name,'path':path,'file':f.name,'fps':fps,'duration_s':duration,'frame_count':len(frames),'bone_count':len(names),'bytes':f.stat().st_size})
        unreal.log('CONTROLRIG_FULL_ANIMATION '+name+' '+str(len(frames)))
    (O/'capture_manifest.json').write_text(json.dumps({'animations':manifest,'ue_assets_saved':False},indent=2),encoding='utf-8')
    unreal.log('CONTROLRIG_FULL_ANIMATION_CAPTURE_OK')

if __name__=='__main__':
    try: main()
    except Exception:
        O.mkdir(parents=True,exist_ok=True)
        (O/'capture_error.txt').write_text(traceback.format_exc(),encoding='utf-8')
        unreal.log_error(traceback.format_exc())
    finally:
        if FLAG in unreal.SystemLibrary.get_command_line(): unreal.SystemLibrary.quit_editor()
