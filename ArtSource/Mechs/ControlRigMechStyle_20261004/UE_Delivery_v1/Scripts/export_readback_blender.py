"""Frozen B-v4 -> centimeter FBX, four explicit LODs, then full Blender readback."""
import bpy, json, hashlib, math
import numpy as np
from pathlib import Path
from mathutils import Matrix
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
O=R/'UE_Delivery_v1'; F=O/'FBX'; F.mkdir(parents=True,exist_ok=True)
SOURCE=R/'Production_B_v4/ControlRigMech_B_v4_Production.blend'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()=='bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b'
assert json.loads((R/'review_decisions.json').read_text(encoding='utf-8'))['B']['status']=='approved_for_UE_import'
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
s=bpy.context.scene; rig=bpy.data.objects['Armature']; rig.animation_data_clear(); rig.data.pose_position='REST'
for p in rig.pose.bones:p.matrix_basis=Matrix.Identity(4)
for c in bpy.data.collections:c.hide_viewport=False
rig.name='SOURCE_Armature'
ex=bpy.data.scenes.new('FBX_EXPORT_ONLY'); ex.unit_settings.system='METRIC'; ex.unit_settings.scale_length=.01
bpy.context.window.scene=ex
arm=rig.copy(); arm.data=rig.data.copy(); arm.name='Armature'; arm.animation_data_clear(); arm.data.pose_position='REST'; arm.data.transform(Matrix.Scale(100,4)); arm.hide_viewport=False; ex.collection.objects.link(arm); arm.hide_set(False)
report={'success':False,'source_blend_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'export_units':'cm, object and root scale 1; original UE animation copies remain native','vertex_color_encoding':'FBX RGB sRGB palette; UE shader explicitly decodes to linear','lods':[]}
original_bones={b.name:{'parent':b.parent.name if b.parent else '', 'world_m':[list(row) for row in b.matrix_local]} for b in rig.data.bones}
refs=[]
def array(data,prop,dims):
    v=np.empty(len(data)*dims,dtype=np.float32);data.foreach_get(prop,v);return v.reshape((-1,dims))
def snapshot(ob):
    me=ob.data; me.calc_loop_triangles()
    return {'points_cm':array(me.vertices,'co',3),'normals':np.asarray([tuple(n.vector) for n in me.corner_normals],dtype=np.float32),'uv':[array(u.data,'uv',2) for u in me.uv_layers],'color':array(me.color_attributes.active_color.data,'color_srgb',4),'weights':[{ob.vertex_groups[g.group].name:float(g.weight) for g in v.groups if g.weight>1e-7} for v in me.vertices],'tris':len(me.loop_triangles),'sections':dict((i,sum(p.material_index==i for p in me.polygons)) for i in range(len(me.materials))),'loopverts':np.asarray([l.vertex_index for l in me.loops],dtype=np.int32)}
for lod in range(4):
    orig=bpy.data.objects[f'ControlRigMech_LOD{lod}_Body']; body=orig.copy();body.data=orig.data.copy();body.name=f'SKM_ControlRigMech_LOD{lod}';ex.collection.objects.link(body);body.hide_viewport=False;body.hide_render=False;body.hide_set(False)
    for m in list(body.modifiers):body.modifiers.remove(m)
    body.parent=None;body.matrix_world=Matrix.Identity(4)
    old=body.data.color_attributes['GuLi_PaletteLinear'];srgb=array(old.data,'color_srgb',4)
    body.data.color_attributes.remove(old);color=body.data.color_attributes.new('GuLi_PaletteSRGB','BYTE_COLOR','CORNER');color.data.foreach_set('color_srgb',srgb.ravel());body.data.color_attributes.active_color=color;body.data.color_attributes.render_color_index=0
    body.data.materials.clear();body.data.materials.append(bpy.data.materials.new(f'Body_LOD{lod}'))
    for p in body.data.polygons:p.material_index=0
    if lod<3:
        outline=bpy.data.objects[f'ControlRigMech_B_v4_LOD{lod}_Outline'].copy();outline.data=outline.data.copy();ex.collection.objects.link(outline);outline.hide_viewport=False;outline.hide_set(False);outline.parent=None;outline.matrix_world=Matrix.Identity(4)
        for m in list(outline.modifiers):
            if m.type=='ARMATURE':outline.modifiers.remove(m)
        for g in list(outline.vertex_groups):
            if g.name not in original_bones:
                assert not any(x.group==g.index and x.weight>1e-7 for v in outline.data.vertices for x in v.groups)
                outline.vertex_groups.remove(g)
        bpy.ops.object.select_all(action='DESELECT');outline.select_set(True);bpy.context.view_layer.objects.active=outline
        for m in list(outline.modifiers):bpy.ops.object.modifier_apply(modifier=m.name)
        for u in body.data.uv_layers:
            layer=outline.data.uv_layers.new(name=u.name)
            layer.data.foreach_set('uv',np.tile((8.,8.) if 'Distance' in u.name else (0.,0.),(len(layer.data),1)).ravel())
        color=outline.data.color_attributes.new('GuLi_PaletteSRGB','BYTE_COLOR','CORNER');color.data.foreach_set('color_srgb',np.tile((27/255,36/255,34/255,1.),(len(color.data),1)).ravel());outline.data.color_attributes.active_color=color;outline.data.color_attributes.render_color_index=0
        outline.data.materials.clear();outline.data.materials.append(bpy.data.materials.get('UE_Outline') or bpy.data.materials.new('UE_Outline'))
        for p in outline.data.polygons:p.material_index=0
        body.select_set(True);bpy.context.view_layer.objects.active=body;bpy.ops.object.join()
    body.data.transform(Matrix.Scale(100,4));body.data.update()
    # Zero-area triangles cannot survive Blender/FBX/UE validation. Remove only
    # mathematically collapsed faces from the export copy, retaining every vertex.
    me=body.data;coords=array(me.vertices,'co',3).astype(np.float64)
    ids=np.asarray([tuple(p.vertices) for p in me.polygons],dtype=np.int32)
    areas=np.linalg.norm(np.cross(coords[ids[:,1]]-coords[ids[:,0]],coords[ids[:,2]]-coords[ids[:,0]]),axis=1)
    seen=set();keep=[];duplicates=0;zero_area=0
    for i,p in enumerate(me.polygons):
        if areas[i]<=1e-12:zero_area+=1;continue
        key=tuple(sorted(p.vertices))
        if key in seen:duplicates+=1;continue
        seen.add(key);keep.append(i)
    keep=np.asarray(keep,dtype=np.int32);removed=len(me.polygons)-len(keep)
    if removed:
        oldnorms=np.asarray([tuple(x.vector) for x in me.corner_normals]); loops=np.asarray([l for i in keep for l in me.polygons[int(i)].loop_indices],dtype=np.int32)
        uvs=[(u.name,array(u.data,'uv',2)[loops]) for u in me.uv_layers];colours=array(me.color_attributes.active_color.data,'color_srgb',4)[loops]
        weights=[{body.vertex_groups[g.group].name:float(g.weight) for g in v.groups} for v in me.vertices]
        polys=[me.polygons[int(i)] for i in keep];mats=list(me.materials);indices=[p.material_index for p in polys];smooth=[p.use_smooth for p in polys]
        clean=bpy.data.meshes.new(me.name+'_ZeroAreaClean');clean.from_pydata(coords,[],[list(p.vertices) for p in polys]);clean.update()
        for mat in mats:clean.materials.append(mat)
        for p,i,sm in zip(clean.polygons,indices,smooth):p.material_index=i;p.use_smooth=sm
        for name,data in uvs:clean.uv_layers.new(name=name).data.foreach_set('uv',data.ravel())
        c=clean.color_attributes.new('GuLi_PaletteSRGB','BYTE_COLOR','CORNER');c.data.foreach_set('color_srgb',colours.ravel());clean.color_attributes.active_color=c;clean.color_attributes.render_color_index=0
        clean.normals_split_custom_set(oldnorms[loops]);names=[g.name for g in body.vertex_groups];body.data=clean
        for name in names:
            if name not in body.vertex_groups:body.vertex_groups.new(name=name)
        for vi,w in enumerate(weights):
            for name,value in w.items():body.vertex_groups[name].add([vi],value,'REPLACE')
        print('EXPORT_REDUNDANCY_CLEANUP',lod,{'zero_area':zero_area,'duplicate_faces':duplicates},flush=True)
    mod=body.modifiers.new('Original152BoneBinding','ARMATURE');mod.object=arm;body.parent=arm;body.matrix_parent_inverse=Matrix.Identity(4)
    bpy.ops.object.select_all(action='DESELECT');body.select_set(True);arm.select_set(True);bpy.context.view_layer.objects.active=body
    ref=snapshot(body);refs.append(ref)
    f=F/f'SKM_ControlRigMech_B_v4_LOD{lod}.fbx'
    bpy.ops.export_scene.fbx(filepath=str(f),use_selection=True,object_types={'MESH','ARMATURE'},global_scale=1.,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',use_space_transform=True,bake_space_transform=False,axis_forward='-Y',axis_up='Z',primary_bone_axis='Y',secondary_bone_axis='X',add_leaf_bones=False,use_armature_deform_only=False,use_mesh_modifiers=True,mesh_smooth_type='FACE',use_tspace=True,colors_type='SRGB',bake_anim=False,path_mode='STRIP',use_custom_props=False)
    row={'lod':lod,'fbx_path':str(f.relative_to(O)),'fbx_sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'triangles':ref['tris'],'sections':ref['sections'],'export_cleanup':{'zero_area_triangles_removed':zero_area,'duplicate_faces_removed':duplicates,'no_vertices_moved':True},'dimensions_m':[float(v/100) for v in body.dimensions],'vertex_groups':len(body.vertex_groups),'uv_channels':len(body.data.uv_layers)}
    report['lods'].append(row);print('EXPORTED',json.dumps(row),flush=True)
    body.select_set(False);arm.select_set(False);body.hide_set(True)
# Read each FBX in a clean scene; compare physical positions, all UVs, colors,
# loop normals and skin weights, independent of vertex splitting/reordering.
for lod,row in enumerate(report['lods']):
    read=bpy.data.scenes.new(f'READBACK_{lod}');read.unit_settings.system='METRIC';read.unit_settings.scale_length=.01;bpy.context.window.scene=read
    before=set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(O/row['fbx_path']),use_custom_normals=True,use_anim=False,automatic_bone_orientation=False)
    objects=set(bpy.data.objects)-before;mesh=next(o for o in objects if o.type=='MESH');rbarm=next(o for o in objects if o.type=='ARMATURE')
    rb=snapshot(mesh);ref=refs[lod]
    assert rb['tris']==ref['tris'],(lod,rb['tris'],ref['tris'])
    assert len(rb['uv'])==3 and len(rbarm.data.bones)==152
    assert {b.name:(b.parent.name if b.parent else '') for b in rbarm.data.bones}=={n:b['parent'] for n,b in original_bones.items()}
    # FBX keeps the wedge stream ordered inside each polygon. Compare corners
    # rather than source vertex IDs, since normals/UV seams may split vertices.
    a=ref['points_cm'][ref['loopverts']];b=rb['points_cm'][rb['loopverts']]
    point_error=float(np.max(np.linalg.norm(a-b,axis=1)))/100
    uv_error=[float(np.max(np.abs(x-y))) for x,y in zip(ref['uv'],rb['uv'])]
    color_error=float(np.max(np.abs(ref['color']-rb['color'])))
    na=ref['normals'].astype(np.float64);nb=rb['normals'].astype(np.float64)
    dots=np.clip(np.sum(na*nb,axis=1)/(np.linalg.norm(na,axis=1)*np.linalg.norm(nb,axis=1)),-1,1)
    normal_error=float(np.max(np.arccos(dots)))*180/math.pi
    weights_error=0.
    for ia,ib in zip(ref['loopverts'],rb['loopverts']):
        wa,wb=ref['weights'][int(ia)],rb['weights'][int(ib)]
        weights_error=max(weights_error,max((abs(wa.get(n,0)-wb.get(n,0)) for n in set(wa)|set(wb)),default=0))
    bone_error=max(float(np.max(np.abs(np.asarray([list(r) for r in rbarm.matrix_world@bone.matrix_local])-np.asarray([list(r) for r in arm.matrix_world@arm.data.bones[bone.name].matrix_local])))) for bone in rbarm.data.bones)
    normal_p99=float(np.percentile(np.arccos(dots)*180/math.pi,99))
    assert point_error<.00002 and max(uv_error)<.00001 and color_error<.001 and normal_error<1 and normal_p99<.05 and weights_error<.00001 and bone_error<.002,(lod,point_error,uv_error,color_error,normal_error,normal_p99,weights_error,bone_error)
    row['readback']={'passed':True,'all_corner_position_max_error_m':point_error,'uv_max_errors':uv_error,'palette_srgb_max_error':color_error,'corner_normal_max_error_degrees':normal_error,'corner_normal_p99_error_degrees':normal_p99,'normal_roundtrip_note':'Blender packed custom normals are re-encoded on FBX readback; maximum under 1 degree, P99 under 0.05 degree','all_corner_weight_max_error':weights_error,'reference_bone_matrix_max_error_cm':bone_error,'bone_count':152,'root_object_scale':list(rbarm.scale)}
    print('READBACK',json.dumps(row['readback']),flush=True)
report['success']=True
(O/'blender_export_readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==report['source_blend_sha256']
print('FBX_READBACK_PASSED',flush=True)
