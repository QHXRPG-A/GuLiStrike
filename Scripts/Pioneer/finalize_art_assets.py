"""Run in the live Editor after the isolated import worker has exited."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
namespace={'__name__':'pioneer_art_helpers'}
exec((ROOT/'Scripts/Pioneer/import_art_assets.py').read_text(encoding='utf8'),namespace)
BASE=namespace['BASE'];LIB=namespace['LIB'];EDIT=namespace['EDIT'];META=namespace['META']
save=namespace['save'];owned=namespace['owned'];node=namespace['node'];link=namespace['link']

def install():
    result={'success':False,'overlays':[]}
    fn=owned(BASE+'/VAT/MF_Pioneer_BoneVAT')
    assert fn
    for source,name in [('/Game/GuLiStrike/FX/UnitFeedback/M_UnitHitWhite_Instanced','Hit'),
                        ('/Game/GuLiStrike/FX/UnitFeedback/M_UnitWreckRust','Wreck'),
                        ('/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportBody','Phase')]:
        path=BASE+'/VAT/M_Pioneer_'+name
        mat=owned(path)
        if not mat:
            mat=LIB.duplicate_asset(source,path)
            assert mat,path
            save(mat)
        if LIB.get_metadata_tag(mat,'GuLi.OverlayVAT')!='v1':
            expressions=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==mat]
            calls=[n for n in expressions if isinstance(n,unreal.MaterialExpressionMaterialFunctionCall)
                   and 'MF_GuLiRigidMechanical' in str(n.get_editor_property('material_function'))]
            assert len(calls)==1,(path,calls)
            old=calls[0]
            vat=node(mat,unreal.MaterialExpressionMaterialFunctionCall)
            assert vat.set_material_function(fn)
            assert EDIT.connect_material_property(vat,'Offset',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
            # Preserve existing effect graphs, replacing their animated-normal source.
            for expression in expressions:
                if isinstance(expression,unreal.MaterialExpressionVertexInterpolator):
                    if old in EDIT.get_inputs_for_material_expression(mat,expression):link(vat,expression,'','Normal')
            EDIT.delete_material_expression(mat,old)
            mat.set_editor_property('used_with_instanced_static_meshes',True)
            mat.set_editor_property('max_world_position_offset_displacement',1500)
            EDIT.recompile_material(mat)
            LIB.set_metadata_tag(mat,'GuLi.OverlayVAT','v1');save(mat)
        result['overlays'].append(path)
    sk=owned(BASE+'/Meshes/SK_Pioneer')
    materials=list(sk.get_editor_property('materials'))
    for material in materials:
        slot=str(material.get_editor_property('material_slot_name'))
        target='M_Pioneer_SK_Contour' if 'Contour' in slot else 'MI_Pioneer_SK_LOD'+slot.rsplit('LOD',1)[1]
        material.set_editor_property('material_interface',owned(BASE+'/Materials/'+target))
    sk.set_editor_property('materials',materials)
    sources=list(sk.get_editor_property('source_models'))
    for i,source in enumerate(sources):
        source.set_editor_property('screen_size',unreal.PerPlatformFloat(default=META['screen_sizes'][i]))
    sk.set_editor_property('source_models',sources);save(sk)
    sub=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    result['skeletal_lods']=[]
    for i,source in enumerate(sk.get_editor_property('source_models')):
        indices=[sub.get_lod_material_slot(sk,i,s) for s in range(2 if i<3 else 1)]
        result['skeletal_lods'].append({'screen_size':source.get_editor_property('screen_size').default,
            'materials':[materials[index].get_editor_property('material_interface').get_path_name() for index in indices]})
    # The skeleton and physics retain the original authoring scale. Compare every local reference transform.
    original='/Game/Assets/RSG_UnderWater_Pack/FPS/Models/Mech/SK_FPS_Mech'
    maximum=0;maximum_angle=0
    for bone in META['bones']:
        a=unreal.SkeletonService.get_bone_transform(original,bone['name'],False)
        b=unreal.SkeletonService.get_bone_transform(sk.get_path_name(),bone['name'],False)
        maximum=max(maximum,(a.translation-b.translation).length())
        qa=a.rotation;qb=b.rotation
        dot=abs(qa.x*qb.x+qa.y*qb.y+qa.z*qb.z+qa.w*qb.w)
        dot/=((qa.x**2+qa.y**2+qa.z**2+qa.w**2)*(qb.x**2+qb.y**2+qb.z**2+qb.w**2))**.5
        import math
        maximum_angle=max(maximum_angle,math.degrees(2*math.acos(min(1,dot))))
        assert (a.scale3d-b.scale3d).length()<.001,bone['name']
    # FBX bone-roll round trips have sub-0.1-degree precision differences; keep the copied skeleton unchanged.
    assert maximum<.02 and maximum_angle<.1,(maximum,maximum_angle)
    copied_skeleton=sk.get_editor_property('skeleton').get_path_name()
    original_skeleton=original+'_Skeleton'
    for bone in META['bones']:
        a=unreal.SkeletonService.get_bone_transform(original_skeleton,bone['name'],False)
        b=unreal.SkeletonService.get_bone_transform(copied_skeleton,bone['name'],False)
        assert (a.translation-b.translation).length()<1e-6,bone['name']
        assert max(abs(x-y) for x,y in zip(a.rotation.to_tuple(),b.rotation.to_tuple()))<1e-6,bone['name']
    result['reference_pose']={'bones':44,'copied_skeleton_reference_unchanged':True,
        'maximum_mesh_local_position_delta_cm':maximum,'maximum_mesh_local_angle_delta_degrees':maximum_angle,
        'note':'Mesh FBX numerical round-trip differences; original-compatible skeleton reference is unchanged.'}
    result['success']=True
    namespace['write']('ue_art_finalization.json',result)
    return result

unreal.MCPythonHelper.submit_result(json.dumps(install()))
