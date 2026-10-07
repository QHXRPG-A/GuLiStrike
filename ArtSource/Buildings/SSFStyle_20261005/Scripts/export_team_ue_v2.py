"""Export only the released actual twelve Blender building variants; never save the blend."""
import bpy,json,hashlib
import numpy as np
from pathlib import Path
from mathutils import Matrix,Vector,Quaternion
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'TeamPalette_B_v2_20261007';D=R/'UE_Delivery_Team_v2';F=D/'FBX'
F.mkdir(exist_ok=True)
auth=json.loads((R/'approval_B_v2_import_20261007.json').read_text(encoding='utf8'))
assert auth['status']=='released_for_formal_UE_storage'
assert hashlib.sha256((O/'SSF_TeamPalette_B_v2.blend').read_bytes()).hexdigest()==auth['source_blend_sha256']
bpy.ops.wm.open_mainfile(filepath=auth['source_blend'])
candidate=json.loads((O/'candidate_manifest.json').read_text(encoding='utf8'))
source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'));src={r['key']:r for r in source['meshes']}
report={'success':False,'source_version':'SSF_TeamPalette_B_v2','source_sha256':auth['source_blend_sha256'],
        'target_root':auth['target_root'],'assets':[],'source_saved':False,
        'portable_uvs':['AtlasUV','SourceUV','PaletteLinear_RG','PaletteLinear_B_LODTeam']}
