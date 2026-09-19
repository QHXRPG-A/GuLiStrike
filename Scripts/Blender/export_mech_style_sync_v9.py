"""Export the authorized light v9 and full-resolution red-panel Spider.

Body paint, internal ink and emissive are portable inputs, never baked lighting.
The authored expanded ink shells are exported as their own material section.
"""
import bpy, json, hashlib, math, sys, shutil
import numpy as np
from pathlib import Path
from mathutils import Matrix, Quaternion

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'UE_StyleSync_v9';(OUT/'FBX').mkdir(parents=True,exist_ok=True);(OUT/'Textures').mkdir(exist_ok=True)
snapshot=json.loads((ROOT/'Source/Production_v1/source_snapshot.json').read_text(encoding='utf-8'))
report={'success':False,'source':bpy.data.filepath,'source_sha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),'parts':{},
        'style':{'light_ue':[.35,.55,.76],'thresholds':[.13,.56],'tones':[[.28,.34,.45],[.64,.70,.80],[1.15,1.10,1.02]],'ink_srgb':'152632'}}
basis=Matrix.Diagonal((1,-1,1,1))
frozen={}
for key in ('Mech_Lightest','SpiderMech'):
    bpy.context.window.scene=bpy.data.scenes['Review_'+key]
    col=bpy.data.collections['WORK_'+key];col.hide_viewport=col.hide_render=False;bpy.context.view_layer.update()
    frozen.update({o:o.matrix_world.copy() for o in col.objects})
scene=bpy.data.scenes.new('UE_StyleSync_Export');bpy.context.window.scene=scene
scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=.01
scene.render.engine='CYCLES';scene.cycles.samples=1;scene.cycles.device='GPU'
scene.render.bake.margin=8;scene.render.bake.use_clear=True;scene.view_settings.view_transform='Standard'

def select(objects,active):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.hide_select=False;o.select_set(True)
    bpy.context.view_layer.objects.active=active

def transform(t,units=.01):
    q=t['rotation']
    return Matrix.Translation((t['location'][0]*units,-t['location'][1]*units,t['location'][2]*units))@(basis@Quaternion((q[3],q[0],q[1],q[2])).to_matrix().to_4x4()@basis)@Matrix.Diagonal((*t['scale'],1))

def copy_meshes(collection,component,part_world):
    body=[];ink=[]
    for old in collection.objects:
        if old.type!='MESH' or (component is not None and old.get('source_component')!=component):continue
        o=old.copy();o.data=old.data.copy();scene.collection.objects.link(o)
        o.parent=None;o.constraints.clear();o.matrix_world=part_world.inverted()@frozen[old]
        o.hide_viewport=o.hide_render=False;o.hide_set(False);o.hide_select=False
        select([o],o)
        for mod in list(o.modifiers):
            if mod.type=='ARMATURE':o.modifiers.remove(mod)
            else:bpy.ops.object.modifier_apply(modifier=mod.name)
        if old.get('ink_layer'):ink.append(o);continue
        for slot in o.material_slots:
            slot.material=slot.material.copy();mat=slot.material
            if o.data.uv_layers:
                uv=mat.node_tree.nodes.new('ShaderNodeUVMap');uv.uv_map=o.data.uv_layers[0].name
                for n in mat.node_tree.nodes:
                    if n.type=='TEX_IMAGE':mat.node_tree.links.new(uv.outputs['UV'],n.inputs['Vector'])
        body.append(o)
    return body,ink

def join(objects,name,normalize=True):
    select(objects,objects[0])
    if len(objects)>1:bpy.ops.object.join()
    o=bpy.context.object;o.name=name
    if normalize:
        o.data.transform(Matrix.Scale(100,4)@o.matrix_world);o.matrix_world=Matrix.Identity(4)
    return o

def surface_source(mat,role):
    if role=='BaseColor':return mat.node_tree.nodes['Paint_x_Three_Tones'].inputs[1],1.
    if role=='LineMask':
        ink=mat.node_tree.nodes.get('Fine_Ink_Overlay')
        return (ink.inputs[0],1.) if ink else (None,0.)
    p=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    return p.inputs['Emission Color'],p.inputs['Emission Strength'].default_value

def bake(o,name,role,size,per_slot=False):
    images=[];paths=[]
    for i,mat in enumerate(o.data.materials):
        tag=f'{name}_{i:02d}_{role}' if per_slot else f'{name}_{role}'
        if i==0 or per_slot:
            im=bpy.data.images.new(tag,size,size,alpha=False)
            im.colorspace_settings.name='Non-Color' if role=='LineMask' else 'sRGB'
            images.append(im);paths.append(OUT/'Textures'/(tag+'.png'))
        else:im=images[0]
        n=mat.node_tree.nodes;l=mat.node_tree.links
        src,strength=surface_source(mat,role);emit=n.new('ShaderNodeEmission')
        emit.inputs['Color'].default_value=(0,0,0,1);emit.inputs['Strength'].default_value=strength
        if src is not None:
            if src.is_linked:l.new(src.links[0].from_socket,emit.inputs['Color'])
            else:
                value=src.default_value
                emit.inputs['Color'].default_value=(value,value,value,1) if isinstance(value,(float,int)) else value
        output=next(n for n in n if n.type=='OUTPUT_MATERIAL');l.new(emit.outputs[0],output.inputs['Surface'])
        tex=n.new('ShaderNodeTexImage');tex.image=im;n.active=tex
    select([o],o);bpy.ops.object.bake(type='EMIT')
    for im,path in zip(images,paths):im.filepath_raw=str(path);im.file_format='PNG';im.save()
    print('BAKED',name,role,flush=True)
    return [str(p) for p in paths]

