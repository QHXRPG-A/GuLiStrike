"""Color-only Sweeper B candidate: original orange atlas region is TeamPrimary.

No reference-image generation, mesh rebuilding, formal UE import or gameplay run.
The UV mask is essential: reduced LOD faces can span both orange and fixed colors.
"""
import bpy
import hashlib
import json
import sys
import traceback
import numpy as np
from collections import Counter
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'ArtSource/SweeperTeamColor_v1_20261008'
SOURCE = ROOT/'ArtSource/CommanderLOD_20261005/SweeperSummon/SweeperSummon_3Tier.blend'
ATLAS = ROOT/'ArtSource/LocalTeamColorReview_20261008/SourceTextures/T_Sweeper_BaseColor.png'
FILE = OUT/'Sweeper_OrangeTeamColor_B_v1.blend'
ROLE = 'Bv1_ColorRegion'
ALPHA = 'Sweeper_TeamRole_Alpha'
BLUE, RED = '#6AA4BE', '#A34053'


def digest(value):
    return hashlib.sha256(json.dumps(value,separators=(',',':')).encode()).hexdigest()


def invariant(obj):
    mesh = obj.data
    return dict(geometry=digest({'vertices':[list(v.co) for v in mesh.vertices],
        'edges':[list(e.vertices) for e in mesh.edges], 'faces':[list(p.vertices) for p in mesh.polygons]}),
        uv=digest({u.name:[list(x.uv) for x in u.data] for u in mesh.uv_layers}),
        original_colors=digest({a.name:[list(x.color) for x in a.data] for a in mesh.color_attributes if a.name!=ALPHA}),
        normals=digest({'vertices':[list(v.normal) for v in mesh.vertices],
            'corners':[list(n.vector) for n in mesh.corner_normals],
            'smooth':[p.use_smooth for p in mesh.polygons], 'sharp':[e.use_edge_sharp for e in mesh.edges]}),
        weights=digest([[(g.group,g.weight) for g in v.groups] for v in mesh.vertices]),
        # Inactive/viewport-disabled objects have an unevaluated world matrix
        # immediately after a .blend load. Compare the persistent transform data.
        transform=digest({'basis':[list(r) for r in obj.matrix_basis],
            'parent_inverse':[list(r) for r in obj.matrix_parent_inverse],
            'location':list(obj.location),'scale':list(obj.scale),
            'rotation_mode':obj.rotation_mode,'rotation_euler':list(obj.rotation_euler),
            'rotation_quaternion':list(obj.rotation_quaternion),
            'parent':obj.parent.name if obj.parent else None}),
        modifiers=digest([(m.name,m.type) for m in obj.modifiers]),
        animation=digest({'action':obj.animation_data.action.name if obj.animation_data and obj.animation_data.action else None,
            'parent':obj.parent.name if obj.parent else None, 'groups':[g.name for g in obj.vertex_groups]}))


