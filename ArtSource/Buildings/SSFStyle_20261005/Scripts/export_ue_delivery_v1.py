"""Export the released B_v1 in a separate Blender process; never save its source."""
import bpy, json, hashlib, math, sys
from pathlib import Path
from mathutils import Matrix, Vector, Quaternion
import numpy as np

R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
P=R/'Production_B_v1'; D=R/'UE_Delivery_v1'; F=D/'FBX'; F.mkdir(parents=True,exist_ok=True)
auth=json.loads((R/'approval_B_import_20261006.json').read_text(encoding='utf8'))
assert hashlib.sha256((P/'SSF_Production_B_v1.blend').read_bytes()).hexdigest()==auth['source_blend_sha256']
bpy.ops.wm.open_mainfile(filepath=str(P/'SSF_Production_B_v1.blend'))
source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))
src={a['key']:a for a in source['meshes']}
product=json.loads((P/'construction_report.json').read_text(encoding='utf8'))
S=Matrix.Diagonal((1,-1,1,1))
report={'success':False,'source_version':'SSF_Production_B_v1','source_sha256':auth['source_blend_sha256'],
        'target_root':'/Game/GuLiStrike/Buildings/SSFStylized','assets':[],
        'portable_uvs':['AtlasUV','SourceUV','PaletteLinear_RG','PaletteLinear_B_LODTeam'],
        'bone_space':'original UE reference including CommandCenter 0.3 root scale', 'source_saved':False}

