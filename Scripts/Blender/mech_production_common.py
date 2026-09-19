import bpy,json,math
from pathlib import Path
from mathutils import Vector,Matrix,Quaternion
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Production_v1'
SNAP=json.loads((ROOT/'Source/Production_v1/source_snapshot.json').read_text(encoding='utf-8'))
IMP=json.loads((OUT/'import_report.json').read_text(encoding='utf-8'))
def objects(key,source=False):return list(bpy.data.collections[('SRC_' if source else 'WORK_')+key].objects)
def meshes(key,source=False):return [o for o in objects(key,source) if o.type=='MESH']
def active(o):
    if bpy.context.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT');o.hide_set(False);o.select_set(True);bpy.context.view_layer.objects.active=o
def save():bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Mechs_Style_SourceBased_v1.blend'),check_existing=False)
def image_for(path,noncolor=False):
    img=bpy.data.images.load(str(ROOT/SNAP['textures'][path]['export']['path']),check_existing=True)
    img.colorspace_settings.name='Non-Color' if noncolor else 'sRGB'
    return img
def principled(mat,data,style=False,key=''):
    mat.use_nodes=True;n=mat.node_tree.nodes;l=mat.node_tree.links;n.clear()
    p=n.new('ShaderNodeBsdfPrincipled');out=n.new('ShaderNodeOutputMaterial');out.location=(520,0);p.location=(200,0);l.new(p.outputs['BSDF'],out.inputs['Surface'])
    p.inputs['Roughness'].default_value=.5;p.inputs['Specular IOR Level'].default_value=.5
    mat['ue_shading_model']=data['shading_model'];mat['source_material']=data['name']
    for channel,socket in [('BASE_COLOR','Base Color'),('METALLIC','Metallic'),('ROUGHNESS','Roughness'),('SPECULAR','Specular IOR Level'),('EMISSIVE_COLOR','Emission Color'),('NORMAL','Normal')]:
        e=data['inputs'].get(channel)
        if not e:continue
        if e.get('texture'):
            t=n.new('ShaderNodeTexImage');t.label=channel;t.name='SOURCE_'+channel;t.image=image_for(e['texture'],channel not in ('BASE_COLOR','EMISSIVE_COLOR'));t.location=(-650,-180*len(n))
            output=t.outputs['Color']
            if channel=='NORMAL':
                separate=n.new('ShaderNodeSeparateColor');combine=n.new('ShaderNodeCombineColor');invert=n.new('ShaderNodeMath');invert.operation='SUBTRACT';invert.inputs[0].default_value=1
                l.new(output,separate.inputs[0]);l.new(separate.outputs[0],combine.inputs[0]);l.new(separate.outputs[1],invert.inputs[1]);l.new(invert.outputs[0],combine.inputs[1]);l.new(separate.outputs[2],combine.inputs[2])
                normal=n.new('ShaderNodeNormalMap');normal.inputs['Strength'].default_value=.25 if style else 1.0;l.new(combine.outputs[0],normal.inputs['Color']);output=normal.outputs[0]
            if channel=='BASE_COLOR' and style and key in ('Mecha_01','FireWeapon_01','MissileWeapon_01'):
                hsv=n.new('ShaderNodeHueSaturation');hsv.inputs['Saturation'].default_value=.35 if key!='MissileWeapon_01' else .45;hsv.name='ART_RedBlue_Desaturation';l.new(output,hsv.inputs['Color']);output=hsv.outputs[0]
            l.new(output,p.inputs[socket])
            if channel=='EMISSIVE_COLOR':p.inputs['Emission Strength'].default_value=1
        elif 'r' in e:p.inputs[socket].default_value=e['r']
        elif 'default_value' in e:p.inputs[socket].default_value=e['default_value']
def setup_materials():
    for key,row in SNAP['meshes'].items():
        for src in [True,False]:
            for o in meshes(key,src):
                for slot in o.material_slots:
                    mat=slot.material
                    matches=[SNAP['materials'][x['path']] for x in row['materials'] if x['path'] and SNAP['materials'][x['path']]['name'] in mat.name]
                    data=matches[0] if matches else SNAP['materials'][row['materials'][slot.slot_index]['path']]
                    principled(mat,data,not src,key)