def linear(hex_color):
    values = [int(hex_color.lstrip('#')[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in values)+(1,)


def face_has_orange(uvs, mask):
    """Full texel/triangle intersection; do not guess from the face center."""
    height,width = mask.shape
    xy = np.array(uvs)*np.array([width,height])
    x0=max(0,int(np.floor(xy[:,0].min()-.75))); x1=min(width,int(np.ceil(xy[:,0].max()+.75)))
    y0=max(0,int(np.floor(xy[:,1].min()-.75))); y1=min(height,int(np.ceil(xy[:,1].max()+.75)))
    if x1<=x0 or y1<=y0:
        return False
    yy,xx = np.nonzero(mask[y0:y1,x0:x1])
    if not len(xx):
        return False
    points = np.column_stack((xx+x0+.5,yy+y0+.5))
    # The source is triangulated. Small tolerance includes the filtered UV edge.
    a,b,c = xy[:3]
    cross = lambda v,w: v[...,0]*w[...,1]-v[...,1]*w[...,0]
    denominator = cross(b-a,c-a)
    if abs(denominator)<1e-8:
        return bool(mask[min(height-1,max(0,int(xy[:,1].mean()))),min(width-1,max(0,int(xy[:,0].mean())))])
    s = cross(points-a,c-a)/denominator
    t = cross(b-a,points-a)/denominator
    tolerance = min(.2, 1.0/max(1.0,np.linalg.norm(b-a),np.linalg.norm(c-a)))
    return bool(np.any((s>=-tolerance)&(t>=-tolerance)&(s+t<=1+tolerance)))


def make_material(name, atlas, mask_image, team_hex, enabled):
    material = bpy.data.materials.new(name)
    material.use_nodes=True
    material.diffuse_color=linear(team_hex)
    nodes,links = material.node_tree.nodes,material.node_tree.links
    nodes.clear()
    output=nodes.new('ShaderNodeOutputMaterial'); output.location=(850,100)
    emission=nodes.new('ShaderNodeEmission'); emission.location=(650,100)
    links.new(emission.outputs[0],output.inputs['Surface'])
    uv=nodes.new('ShaderNodeUVMap');uv.uv_map='UVmap_0';uv.location=(-1100,100)
    base=nodes.new('ShaderNodeTexImage');base.image=atlas;base.label='Original BaseColor — unchanged';base.location=(-900,240)
    texmask=nodes.new('ShaderNodeTexImage');texmask.image=mask_image;texmask.label='Only original orange texels';texmask.location=(-900,-40)
    links.new(uv.outputs['UV'],base.inputs['Vector']);links.new(uv.outputs['UV'],texmask.inputs['Vector'])
    role=nodes.new('ShaderNodeVertexColor');role.layer_name=ALPHA;role.label='Role / 255; original RGB preserved';role.location=(-900,-340)
    eligible=nodes.new('ShaderNodeMath');eligible.operation='GREATER_THAN';eligible.inputs[1].default_value=2.5/255
    links.new(role.outputs['Alpha'],eligible.inputs[0]);eligible.location=(-650,-260)
    gate=nodes.new('ShaderNodeMath');gate.operation='MULTIPLY';gate.location=(-440,-80)
    links.new(texmask.outputs['Color'],gate.inputs[0]);links.new(eligible.outputs[0],gate.inputs[1])
    enable=nodes.new('ShaderNodeValue');enable.name='TeamEnabled';enable.label='TeamEnabled';enable.outputs[0].default_value=enabled;enable.location=(-650,-430)
    finalgate=nodes.new('ShaderNodeMath');finalgate.operation='MULTIPLY';finalgate.location=(-250,-80)
    links.new(gate.outputs[0],finalgate.inputs[0]);links.new(enable.outputs[0],finalgate.inputs[1])
    team=nodes.new('ShaderNodeRGB');team.name='TeamPrimary';team.label='TeamPrimary '+team_hex;team.outputs[0].default_value=linear(team_hex);team.location=(-400,350)
    paint=nodes.new('ShaderNodeMixRGB');paint.name='ApplyLocalTeamColor';paint.label='UV mask confines replacement to original orange';paint.location=(0,200)
    links.new(finalgate.outputs[0],paint.inputs[0]);links.new(base.outputs['Color'],paint.inputs[1]);links.new(team.outputs[0],paint.inputs[2])
    geometry=nodes.new('ShaderNodeNewGeometry');geometry.location=(-400,-400)
    dot=nodes.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';dot.location=(-190,-350)
    dot.inputs[1].default_value=Vector((.35,-.55,.76)).normalized();links.new(geometry.outputs['Normal'],dot.inputs[0])
    tiers=nodes.new('ShaderNodeValToRGB');tiers.name='OriginalThreeTone';tiers.label='Original UE thresholds / colors';tiers.location=(0,-230)
    tiers.color_ramp.interpolation='CONSTANT';tiers.color_ramp.elements.remove(tiers.color_ramp.elements[1])
    for i,(threshold,color) in enumerate([(0,(.43,.52,.66,1)),(.12,(.72,.78,.88,1)),(.55,(1,1,1,1))]):
        e=tiers.color_ramp.elements[0] if i==0 else tiers.color_ramp.elements.new(threshold)
        e.position=threshold;e.color=color
    links.new(dot.outputs['Value'],tiers.inputs[0])
    multiply=nodes.new('ShaderNodeMixRGB');multiply.blend_type='MULTIPLY';multiply.inputs[0].default_value=1;multiply.location=(400,100)
    links.new(paint.outputs[0],multiply.inputs[1]);links.new(tiers.outputs[0],multiply.inputs[2]);links.new(multiply.outputs[0],emission.inputs['Color'])
    material['ModelId']=1005;material['TeamPrimaryHex']=team_hex;material['TeamEnabled']=enabled
    material['RegionContract']='Alpha eligibility * UV orange mask; fixed atlas unchanged'
    return material


def setup_scene(name):
    scene=bpy.data.scenes.new(name)
    scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.seed=0
    # Pure emission does not need light sampling or denoising.
    scene.cycles.use_denoising=False
    scene.render.resolution_x=1200;scene.render.resolution_y=900;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA'
    scene.world=bpy.data.worlds.new(name+'_World');scene.world.use_nodes=True
    next(n for n in scene.world.node_tree.nodes if n.type=='BACKGROUND').inputs['Color'].default_value=linear('#EEEAE2')
    scene.view_settings.view_transform='Standard';scene.view_settings.look='None';scene.view_settings.exposure=0;scene.view_settings.gamma=1
    scene['ReviewStatus']='B pending';scene['SourceGeometryUnchanged']=True
    return scene


def instance(scene, source, name, material, offset=(0,0,0)):
    obj=source.copy();obj.data=source.data;obj.name=name
    scene.collection.objects.link(obj)
    for collection in list(obj.users_collection):
        if collection!=scene.collection:
            collection.objects.unlink(obj)
    obj.hide_viewport=False;obj.hide_render=False;obj.hide_set(False)
    # Source FBX objects are parented under an imported scale. Review spacing is
    # in world meters; applying a local offset would shrink it and overlap copies.
    obj.matrix_world=source.matrix_world.copy()
    obj.matrix_world.translation=source.matrix_world.translation+Vector(offset)
    obj.material_slots[0].link='OBJECT';obj.material_slots[0].material=material
    obj['ModelId']=1005;obj['SourceObject']=source.name;obj['ReviewInstanceOnly']=True
    return obj


def camera(scene, position, target, scale=4.4):
    data=bpy.data.cameras.new(scene.name+'_Camera');data.type='ORTHO';data.ortho_scale=scale
    obj=bpy.data.objects.new(data.name,data);scene.collection.objects.link(obj)
    obj.location=position;obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    scene.camera=obj
    return obj


def run():
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'Masks').mkdir(exist_ok=True);(OUT/'Previews').mkdir(exist_ok=True)
    source_sha=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    sources=[bpy.data.objects[f'LOD{i}_SweeperSummon_0'] for i in range(3)]
    baseline={o.name:invariant(o) for o in sources}
    original_scene=bpy.context.scene;original_scene.name='90_原始三档源_原形保留'
    atlas=bpy.data.images.load(str(ATLAS),check_existing=False);atlas.name='T_Sweeper_OriginalBaseColor_Unmodified'
    pixels=np.empty(atlas.size[0]*atlas.size[1]*4,dtype=np.float32);atlas.pixels.foreach_get(pixels)
    pixels=pixels.reshape(atlas.size[1],atlas.size[0],4);rgb=pixels[:,:,:3]
    mask=(rgb[:,:,0]>.35)&(rgb[:,:,0]>rgb[:,:,1]*1.5)&(rgb[:,:,0]>rgb[:,:,2]*2)
    mask_image=bpy.data.images.new('T_Sweeper_OrangeTeamMask',width=atlas.size[0],height=atlas.size[1],alpha=True)
    mask_image.colorspace_settings.name='Non-Color'
    rgba=np.zeros_like(pixels);rgba[:,:,:3]=mask[:,:,None].astype(np.float32);rgba[:,:,3]=1
    mask_image.pixels.foreach_set(rgba.ravel());mask_image.update()
    mask_image.filepath_raw=str(OUT/'Masks/T_Sweeper_OrangeTeamMask.png');mask_image.file_format='PNG';mask_image.save()
    colors=np.round(rgb*255).astype(np.uint8)
    yellow=np.all(colors==np.array([255,209,102],dtype=np.uint8),axis=2)
    cream=np.all(colors==np.array([242,235,221],dtype=np.uint8),axis=2)
    mechanics=np.all(colors==np.array([82,104,122],dtype=np.uint8),axis=2)|np.all(colors==np.array([40,56,68],dtype=np.uint8),axis=2)
    assert not np.any(mask&(yellow|cream|mechanics))
    meshes=[]
    for lod,obj in enumerate(sources):
        mesh=obj.data;mesh.calc_loop_triangles();uv=mesh.uv_layers[0].data
        region=mesh.attributes.new(ROLE,'INT','FACE')
        encoded=mesh.color_attributes.new(ALPHA,'BYTE_COLOR','CORNER')
        oldcolors=mesh.color_attributes.get('Attribute')
        for face in mesh.polygons:
            has_orange=face_has_orange([uv[i].uv[:] for i in face.loop_indices],mask)
            region.data[face.index].value=3 if has_orange else 0
            for i in face.loop_indices:
                original_rgb=oldcolors.data[i].color[:3] if oldcolors else (1,1,1)
                encoded.data[i].color=(*original_rgb,(3 if has_orange else 0)/255)
        mesh.color_attributes.active_color=encoded
        assert invariant(obj)==baseline[obj.name],obj.name+' source invariant changed'
        payload={'ModelId':1005,'LOD':lod,'SourceObject':obj.name,'SourceInvariant':baseline[obj.name],
            'MaskSource':'VertexAlphaEligibilityAndOriginalUVOrangeMask','Role':3,'UVLayer':'UVmap_0',
            'preserve_rgb':True,'TextureMask':'Masks/T_Sweeper_OrangeTeamMask.png',
            'roles':[region.data[p.index].value for p in mesh.polygons],
            'triangles':[[*[float(v) for index in t.vertices for v in mesh.vertices[index].co],region.data[t.polygon_index].value,*encoded.data[t.loops[0]].color[:3]] for t in mesh.loop_triangles],
            'uv0':[[list(uv[i].uv) for i in t.loops] for t in mesh.loop_triangles]}
        sidecar=OUT/'Masks'/f'Sweeper_LOD{lod}_ColorRegions.json'
        sidecar.write_text(json.dumps(payload,separators=(',',':')),encoding='utf8')
        meshes.append({'object':obj.name,'lod':lod,'vertices':len(mesh.vertices),'triangles':len(mesh.loop_triangles),
            'roles':dict(Counter(region.data[p.index].value for p in mesh.polygons)), 'invariant':baseline[obj.name],
            'sidecar':str(sidecar.relative_to(OUT)),'sha256':hashlib.sha256(sidecar.read_bytes()).hexdigest()})
    materials={key:make_material('M_Sweeper_'+key+'_OrangeOnly',atlas,mask_image,hex_color,enabled)
        for key,hex_color,enabled in [('Original',BLUE,0),('Blue',BLUE,1),('Red',RED,1)]}
    for obj in original_scene.objects:
        if obj.type=='MESH':obj.hide_render=True;obj.hide_viewport=True
    overview=setup_scene('00_扫荡者橙区队色_原蓝红_B待审核')
    for key,y in [('Original',-4.1),('Blue',0),('Red',4.1)]:
        instance(overview,sources[0],key+'_Sweeper_LOD0',materials[key],(0,y,0))
    overview.render.resolution_x=1800;overview.render.resolution_y=780
    camera(overview,(14,-5.5,8.2),(0,0,.85),14.2)
    scenes={}
    views={'Hero':((5,-6,4.4),(0,0,.85)), 'Front':((7,0,.88),(0,0,.88)),
        'Left':((0,-7,.88),(0,0,.88)), 'Back':((-7,0,.88),(0,0,.88))}
    for key in ['Original','Blue','Red']:
        scene=setup_scene({'Original':'01_原版橙色','Blue':'02_蓝方橙区改蓝','Red':'03_红方橙区改红'}[key])
        obj=instance(scene,sources[0],key+'_Review_LOD0',materials[key]);scenes[key]=scene
        camera(scene,*views['Hero'],scale=4.6)
        for view,(position,target) in views.items():
            scene.camera.location=position;scene.camera.rotation_euler=(Vector(target)-scene.camera.location).to_track_quat('-Z','Y').to_euler()
            scene.render.filepath=str(OUT/'Previews'/f'{key}_{view}.png')
            bpy.ops.render.render(write_still=True,scene=scene.name)
        position,target=views['Hero'];scene.camera.location=position;scene.camera.rotation_euler=(Vector(target)-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    lodscene=setup_scene('04_蓝方三档LOD_原网格')
    for lod,y in [(0,-4.1),(1,0),(2,4.1)]:instance(lodscene,sources[lod],f'Blue_Review_LOD{lod}',materials['Blue'],(0,y,0))
    lodscene.render.resolution_x=1800;lodscene.render.resolution_y=780
    camera(lodscene,(14,-5.5,8.2),(0,0,.85),14.2)
    for scene,filename in [(overview,'Original_Blue_Red_Overview.png'),(lodscene,'Blue_ThreeLOD.png')]:
        scene.render.filepath=str(OUT/'Previews'/filename)
        bpy.ops.render.render(write_still=True,scene=scene.name)
    # Actual mask renders permit fixed-region comparison in the same four views.
    mask_material=bpy.data.materials.new('M_Sweeper_UVMask_Inspect');mask_material.use_nodes=True
    nodes,links=mask_material.node_tree.nodes,mask_material.node_tree.links;nodes.clear()
    uv=nodes.new('ShaderNodeUVMap');uv.uv_map='UVmap_0'
    tex=nodes.new('ShaderNodeTexImage');tex.image=mask_image
    emission=nodes.new('ShaderNodeEmission');output=nodes.new('ShaderNodeOutputMaterial')
    links.new(uv.outputs['UV'],tex.inputs['Vector']);links.new(tex.outputs['Color'],emission.inputs['Color']);links.new(emission.outputs[0],output.inputs['Surface'])
    maskscene=setup_scene('05_橙色UV遮罩检查_白色可变')
    next(n for n in maskscene.world.node_tree.nodes if n.type=='BACKGROUND').inputs['Color'].default_value=(0,0,0,1)
    instance(maskscene,sources[0],'Mask_Review_LOD0',mask_material)
    camera(maskscene,*views['Hero'],scale=4.6)
    for view,(position,target) in views.items():
        maskscene.camera.location=position;maskscene.camera.rotation_euler=(Vector(target)-maskscene.camera.location).to_track_quat('-Z','Y').to_euler()
        maskscene.render.filepath=str(OUT/'Previews'/f'Mask_{view}.png')
        bpy.ops.render.render(write_still=True,scene=maskscene.name)
    atlas.pack();mask_image.pack()
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.shading.type='MATERIAL'
                area.spaces.active.shading.use_scene_world=True
                area.spaces.active.overlay.show_overlays=False
                area.spaces.active.region_3d.view_perspective='CAMERA'
    if bpy.context.window:bpy.context.window.scene=overview
    report={'ModelId':1005,'version':'Sweeper.OrangeTeamColor.B_v1.20261008',
        'user_decision':'不用出图了，就这橙色的区域作为team colour就好了',
        'source':str(SOURCE),'source_sha256':source_sha,'source_atlas':str(ATLAS),
        'source_atlas_sha256':hashlib.sha256(ATLAS.read_bytes()).hexdigest(),
        'reference_A':'User waived new drawings and directly selected original orange region',
        'B_approval':'pending','formal_ue_import':False,'runtime_verified':False,
        'team_colors':{'Blue':BLUE,'Red':RED},'fixed_regions':'Original texture outside the orange mask, including wheels, gun, cream and yellow lamps',
        'mask':{'file':'Masks/T_Sweeper_OrangeTeamMask.png','width':atlas.size[0],'height':atlas.size[1],
            'orange_texels':int(mask.sum()),'fixed_yellow_texels':int(yellow.sum()),'fixed_cream_texels':int(cream.sum()),
            'fixed_mechanical_texels':int(mechanics.sum()),'fixed_palette_overlap':0},
        'encoding':'New corner Alpha=role/255 preserves source RGB. Precise original-UV mask must also gate team color, especially reduced LODs.',
        'three_tone':'Preserved original UE thresholds .12/.55 and RGB tones (.43,.52,.66)/(.72,.78,.88)/(1,1,1)',
        'ink':'Existing Sweeper no additional ink/outline exception retained','meshes':meshes,
        'geometry_uv_normals_weights_transforms_original_colors_unchanged':True,
        'original_source_file_unchanged':hashlib.sha256(SOURCE.read_bytes()).hexdigest()==source_sha,
        'LOD_boundary':'Blender saved LOD1 has 1355 triangles versus UE render LOD1 1363; pre-existing FBX omission. No formal mesh replacement or LOD regeneration.',
        'excluded_models':[{'ModelId':2004,'name':'克隆兵营占位'},{'ModelId':2007,'name':'领地据点占位'}],
        'blend':str(FILE),'previews':[str(p.relative_to(OUT)) for p in sorted((OUT/'Previews').glob('*.png'))]}
    assert report['original_source_file_unchanged']
    (OUT/'build-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    bpy.ops.wm.save_as_mainfile(filepath=str(FILE))
    report['blend_sha256']=hashlib.sha256(FILE.read_bytes()).hexdigest()
    (OUT/'build-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('SWEEPER_TEAM_REVIEW_READY',json.dumps({'file':str(FILE),'lods':len(meshes),'orange_texels':int(mask.sum()),'formal_ue_import':False}),flush=True)


try:
    run()
except Exception:
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'build-error.txt').write_text(traceback.format_exc(),encoding='utf8')
    traceback.print_exc()
    sys.exit(1)