def dump(): (D/'export_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
def select(objects,active):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.hide_set(False);o.hide_select=False;o.select_set(True)
    bpy.context.view_layer.objects.active=active
def transform(v):
    q=v['rotation_xyzw']
    m=Matrix.LocRotScale(Vector(v['translation_cm']),Quaternion((q[3],q[0],q[1],q[2])),Vector(v['scale']))
    return S@m@S
def rig_from_source(key,scene):
    data=src[key]; root_scale=data['bones'][0]['global']['scale']; scale=sum(root_scale)/3
    assert max(abs(x-scale) for x in root_scale)<1e-5
    rig=bpy.data.objects.new('Armature',bpy.data.armatures.new(key+'_UE_Reference'))
    scene.collection.objects.link(rig);select([rig],rig);bpy.ops.object.mode_set(mode='EDIT')
    for b in data['bones']:
        bone=rig.data.edit_bones.new(b['name']);bone.head=(0,0,0);bone.tail=(0,5,0)
        world=transform(b['global']);world.translation/=scale;bone.matrix=world
        bone.length=5
        if b['parent']:bone.parent=rig.data.edit_bones[b['parent']]
    bpy.ops.object.mode_set(mode='OBJECT');rig.scale=(scale,scale,scale)
    bpy.context.view_layer.update();return rig
def placeholder(name):
    mat=bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.diffuse_color=(.5,.5,.5,1);return mat
def copy_surface(old,scene,lod,accent,outline=False):
    # Evaluate the actual approved outline-width modifier in rest pose.
    deps=bpy.context.evaluated_depsgraph_get(); ev=old.evaluated_get(deps)
    mesh=bpy.data.meshes.new_from_object(ev,preserve_all_data_layers=True,depsgraph=deps)
    world=old.matrix_world.copy()
    native_normals=[tuple((world.to_3x3().inverted().transposed()@n.vector).normalized()) for n in mesh.corner_normals]
    mesh.transform(Matrix.Scale(100,4)@world)
    mesh.normals_split_custom_set(native_normals);mesh.update()
    obj=bpy.data.objects.new(old.name+'_UEExport',mesh)
    for g in old.vertex_groups:obj.vertex_groups.new(name=g.name)
    scene.collection.objects.link(obj)
    uv0=mesh.uv_layers.get('SSF_AtlasUV') or mesh.uv_layers.get('SourceUV')
    uv1=mesh.uv_layers.get('SourceUV') or uv0
    atlas=[tuple(x.uv) for x in uv0.data] if uv0 else [(0,0)]*len(mesh.loops)
    original=[tuple(x.uv) for x in uv1.data] if uv1 else atlas
    color=mesh.color_attributes.get('SSF_PaletteLinear')
    rgba=[tuple(x.color) for x in color.data] if color else [(1,1,1,1)]*len(mesh.loops)
    for uv in list(mesh.uv_layers):mesh.uv_layers.remove(uv)
    # UE skeletal import supports four UV channels. Encode integer LOD plus
    # an exact .25 team flag in UV3.y; recover UE's V flip in the material.
    arrays=[atlas,original,[(c[0],c[1]) for c in rgba],
            [(c[2],float(lod)+.25*float(sum((c[i]-accent[i])**2 for i in range(3))<1e-8)) for c in rgba]]
    for name,array in zip(report['portable_uvs'],arrays):
        uv=mesh.uv_layers.new(name=name)
        for item,xy in zip(uv.data,array):item.uv=xy
    mesh.uv_layers.active_index=0;mesh.uv_layers[0].active_render=True
    flags=[p.material_index for p in mesh.polygons]
    mesh.materials.clear()
    if outline:mesh.materials.append(placeholder('SSF_Outline'))
    elif old.name.startswith('Light_'):mesh.materials.append(placeholder('SSF_LightDisplay'))
    else:
        mesh.materials.append(placeholder('SSF_Body'))
        if max(flags,default=0):mesh.materials.append(placeholder('SSF_Display'))
    for p,i in zip(mesh.polygons,flags):p.material_index=0 if outline else i
    return obj
def linear(code):
    rgb=[int(code[i:i+2],16)/255 for i in (1,3,5)]
    return [v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb]
refs={a['key']:a for a in json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))['assets']}
requested=set(sys.argv[sys.argv.index('--')+1:]) if '--' in sys.argv else None
for row in product['assets']:
    key=row['key']
    if requested and key not in requested:continue
    original_scene=bpy.data.scenes[row['scene']];bpy.context.window.scene=original_scene
    native_rig=bpy.data.objects[row['rig']] if row['rig'] else None
    if native_rig:native_rig.data.pose_position='REST'
    bpy.context.view_layer.update()
    accent=linear(refs[key]['theme']['Accent'])
    asset={'key':key,'source':src[key]['path'],'skeleton_source':src[key].get('skeleton'),
           'physics_source':src[key].get('physics_asset'),'textures':row['textures'],'theme':refs[key]['theme'],
           'bone_count':len(src[key].get('bones',[])),'animations':row.get('animations',[]),'lods':[]}
    for entry in row['lods']:
        lod=entry['LOD'];bpy.context.window.scene=original_scene;bpy.context.view_layer.update()
        tmp=bpy.data.scenes.new('Export_'+key+'_LOD'+str(lod))
        tmp.unit_settings.system='METRIC';tmp.unit_settings.scale_length=.01
        body=copy_surface(bpy.data.objects[entry['body']],tmp,lod,accent)
        edge=copy_surface(bpy.data.objects[entry['outline']],tmp,lod,accent,True) if entry['outline'] else None
        bpy.context.window.scene=tmp;bpy.context.view_layer.update();objects=[body]+([edge] if edge else [])
        select(objects,body)
        if edge:bpy.ops.object.join()
        mesh=bpy.context.object;mesh.name=key+'_LOD'+str(lod)
        rig=rig_from_source(key,tmp) if native_rig else None
        if rig:
            mesh.parent=rig;mesh.matrix_parent_inverse=rig.matrix_world.inverted();mesh.matrix_basis=Matrix.Identity(4)
            mod=mesh.modifiers.new('Original_UE_Binding','ARMATURE');mod.object=rig
        bpy.context.view_layer.update();mesh.data.calc_loop_triangles()
        count=len(mesh.data.loop_triangles);assert count==entry['total_triangles'],(key,lod,count,entry['total_triangles'])
        pts=np.array([list(mesh.matrix_world@v.co) for v in mesh.data.vertices],np.float32)
        path=F/(key+'_LOD'+str(lod)+'.fbx');select([mesh]+([rig] if rig else []),mesh)
        bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','ARMATURE'},
            add_leaf_bones=False,use_armature_deform_only=False,bake_anim=False,axis_forward='-Y',axis_up='Z',
            apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',use_mesh_modifiers=True,
            mesh_smooth_type='FACE',colors_type='LINEAR',use_custom_props=False)
        expected=pts*.01
        # A portable numeric baseline permits independent roundtrip comparison.
        data={'points_m':expected.tolist(),'weights':[[[mesh.vertex_groups[g.group].name,g.weight] for g in v.groups] for v in mesh.data.vertices],
              'corner_uvs':[[list(u.uv) for u in layer.data] for layer in mesh.data.uv_layers],
              'materials':[m.name for m in mesh.data.materials],
              'normal_min_length':min(n.vector.length for n in mesh.data.corner_normals)}
        (F/(key+'_LOD'+str(lod)+'_Expected.json')).write_text(json.dumps(data),encoding='utf8')
        asset['lods'].append({'LOD':lod,'fbx':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
             'triangles':count,'body_triangles':entry['body_triangles'],'outline_triangles':entry['outline_triangles'],
             'materials':data['materials'],'bone_count':asset['bone_count'],'bounds_m':{'min':expected.min(0).tolist(),'max':expected.max(0).tolist()},
             'root_scale':list(rig.scale) if rig else None})
        print('SSF_EXPORTED',key,lod,count,asset['lods'][-1]['root_scale'],flush=True)
        # Keep Blender's reserved Armature name for each FBX (UE strips it).
        if rig:rig.name='AlreadyExported_'+key+'_LOD'+str(lod)
    report['assets'].append(asset);dump()
report['success']=True;dump()
assert hashlib.sha256((P/'SSF_Production_B_v1.blend').read_bytes()).hexdigest()==report['source_sha256']
print('SSF_UE_EXPORT_COMPLETE',flush=True)
