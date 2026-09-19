"""Export the explicitly selected Lightest production model; run in background Blender."""
import bpy, json, hashlib, math
from pathlib import Path
from mathutils import Matrix, Vector, Quaternion

ROOT = Path('D:/UE5.7/test1')
SOURCE = ROOT/'ArtSource/Mechs/StyleUnification_20260919'
OUT = SOURCE/'GroundMech_UE'
OUT.mkdir(exist_ok=True)
snapshot = json.loads((SOURCE/'Source/Production_v1/source_snapshot.json').read_text(encoding='utf-8'))
source_collection = bpy.data.collections['WORK_Mech_Lightest']
bpy.context.window.scene=bpy.data.scenes['Review_Mech_Lightest']
bpy.context.view_layer.update()
frozen_world={o:o.matrix_world.copy() for o in source_collection.objects}
scene = bpy.data.scenes.new('GroundMech_Export')
bpy.context.window.scene = scene
scene.unit_settings.system='METRIC'
scene.unit_settings.scale_length=.01
scene.render.engine = 'CYCLES'
scene.cycles.samples = 1
scene.render.bake.margin = 8
scene.render.bake.use_clear = True
scene.view_settings.view_transform = 'Standard'
report = {'source': bpy.data.filepath, 'source_sha256': hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(), 'parts': {}}

def select(objects, active):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects: o.select_set(True)
    bpy.context.view_layer.objects.active = active

