"""Standard import hook for the six locomotion clips and two turn derivatives."""
import json
import shutil
import subprocess
from pathlib import Path
import unreal

def run_authoring(root,*args):
    command=[shutil.which('python'),*map(str,args)]
    result=subprocess.run(command,cwd=root,capture_output=True,encoding='utf8',errors='replace',
                          creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if result.returncode: raise RuntimeError(result.stdout+'\n'+result.stderr)
    return result.stdout

def sync_heavy_gait_assets(root):
    out=root/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
    run_authoring(root,root/'Scripts/GroundMech/MechaDestroyer/bake_heavy_gait_data.py')
    data=json.loads((out/'heavy_gait_animation_data.json').read_text())
    lib=unreal.EditorAssetLibrary; folder='/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations'
    mesh=unreal.load_asset('/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2')
    idle=unreal.load_asset(folder+'/Anim_Mecha_Idle'); changed=[]
    for name,d in data['clips'].items():
        path=folder+'/'+name
        asset=unreal.load_asset(path) if lib.does_asset_exist(path) else None
        signature=d.get('signature',data['signature'])
        if asset and lib.get_metadata_tag(asset,'GroundHeavyBake')==signature: continue
        if asset is None:
            factory=unreal.AnimSequenceFactory(); factory.set_editor_property('target_skeleton',mesh.skeleton)
            factory.set_editor_property('preview_skeletal_mesh',mesh)
            asset=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,unreal.AnimSequence,factory)
        assert asset and asset.get_skeleton()==mesh.skeleton,path
        ctl=asset.controller; ctl.open_bracket('Excel heavy gait source-track authoring',False)
        try:
            ctl.set_frame_rate(unreal.FrameRate(30,1),False)
            ctl.set_number_of_frames(unreal.FrameNumber(len(d['frames'])-1),False)
            existing=set(map(str,unreal.AnimationLibrary.get_animation_track_names(asset)))
            for i,bone in enumerate(data['bones']):
                if bone not in existing: ctl.add_bone_track(bone,False)
                values=[f[i] for f in d['frames']]
                assert ctl.set_bone_track_keys(bone,[unreal.Vector(*v['t']) for v in values],
                    [unreal.Quat(*v['q']) for v in values],[unreal.Vector(1,1,1)]*len(values),False),bone
        finally:ctl.close_bracket(False)
        if d.get('additive'):
            asset.set_editor_property('additive_anim_type',unreal.AdditiveAnimationType.AAT_LOCAL_SPACE_BASE)
            asset.set_editor_property('ref_pose_type',unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
            asset.set_editor_property('ref_pose_seq',idle); asset.set_editor_property('ref_frame_index',0)
            asset.set_editor_property('enable_root_motion',False)
            asset.set_editor_property('force_root_lock',True)
            for curve in unreal.AnimationLibrary.get_animation_curve_names(asset,unreal.RawCurveTrackTypes.RCT_FLOAT):
                unreal.AnimationLibrary.remove_curve(asset,curve,False)
        lib.set_metadata_tag(asset,'GroundHeavyBake',signature)
        assert lib.save_loaded_asset(asset,False),path
        changed.append(path)
    return {'signature':data['signature'],'previous_signature':data.get('previous_signature'),'changed':changed,
            'changed_gaits':[d.get('row','MovingTurn'+('Left' if n.endswith('_L') else 'Right'))
                for n,d in data['clips'].items() if folder+'/'+n in changed],
            'authoring_report':str(out/'heavy_gait_authoring.json')}
