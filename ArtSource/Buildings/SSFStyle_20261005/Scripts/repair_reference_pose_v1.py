"""Restore source mesh reference poses on owned formal copies only.

FBX rounding of near-zero bone offsets can change OrientAndScale retargeting.
Use the engine's supported mesh skeleton modifier; do not edit the cloned
Skeleton reference poses, animation keys, geometry or frozen Blender source.
"""
import json, math, runpy, traceback
from pathlib import Path
import unreal

R = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
D = R/'UE_Delivery_v1'
assert '-SSFReferenceRepairWorker' in unreal.SystemLibrary.get_command_line()
ns = runpy.run_path(str(R/'Scripts/import_ue_delivery_v1.py'), run_name='ssf_import_functions')
im = json.loads((D/'ue_import.json').read_text(encoding='utf8'))
assert im['success']
ns['REPORT'].update(im)
report = {'success':False, 'method':'SkeletonModifier source local mesh reference pose',
          'source_packages_modified':False, 'frozen_B_version_modified':False, 'meshes':[]}

def poses(path):
    return list(unreal.SkeletonService.list_bones(path))

def error(a, b):
    result = [0.,0.,0.]
    assert len(a)==len(b)
    for x,y in zip(a,b):
        assert x.bone_name==y.bone_name and x.parent_bone_name==y.parent_bone_name
        p,q=x.local_transform,y.local_transform
        r,s=p.rotation,q.rotation
        ra=(r.x,r.y,r.z,r.w);sa=(s.x,s.y,s.z,s.w)
        dot=abs(sum(i*j for i,j in zip(ra,sa)))/(sum(i*i for i in ra)*sum(i*i for i in sa))**.5
        values=[(p.translation-q.translation).length(),(p.scale3d-q.scale3d).length(),
                math.degrees(2*math.acos(min(1.,dot)))]
        result=[max(i,j) for i,j in zip(result,values)]
    return result

try:
    sk=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    for a in im['assets']:
        if a['type']!='SkeletalMesh':continue
        ns['owned'](a['path']);ns['owned'](a['skeleton'])
        mesh=unreal.load_asset(a['path']);skeleton=mesh.get_editor_property('skeleton')
        assert skeleton.get_path_name()==a['skeleton']
        original=poses(a['source']);before=poses(a['path']);skeleton_before=poses(a['skeleton'])
        screens=[m.get_editor_property('screen_size').export_text() for m in mesh.get_editor_property('source_models')]
        settings=[sk.get_lod_build_settings(mesh,i).export_text() for i in range(3)]
        materials=[m.get_editor_property('material_interface').get_path_name() for m in mesh.get_editor_property('materials')]
        modifier=unreal.SkeletonModifier()
        assert modifier.set_skeletal_mesh(mesh)
        assert modifier.set_bones_transforms([b.bone_name for b in original], [b.local_transform for b in original],True)
        committed=modifier.commit_skeleton_to_skeletal_mesh()
        forced_exact_commit=False
        # UE skips transform-only commits below KINDA_SMALL_NUMBER. A tiny
        # offset can still rotate OrientAndScale retargeting noticeably when
        # the original bone offset is almost zero. Apply an owned temporary
        # reference offset and restore the exact source transforms before
        # saving; neither step changes bone topology or mesh vertices.
        if not committed and any(x.local_transform.export_text()!=y.local_transform.export_text() for x,y in zip(original,before)):
            temporary=original[0].local_transform.copy()
            temporary.translation=temporary.translation+unreal.Vector(.1,0,0)
            assert modifier.set_bone_transform(original[0].bone_name,temporary,True)
            assert modifier.commit_skeleton_to_skeletal_mesh()
            # Commit moves the modifier's MeshDescription into the asset.
            # Create a fresh modifier before the second commit.
            modifier=unreal.SkeletonModifier()
            assert modifier.set_skeletal_mesh(mesh)
            assert modifier.set_bones_transforms([b.bone_name for b in original],[b.local_transform for b in original],True)
            assert modifier.commit_skeleton_to_skeletal_mesh()
            committed=True;forced_exact_commit=True
        after=poses(a['path']);delta=error(original,after)
        assert error(skeleton_before,poses(a['skeleton']))==[0.,0.,0.],a['key']
        assert sk.get_lod_count(mesh)==3
        assert screens==[m.get_editor_property('screen_size').export_text() for m in mesh.get_editor_property('source_models')]
        assert settings==[sk.get_lod_build_settings(mesh,i).export_text() for i in range(3)]
        assert materials==[m.get_editor_property('material_interface').get_path_name() for m in mesh.get_editor_property('materials')]
        assert delta[0]<.0001 and delta[1]<.00001 and delta[2]<.001,(a['key'],delta)
        if committed:ns['save'](mesh)
        a['reference_contract']={'bones':len(after),'translation_error_cm':delta[0],
            'scale_error':delta[1],'angle_error_deg':delta[2],'root_scale':list(after[0].local_transform.scale3d.to_tuple())}
        a['UE_export_readback']=ns['export_mesh'](mesh,a['key'],True)
        report['meshes'].append({'key':a['key'],'committed':committed,'forced_below_epsilon_exact_commit':forced_exact_commit,'before_error_cm_scale_degrees':error(original,before),
            'after_error_cm_scale_degrees':delta,'copied_skeleton_reference_pose_unchanged':True,
            'LOD_settings_and_materials_unchanged':True})
        (D/'reference_pose_repair.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    ns['REPORT']['assets']=im['assets']
    ns['REPORT']['source_reference_pose_restored']=True
    ns['checkpoint']()
    report['success']=True
except Exception:
    report['error']=traceback.format_exc();unreal.log_error(report['error'])
finally:
    (D/'reference_pose_repair.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.SystemLibrary.quit_editor()