def stage():
    scene=bpy.context.scene
    scene.render.engine='CYCLES';scene.cycles.samples=24;scene.cycles.use_denoising=True
    try:
        prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
        for device in prefs.devices:device.use=device.type=='OPTIX'
        scene.cycles.device='GPU'
    except Exception:pass
    scene.render.resolution_x=1100;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.film_transparent=False
    scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast';scene.view_settings.exposure=-.7
    if not scene.world:scene.world=bpy.data.worlds.new('Mech_Studio_World')
    scene.world.use_nodes=True
    bg=next(n for n in scene.world.node_tree.nodes if n.type=='BACKGROUND');bg.inputs[0].default_value=(.78,.82,.9,1);bg.inputs[1].default_value=.25
    col=bpy.data.collections.get('90_STUDIO') or bpy.data.collections.new('90_STUDIO')
    if col.name not in [c.name for c in scene.collection.children]:scene.collection.children.link(col)
    if not bpy.data.objects.get('Studio_Floor'):
        bpy.ops.mesh.primitive_plane_add(size=200);floor=bpy.context.object;floor.name='Studio_Floor'
        for c in list(floor.users_collection):c.objects.unlink(floor)
        col.objects.link(floor);mat=bpy.data.materials.new('Studio_Ivory');mat.diffuse_color=(.72,.70,.65,1);mat.use_nodes=True;p=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Base Color'].default_value=(.72,.70,.65,1);p.inputs['Roughness'].default_value=.82;floor.data.materials.append(mat)
        for name,pos,power,size in [('Key',(5,-6,9),850,5),('Fill',(-5,-2,5),400,6),('Rim',(1,5,8),950,5)]:
            data=bpy.data.lights.new('Studio_'+name,'AREA');data.energy=power;data.shape='DISK';data.size=size;o=bpy.data.objects.new('Studio_'+name,data);col.objects.link(o);o.location=pos;o.rotation_euler=(Vector((0,0,1.5))-o.location).to_track_quat('-Z','Y').to_euler()
        data=bpy.data.cameras.new('Studio_Camera');o=bpy.data.objects.new('Studio_Camera',data);col.objects.link(o);scene.camera=o
    else:scene.camera=bpy.data.objects['Studio_Camera']
def visible(keys,source=False):
    bpy.data.collections['00_SOURCE_READONLY'].hide_render=not source
    bpy.data.collections['00_SOURCE_READONLY'].hide_viewport=not source
    bpy.data.collections['10_WORKING_MODELS'].hide_render=source
    bpy.data.collections['10_WORKING_MODELS'].hide_viewport=source
    for prefix in ['SRC_','WORK_']:
        parent=bpy.data.collections['00_SOURCE_READONLY' if prefix=='SRC_' else '10_WORKING_MODELS']
        for c in parent.children:
            key=c.name.removeprefix(prefix);c.hide_render=key not in keys;c.hide_viewport=key not in keys
    bpy.context.view_layer.update()
def render(keys,label,view='Hero',source=False):
    visible(keys,source);obs=[o for key in keys for o in meshes(key,source)]
    deps=bpy.context.evaluated_depsgraph_get();pts=[o.matrix_world@Vector(v) for o in obs for v in o.evaluated_get(deps).bound_box]
    lo=Vector([min(p[i] for p in pts) for i in range(3)]);hi=Vector([max(p[i] for p in pts) for i in range(3)]);center=(lo+hi)/2;span=max(hi-lo)
    if len(keys)==1 and keys[0] in IMP['objects']:
        frozen=IMP['objects'][keys[0]]['bounds'];frame_lo=Vector(frozen['min']);frame_hi=Vector(frozen['max']);center=(frame_lo+frame_hi)/2;span=max(frame_hi-frame_lo)
    print('RENDER_BOUNDS',label,view,list(lo),list(hi),flush=True)
    camera=bpy.context.scene.camera
    direction={'Hero':Vector((1.5,-2,1.05)),'Front':Vector((0,-1,0)),'Side':Vector((1,0,0)),'Rear':Vector((0,1,0))}[view].normalized()
    camera.location=center+direction*span*3;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=span*1.28;camera.data.clip_end=1000
    bpy.data.objects['Studio_Floor'].location.z=lo.z-.025
    dest=OUT/'Previews'/f'{label}_{view}.png';dest.parent.mkdir(exist_ok=True)
    bpy.context.scene.render.filepath=str(dest);bpy.ops.render.render(write_still=True)
    return str(dest)
