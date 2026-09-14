"""Read the newly imported assets in the user's current UE editor."""
import unreal,json,math
from pathlib import Path
OUT=Path('D:/UE5.7/test1/outputs/hardsurface-models-20260914')
report=json.loads((OUT/'ue_import_report.json').read_text(encoding='utf-8'))
registry=unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(['/Game/GuLiStrike/Buildings/'+n for n in report['assets']],force_rescan=True)
results={};meshes=[]
for name,expected in report['assets'].items():
    mesh=unreal.load_asset(expected['mesh']);assert mesh,name
    materials=[s.material_interface.get_path_name() for s in mesh.materials] if isinstance(mesh,unreal.SkeletalMesh) else [s.material_interface.get_path_name() for s in mesh.static_materials]
    assert materials==[expected['material']],(name,materials)
    textures={}
    for role,info in expected['textures'].items():
        tex=unreal.load_asset(info['path']);assert tex
        assert bool(tex.srgb)==info['srgb'],(name,role)
        assert str(tex.compression_settings)==info['compression'],(name,role)
        textures[role]=tex.get_path_name()
    results[name]={'mesh':mesh.get_path_name(),'class':mesh.get_class().get_name(),'materials':materials,'textures':textures}
    if isinstance(mesh,unreal.SkeletalMesh):
        anim=unreal.load_asset(expected['animation']);assert anim and anim.get_editor_property('skeleton')==mesh.skeleton
        assert [str(b.bone_name) for b in unreal.SkeletonService.list_bones(mesh.get_path_name())]==expected['bones']
        for bone in unreal.SkeletonService.list_bones(mesh.get_path_name()):
            for t in [0,2,5]:
                pose=unreal.AnimationLibrary.get_bone_pose_for_time(anim,bone.bone_name,t,False)
                assert (pose.translation-bone.local_transform.translation).length()<.001
                assert (pose.scale3d-bone.local_transform.scale3d).length()<.001
        samples={str(b):[[getattr(unreal.AnimationLibrary.get_bone_pose_for_time(anim,b,t,False).rotation,k) for k in ['x','y','z','w']] for t in [0,1,2,5]] for b in expected['bones']}
        angle=lambda a,b:math.degrees(2*math.acos(min(1,abs(sum(x*y for x,y in zip(a,b))))))
        motion={b:[angle(v[0],q) for q in v] for b,v in samples.items()}
        assert max(motion['root'])<.01 and max(motion['base_yaw'])>179 and max(motion['barrel_pitch'])>34
        results[name].update({'skeleton':mesh.skeleton.get_path_name(),'animation':anim.get_path_name(),'animation_seconds':anim.get_play_length(),'animation_angles_from_rest_deg':motion})
    meshes.append(mesh)
unreal.EditorAssetLibrary.sync_browser_to_objects([m.get_path_name() for m in meshes])
result={'success':True,'assets':results,'world':unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name(),
        'showcase_loaded':unreal.find_object(None,'/Game/GuLiStrike/Buildings/IndustrialDefenseSet/Demo/LVL_IndustrialDefense_Showcase.LVL_IndustrialDefense_Showcase') is not None}
(OUT/'ue_live_validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