def flat_material(name):
    m=bpy.data.materials.new(name);m.diffuse_color=(.4,.4,.4,1);return m

def build_rig(key):
    rig=bpy.data.objects.new('Armature',bpy.data.armatures.new(key+'_OriginalReference'))
    scene.collection.objects.link(rig);select([rig],rig);bpy.ops.object.mode_set(mode='EDIT')
    for ref in snapshot['meshes'][key]['bones']:
        e=rig.data.edit_bones.new(ref['name']);e.head=(0,0,0);e.tail=(0,5,0)
        e.matrix=transform(ref['global'],1.)
        if ref['parent']:e.parent=rig.data.edit_bones[ref['parent']]
    bpy.ops.object.mode_set(mode='OBJECT');return rig

parts=[('Legs','SkeletalMeshComponent0','Mech_Legs_Lt'),('Armor','Cockpit_Jet',None),('Shoulder','HalfShoulder_Box',None),('Machinegun','Weapons_Machinegun_lvl1','Machinegun_lvl1'),('SpiderMech',None,'SpiderMech')]
requested=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [p[0] for p in parts]
if set(requested)!=set(p[0] for p in parts):
    report['parts']=json.loads((OUT/'export_report.json').read_text())['parts']
for name,component,rig_key in parts:
    if name not in requested:continue
    spider=name=='SpiderMech'
    collection=bpy.data.collections['WORK_SpiderMech' if spider else 'WORK_Mech_Lightest']
    part_world=Matrix.Identity(4) if spider else transform(next(c for c in snapshot['assemblies']['Mech_Lightest'] if c['name']==component)['world_transform'])
    bodies,outlines=copy_meshes(collection,component,part_world)
    body=join(bodies,name+'_Body');body.data.calc_loop_triangles();body_tri=len(body.data.loop_triangles)
    if spider:
        assert body_tri==839778
        masks=[]
        for i,mat in enumerate(body.data.materials):
            src=Path(mat.node_tree.nodes['Internal_Structure_Ink'].image.filepath)
            dest=OUT/'Textures'/f'SpiderMech_{i:02d}_LineMask.png';shutil.copy2(src,dest);masks.append(str(dest))
        maps={'BaseColor':bake(body,name,'BaseColor',2048,True),'LineMask':masks}
    else:
        body.data.uv_layers.new(name='GameAtlas');body.data.uv_layers.active=body.data.uv_layers['GameAtlas'];body.data.uv_layers['GameAtlas'].active_render=True
        select([body],body);bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.006)
        bpy.ops.object.mode_set(mode='OBJECT')
        maps={role:bake(body,name,role,2048) for role in ('BaseColor','LineMask','Emissive')}
        for uv in list(body.data.uv_layers):
            if uv.name!='GameAtlas':body.data.uv_layers.remove(uv)
    count=len(body.data.materials) if spider else 1
    material_ids=np.empty(len(body.data.polygons),np.int32)
    body.data.polygons.foreach_get('material_index',material_ids)
    body.data.materials.clear()
    for i in range(count):body.data.materials.append(flat_material(f'Export_{name}_Surface_{i:02d}'))
    if spider:body.data.polygons.foreach_set('material_index',material_ids)
    else:
        for p in body.data.polygons:p.material_index=0
    edge=join(outlines,name+'_Ink');edge.data.calc_loop_triangles();ink_tri=len(edge.data.loop_triangles)
    edge.data.materials.clear();edge.data.materials.append(flat_material('Export_Ink_Backfaces'))
    for p in edge.data.polygons:p.material_index=0
    # Use the same UV layer name when joining; the outline does not sample it.
    for uv in list(edge.data.uv_layers):edge.data.uv_layers.remove(uv)
    edge.data.uv_layers.new(name=body.data.uv_layers[0].name)
    # Body and ink are already in centimetres. Joining those must not repeat
    # the metre-to-centimetre normalization used on source object copies.
    expected_bounds={k:[fn(v.co[i] for obj in (body,edge) for v in obj.data.vertices) for i in range(3)] for k,fn in [('min',min),('max',max)]}
    mesh=join([body,edge],'Styled_'+name,normalize=False)
    assert all(abs(fn(v.co[i] for v in mesh.data.vertices)-expected_bounds[k][i])<.001 for k,fn in [('min',min),('max',max)] for i in range(3))
    rig=build_rig(rig_key) if rig_key else None
    if rig:
        mod=mesh.modifiers.new('Original_UE_Skeleton','ARMATURE');mod.object=rig
    selected=[mesh]+([rig] if rig else [])
    select(selected,mesh)
    path=OUT/'FBX'/(name+'.fbx')
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','ARMATURE'},add_leaf_bones=False,
        use_armature_deform_only=False,bake_anim=False,axis_forward='-Y',axis_up='Z',apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_NONE',use_mesh_modifiers=True,mesh_smooth_type='FACE',colors_type='NONE')
    if rig:rig.name='Exported_'+name+'_Armature'
    report['parts'][name]={'fbx':str(path),'fbx_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'body_triangles':body_tri,'outline_triangles':ink_tri,
        'bones':len(rig.data.bones) if rig else 0,'body_material_slots':count,'ink_slot':count,'textures':maps,
        'bounds_cm':{'min':[min(v.co[i] for v in mesh.data.vertices) for i in range(3)],'max':[max(v.co[i] for v in mesh.data.vertices) for i in range(3)]}}
    (OUT/'export_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('EXPORTED_STYLE',name,body_tri,ink_tri,flush=True)
    for o in selected:scene.collection.objects.unlink(o)
report['success']=True
(OUT/'export_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('MECH_STYLE_SYNC_EXPORT_READY',flush=True)
