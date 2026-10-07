"""Read saved three-tier candidate groups, indices, sockets and compatibility references."""
import unreal,json,sys,math,hashlib
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,REVIEW_PACKAGE
from common import require_unapproved_candidate
require_unapproved_candidate()
lib=unreal.EditorAssetLibrary;sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
skeletal=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
native=json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))
blender={row['name']:row for row in json.loads((ART/'Reports/blender_candidates.json').read_text(encoding='utf8'))}
report=dict(success=False,formal_assets_changed=False,approval='pending',native_compile='not_run',units=[])
for row in native['units']:
    result=dict(id=row['id'],name=row['name'],meshes=[],triangles=[0,0,0])
    for item in row['meshes']:
        mesh=unreal.load_asset(item['asset']);original=unreal.load_asset(item['source'])
        skin=isinstance(mesh,unreal.SkeletalMesh)
        count=skeletal.get_lod_count(mesh) if skin else sub.get_lod_count(mesh)
        assert count==3,item['asset']
        data=dict(source=item['source'],asset=item['asset'],lod_count=count)
        if skin:
            data['triangles']=next(x['fbx_triangles'] for x in blender[row['name']]['meshes'] if x['source']==item['source'])
            data['triangle_source']='FBX render export'
            assert mesh.skeleton==original.skeleton
            data['skeleton']=mesh.skeleton.get_path_name()
            data['physics_asset']=mesh.physics_asset.get_path_name() if mesh.physics_asset else None
            source_bones=list(unreal.SkeletonService.list_bones(original.get_path_name()))
            candidate_bones=list(unreal.SkeletonService.list_bones(mesh.get_path_name()))
            assert [str(x.bone_name) for x in source_bones]==[str(x.bone_name) for x in candidate_bones]
            data['bone_count']=len(candidate_bones)
            data['screen_sizes']=[model.get_editor_property('screen_size').get_editor_property('default') for model in mesh.get_editor_property('source_models')]
        else:
            data.update(triangles=[mesh.get_num_triangles(i) for i in range(3)],
                sections=[mesh.get_num_sections(i) for i in range(3)],
                screen_sizes=list(sub.get_lod_screen_sizes(mesh)),uv_channels=[sub.get_num_uv_channels(mesh,i) for i in range(3)])
            assert mesh.get_num_triangles(0)==original.get_num_triangles(0)
            data['materials']=[slot.material_interface.get_path_name() if slot.material_interface else None for slot in mesh.static_materials]
            assert data['materials']==[slot.material_interface.get_path_name() if slot.material_interface else None for slot in original.static_materials]
            component=unreal.new_object(unreal.StaticMeshComponent);component.set_static_mesh(original)
            data['sockets']=[]
            for name in component.get_all_socket_names():
                source=original.find_socket(name);target=mesh.find_socket(name);assert target,name
                assert source.relative_location==target.relative_location and source.relative_rotation==target.relative_rotation and source.relative_scale==target.relative_scale
                data['sockets'].append(str(name))
            if row['name']=='WM01':
                diagnostics=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.get_missile_pod_mesh_diagnostics(mesh))
                if 'lods' in diagnostics:
                    assert all(lod['nonintegral_parts']==lod['mixed_pod_triangles']==0 for lod in diagnostics['lods'])
                else:print('LEGACY_NATIVE_DIAGNOSTIC',diagnostics)
                data['mechanical_part_readback']=diagnostics
        repeats=sum(component['mesh']==item['source'] for component in row['components']) if row['components'] else 1
        data['instances_per_unit']=repeats
        result['triangles']=[a+b*repeats for a,b in zip(result['triangles'],data['triangles'])]
        result['meshes'].append(data)
    report['units'].append(result)
meta=json.loads((ART/'BiZhiMao/vertex_metadata.json').read_text(encoding='utf8'))
biz=dict(id=6,name='BiZhiMao',meshes=[],triangles=[],textures=[])
for role in ['VAT','Construction']:
    mesh=unreal.load_asset(REVIEW_PACKAGE+'/BiZhiMao/Meshes/SM_BiZhiMao_'+role)
    assert sub.get_lod_count(mesh)==3
    data=dict(asset=mesh.get_path_name(),lod_count=3,triangles=[],sections=[],screen_sizes=list(sub.get_lod_screen_sizes(mesh)))
    for lod in meta['lods']:
        i=lod['lod'];desc=mesh.get_static_mesh_description(i);n=desc.get_vertex_instance_count()
        assert sub.get_num_uv_channels(mesh,i)==6
        errors=[]
        for corner in range(n):
            vi=unreal.VertexInstanceID(corner);index=desc.get_vertex_instance_uv(vi,3);tier=desc.get_vertex_instance_uv(vi,5)
            assert abs(tier.x-i)<1e-4 and 0<=int(math.floor(index.x*lod['vertex_count']))<lod['vertex_count']
        assert mesh.get_num_triangles(i)==sum(section['triangles'] for section in lod['sections'])
        assert mesh.get_num_sections(i)==len(lod['sections'])
        data['triangles'].append(mesh.get_num_triangles(i));data['sections'].append(mesh.get_num_sections(i))
    biz['meshes'].append(data)
biz['triangles']=biz['meshes'][0]['triangles']
for lod in meta['lods']:
    for role in ['Position','Rotation']:
        texture=unreal.load_asset(REVIEW_PACKAGE+f'/BiZhiMao/Textures/T_BiZhiMao_Vertex{role}_LOD{lod["lod"]}')
        assert texture.blueprint_get_size_x()==lod['width'] and texture.blueprint_get_size_y()==lod['height']
        assert not texture.srgb and texture.filter==unreal.TextureFilter.TF_NEAREST
        biz['textures'].append(dict(asset=texture.get_path_name(),width=lod['width'],height=lod['height'],samples=lod['frames_per_clip']))
fn=unreal.load_asset(REVIEW_PACKAGE+'/BiZhiMao/VAT/MF_BiZhiMao_VertexVAT')
custom=next(node for node in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if node.get_outer()==fn)
pins=[str(pin.get_editor_property('input_name')) for pin in custom.get_editor_property('inputs')]
code=custom.get_editor_property('code')
assert code.strip()==(ROOT/'Scripts/BiZhiMao/BiZhiMaoVertexVAT.hlsl').read_text(encoding='utf8').strip()
biz['shader_code_sha256']=hashlib.sha256(code.encode()).hexdigest()
assert all(name in pins for name in ['Position0','Position1','Position2','Rotation0','Rotation1','Rotation2'])
assert 'Position3' not in pins and 'Rotation3' not in pins
biz['shader_pins']=pins;biz['runtime_bones']=0;biz['animation_gpu_bytes']=meta['gpu_animation_bytes']
report['units'].append(biz)
report['success']=True
(ART/'Reports/candidate_readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,units=[dict(name=row['name'],triangles=row['triangles']) for row in report['units']])))