S=Matrix.Diagonal((1,-1,1,1))
def dump():(D/'export_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
def select(objects,active):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.hide_set(False);o.hide_select=False;o.select_set(True)
    bpy.context.view_layer.objects.active=active
def transform(v):
    q=v['rotation_xyzw'];m=Matrix.LocRotScale(Vector(v['translation_cm']),Quaternion((q[3],q[0],q[1],q[2])),Vector(v['scale']))
    return S@m@S
def rig_from_source(key,scene):
    data=src[key];root_scale=data['bones'][0]['global']['scale'];scale=sum(root_scale)/3
    assert max(abs(x-scale) for x in root_scale)<1e-5
    rig=bpy.data.objects.new('Armature',bpy.data.armatures.new(key+'_UE_Reference'))
    scene.collection.objects.link(rig);select([rig],rig);bpy.ops.object.mode_set(mode='EDIT')
    for b in data['bones']:
        bone=rig.data.edit_bones.new(b['name']);bone.head=(0,0,0);bone.tail=(0,5,0)
        world=transform(b['global']);world.translation/=scale;bone.matrix=world;bone.length=5
        if b['parent']:bone.parent=rig.data.edit_bones[b['parent']]
    bpy.ops.object.mode_set(mode='OBJECT');rig.scale=(scale,scale,scale);bpy.context.view_layer.update();return rig
def placeholder(name):
    m=bpy.data.materials.get(name) or bpy.data.materials.new(name);m.diffuse_color=(.5,.5,.5,1);return m
def linear(code):
    rgb=[int(code[i:i+2],16)/255 for i in (1,3,5)]
    return [v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb]
def copy_surface(old,scene,lod,accent,outline=False):
    deps=bpy.context.evaluated_depsgraph_get();ev=old.evaluated_get(deps)
    m=bpy.data.meshes.new_from_object(ev,preserve_all_data_layers=True,depsgraph=deps);world=old.matrix_world.copy()
    normals=[tuple((world.to_3x3().inverted().transposed()@n.vector).normalized()) for n in m.corner_normals]
    m.transform(Matrix.Scale(100,4)@world);m.normals_split_custom_set(normals);m.update()
    o=bpy.data.objects.new(old.name+'_UEExport',m)
    for g in old.vertex_groups:o.vertex_groups.new(name=g.name)
    scene.collection.objects.link(o)
    uv0=m.uv_layers.get('SSF_AtlasUV') or m.uv_layers.get('SourceUV');uv1=m.uv_layers.get('SourceUV') or uv0
    atlas=[tuple(x.uv) for x in uv0.data] if uv0 else [(0,0)]*len(m.loops)
    original=[tuple(x.uv) for x in uv1.data] if uv1 else atlas
    color=m.color_attributes.get('SSF_PaletteLinear');assert color or outline
    rgba=[tuple(x.color) for x in color.data] if color else [(1,1,1,1)]*len(m.loops)
    for uv in list(m.uv_layers):m.uv_layers.remove(uv)
    arrays=[atlas,original,[(c[0],c[1]) for c in rgba],[(c[2],float(lod)+.25*float(sum((c[i]-accent[i])**2 for i in range(3))<1e-8)) for c in rgba]]
    for name,array in zip(report['portable_uvs'],arrays):
        uv=m.uv_layers.new(name=name)
        for item,xy in zip(uv.data,array):item.uv=xy
    m.uv_layers.active_index=0;m.uv_layers[0].active_render=True
    flags=[p.material_index for p in m.polygons];m.materials.clear()
    m.materials.append(placeholder('SSF_Outline' if outline else 'SSF_Body'))
    if not outline and max(flags,default=0):m.materials.append(placeholder('SSF_Display'))
    for p,i in zip(m.polygons,flags):p.material_index=0 if outline else i
    return o
for row in candidate['assets']:
    team,key=row['team'],row['key'];variant=team+'_'+key
    original_scene=bpy.data.scenes[row['scene']];bpy.context.window.scene=original_scene
    bpy.data.objects[row['rig']].data.pose_position='REST';bpy.context.view_layer.update()
    theme=candidate['role_mapping'][team][key];accent=linear(theme['Accent'])
    asset={'key':variant,'building':key,'team':team,'source':src[key]['path'],'theme':theme,
           'bone_count':len(src[key]['bones']),'lods':[],
           'base_color_texture':str(O/'Textures'/(variant+'_BaseColor_2K.png'))}
    for entry in row['lods']:
        lod=entry['LOD'];bpy.context.window.scene=original_scene;bpy.context.view_layer.update()
        tmp=bpy.data.scenes.new('Export_'+variant+'_LOD'+str(lod));tmp.unit_settings.system='METRIC';tmp.unit_settings.scale_length=.01
        body=copy_surface(bpy.data.objects[entry['body']],tmp,lod,accent)
        edge=copy_surface(bpy.data.objects[entry['outline']],tmp,lod,accent,True) if entry.get('outline') else None
        bpy.context.window.scene=tmp;bpy.context.view_layer.update();objects=[body]+([edge] if edge else []);select(objects,body)
        if edge:bpy.ops.object.join()
        mesh=bpy.context.object;mesh.name=variant+'_LOD'+str(lod);rig=rig_from_source(key,tmp)
        mesh.parent=rig;mesh.matrix_parent_inverse=rig.matrix_world.inverted();mesh.matrix_basis=Matrix.Identity(4)
        mod=mesh.modifiers.new('Original_UE_Binding','ARMATURE');mod.object=rig;bpy.context.view_layer.update();mesh.data.calc_loop_triangles()
        count=len(mesh.data.loop_triangles);expected_count=entry['source_body_triangles']+entry['source_outline_triangles'];assert count==expected_count,(variant,lod,count,expected_count)
        points=np.array([list(mesh.matrix_world@v.co) for v in mesh.data.vertices],np.float32)
        path=F/(variant+'_LOD'+str(lod)+'.fbx');select([mesh,rig],mesh)
        bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','ARMATURE'},add_leaf_bones=False,
            use_armature_deform_only=False,bake_anim=False,axis_forward='-Y',axis_up='Z',apply_unit_scale=True,
            apply_scale_options='FBX_SCALE_NONE',use_mesh_modifiers=True,mesh_smooth_type='FACE',colors_type='LINEAR',use_custom_props=False)
        expected=points*.01
        data={'points_m':expected.tolist(),'weights':[[[mesh.vertex_groups[g.group].name,g.weight] for g in v.groups] for v in mesh.data.vertices],
              'corner_uvs':[[list(d.uv) for d in u.data] for u in mesh.data.uv_layers],
              'materials':[m.name for m in mesh.data.materials],'normal_min_length':min(n.vector.length for n in mesh.data.corner_normals)}
        path.with_name(path.stem+'_Expected.json').write_text(json.dumps(data),encoding='utf8')
        asset['lods'].append({'LOD':lod,'fbx':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'triangles':count,
            'body_triangles':entry['source_body_triangles'],'outline_triangles':entry['source_outline_triangles'],
            'materials':data['materials'],'bone_count':asset['bone_count'],'bounds_m':{'min':expected.min(0).tolist(),'max':expected.max(0).tolist()},'root_scale':list(rig.scale)})
        rig.name='AlreadyExported_'+variant+'_LOD'+str(lod);print('SSF_TEAM_EXPORTED',variant,lod,count,flush=True)
    report['assets'].append(asset);dump()
report['success']=True;dump()
assert hashlib.sha256((O/'SSF_TeamPalette_B_v2.blend').read_bytes()).hexdigest()==auth['source_blend_sha256']
print('SSF_TEAM_EXPORT_COMPLETE',flush=True)