for component, name in [('SkeletalMeshComponent0','Legs'),('Cockpit_Jet','Armor'),('HalfShoulder_Box','Shoulder'),('Weapons_Machinegun_lvl1','Machinegun')]:
    assembly = next(c for c in snapshot['assemblies']['Mech_Lightest'] if c['name']==component)
    t = assembly['world_transform']; q=t['rotation']; b=Matrix.Diagonal((1,-1,1,1))
    part_world = Matrix.Translation((t['location'][0]*.01,-t['location'][1]*.01,t['location'][2]*.01)) @ (b@Quaternion((q[3],q[0],q[1],q[2])).to_matrix().to_4x4()@b) @ Matrix.Diagonal((*t['scale'],1))
    copies = {}
    for old in source_collection.objects:
        if old.get('source_component') != component or old.type not in ('MESH','ARMATURE'): continue
        new = old.copy(); new.data = old.data.copy(); scene.collection.objects.link(new)
        world = frozen_world[old]; new.parent=None; new.constraints.clear()
        new.matrix_world = part_world.inverted() @ world
        new.hide_viewport=False; new.hide_render=False; new.hide_set(False)
        copies[old] = new
    rig = next((o for o in copies.values() if o.type=='ARMATURE'),None)
    if rig:
        rig.name='Armature'; rig.data.pose_position='REST'
        for bone in rig.pose.bones: bone.matrix_basis.identity()
    meshes = [o for o in copies.values() if o.type=='MESH']
    for mesh in meshes:
        select([mesh],mesh)
        for mod in list(mesh.modifiers):
            if mod.type=='ARMATURE': mod.object=rig; mod.show_viewport=False; mod.show_render=False
            else: bpy.ops.object.modifier_apply(modifier=mod.name)
        for slot in mesh.material_slots:
            slot.material=slot.material.copy()
            mat=slot.material
            # Freeze source UV lookup before the new atlas becomes the active render UV.
            if mesh.data.uv_layers:
                uv=mat.node_tree.nodes.new('ShaderNodeUVMap'); uv.uv_map=mesh.data.uv_layers.active.name
                for node in mat.node_tree.nodes:
                    if node.type=='TEX_IMAGE': mat.node_tree.links.new(uv.outputs['UV'],node.inputs['Vector'])
    select(meshes,meshes[0]); bpy.ops.object.join(); mesh=bpy.context.object; mesh.name='GroundMech_'+name
    # Normalize geometry to centimetres and regenerate the export rig from the exact UE
    # reference pose. This avoids Blender's FBX armature-object unit compensation.
    bpy.context.view_layer.update()
    mesh.data.transform(Matrix.Scale(100,4)@mesh.matrix_world)
    mesh.matrix_world=Matrix.Identity(4)
    if rig:
        for mod in list(mesh.modifiers):
            if mod.type=='ARMATURE':mesh.modifiers.remove(mod)
        bpy.data.objects.remove(rig,do_unlink=True)
        rig=bpy.data.objects.new('Armature',bpy.data.armatures.new(name+'_ExportSkeleton'))
        scene.collection.objects.link(rig);select([rig],rig);bpy.ops.object.mode_set(mode='EDIT')
        key='Mech_Legs_Lt' if name=='Legs' else 'Machinegun_lvl1'
        for bone in snapshot['meshes'][key]['bones']:
            edit=rig.data.edit_bones.new(bone['name']);tbone=bone['global'];qbone=tbone['rotation']
            transform=Matrix.Translation((tbone['location'][0],-tbone['location'][1],tbone['location'][2])) @ (b@Quaternion((qbone[3],qbone[0],qbone[1],qbone[2])).to_matrix().to_4x4()@b)
            # A new edit bone has zero length: establish its axis before assigning
            # the reference matrix, otherwise Blender discards the swing rotation.
            edit.head=(0,0,0);edit.tail=(0,5,0);edit.matrix=transform
            if bone['parent']:edit.parent=rig.data.edit_bones[bone['parent']]
        bpy.ops.object.mode_set(mode='OBJECT')
        mod=mesh.modifiers.new('Original_UE_Skeleton','ARMATURE');mod.object=rig;mod.show_viewport=False;mod.show_render=False
    select([mesh],mesh)
    mesh.data.uv_layers.new(name='GameAtlas'); mesh.data.uv_layers.active=mesh.data.uv_layers['GameAtlas']; mesh.data.uv_layers['GameAtlas'].active_render=True
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.015)
    bpy.ops.object.mode_set(mode='OBJECT')
    source_materials=list(mesh.data.materials)
    maps={}
    for channel in ['BaseColor','ORM','Emissive']:
        image=bpy.data.images.new(name+'_'+channel,1024,1024,alpha=False)
        image.colorspace_settings.name='sRGB' if channel!='ORM' else 'Non-Color'
        for mat in source_materials:
            nodes=mat.node_tree.nodes; links=mat.node_tree.links
            p=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
            output=next(n for n in nodes if n.type=='OUTPUT_MATERIAL')
            emit=nodes.new('ShaderNodeEmission')
            if channel=='ORM':
                emit.inputs['Color'].default_value=(1,p.inputs['Roughness'].default_value,p.inputs['Metallic'].default_value,1)
            else:
                source=p.inputs['Base Color' if channel=='BaseColor' else 'Emission Color']
                if source.is_linked: links.new(source.links[0].from_socket,emit.inputs['Color'])
                else: emit.inputs['Color'].default_value=source.default_value
                if channel=='Emissive': emit.inputs['Strength'].default_value=p.inputs['Emission Strength'].default_value
            links.new(emit.outputs[0],output.inputs['Surface'])
            target=nodes.new('ShaderNodeTexImage'); target.image=image; nodes.active=target
        select([mesh],mesh)
        bpy.ops.object.bake(type='EMIT')
        image.filepath_raw=str(OUT/(name+'_'+channel+'.png')); image.file_format='PNG'; image.save()
        maps[channel]=image.filepath_raw
    final=bpy.data.materials.new('M_GroundMech_'+name); final.use_nodes=True
    nodes=final.node_tree.nodes; links=final.node_tree.links; p=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
    for channel,socket in [('BaseColor','Base Color'),('Emissive','Emission Color')]:
        tex=nodes.new('ShaderNodeTexImage'); tex.image=bpy.data.images.load(maps[channel]); links.new(tex.outputs['Color'],p.inputs[socket])
    p.inputs['Emission Strength'].default_value=1; p.inputs['Roughness'].default_value=.58; p.inputs['Metallic'].default_value=.25
    mesh.data.materials.clear(); mesh.data.materials.append(final)
    for poly in mesh.data.polygons: poly.material_index=0
    # Export atlas as UV0. Source UVs have already been baked into the maps.
    for uv in list(mesh.data.uv_layers):
        if uv.name!='GameAtlas': mesh.data.uv_layers.remove(uv)
    for mod in mesh.modifiers:
        if mod.type=='ARMATURE': mod.show_viewport=True; mod.show_render=True
    selected=[mesh]+([rig] if rig else [])
    select(selected,mesh)
    path=OUT/(name+'.fbx')
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','ARMATURE'},add_leaf_bones=False,use_armature_deform_only=False,bake_anim=False,axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',use_mesh_modifiers=True,mesh_smooth_type='FACE')
    if rig: rig.name='Exported_'+name+'_Armature'
    mesh.data.calc_loop_triangles()
    report['parts'][name]={'fbx':str(path),'triangles':len(mesh.data.loop_triangles),'bones':len(rig.data.bones) if rig else 0,'textures':maps,'matrix':list(map(list,mesh.matrix_world))}
    print('EXPORTED',name,report['parts'][name]['triangles'],flush=True)
    for o in selected: scene.collection.objects.unlink(o)
report['success']=True
(OUT/'export_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('GROUND_EXPORT_SUCCESS',flush=True)
