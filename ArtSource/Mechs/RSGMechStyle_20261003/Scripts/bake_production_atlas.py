"""UV and portable 2K flat-color / independent structural-line atlas in Blender."""
import bpy
import bmesh
import json
import math
from collections import defaultdict
from pathlib import Path
from mathutils import Vector

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'Production_B_v1'
TEX=OUT/'Textures'
TEX.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene
obj=bpy.data.objects['RSGMech_LOD0_Body']
mesh=obj.data
palette=json.loads((ROOT/'References_A_v3/reference_setup.json').read_text(encoding='utf-8'))['palette_srgb']
bm=bmesh.new()
bm.from_mesh(mesh)
bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
bm.to_mesh(mesh)
bm.free()
mesh.update()

def linear(hexcode):
    c=[int(hexcode[i:i+2],16)/255 for i in (1,3,5)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in c)

def node(nt,kind,name=None):
    n=nt.nodes.new(kind)
    if name: n.name=n.label=name
    return n

def image(name,noncolor=False):
    im=bpy.data.images.new(name,width=2048,height=2048,alpha=False,float_buffer=False)
    im.colorspace_settings.name='Non-Color' if noncolor else 'sRGB'
    im.file_format='PNG'
    im.filepath_raw=str(TEX/(name+'.png'))
    return im

# Match the reference's regular curve shading, with hard mechanical junctions.
incident=defaultdict(list)
for face in mesh.polygons:
    for vi in face.vertices:
        key=tuple(round(a,5) for a in mesh.vertices[vi].co)
        incident[key].append((face.normal.copy(),math.sqrt(max(face.area,1e-10))))
normals=[(0,0,0)]*len(mesh.loops)
for face in mesh.polygons:
    face.use_smooth=True
    for li in face.loop_indices:
        key=tuple(round(a,5) for a in mesh.vertices[mesh.loops[li].vertex_index].co)
        total=Vector((0,0,0))
        for other,weight in incident[key]:
            if face.normal.dot(other)>math.cos(math.radians(42)): total+=other*weight
        normals[li]=tuple(total.normalized()) if total.length_squared else tuple(face.normal)
mesh.normals_split_custom_set(normals)
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active=obj
if not mesh.uv_layers: mesh.uv_layers.new(name='UV0_Atlas2K')
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=math.radians(62),margin_method='FRACTION',island_margin=4/2048,
                         area_weight=.05,correct_aspect=True,scale_to_bounds=False)
bpy.ops.object.mode_set(mode='OBJECT')
# Reserve a contiguous atlas strip for the user-marked central head panel.
# Its existing geometry boundary is preserved; the flat color follows faces,
# with more texels at the yellow/blue seam instead of tiny packed islands.
component_ids=mesh.attributes['RSG_Component'].data
panel_material=next(i for i,m in enumerate(mesh.materials) if m.name=='Reference_SkyBlue')
head_panel_faces=[p for p in mesh.polygons if component_ids[p.index].value==28551 and p.material_index==panel_material]
if head_panel_faces:
    for loop in mesh.uv_layers.active.data: loop.uv.x*=.79
    panel_points=[mesh.vertices[vi].co for p in head_panel_faces for vi in p.vertices]
    xmin=min(v.x for v in panel_points); xmax=max(v.x for v in panel_points)
    zmin=min(v.z for v in panel_points); zmax=max(v.z for v in panel_points)
    uv_scale=.185/max(xmax-xmin,1e-8)
    for face in head_panel_faces:
        for li in face.loop_indices:
            co=mesh.vertices[mesh.loops[li].vertex_index].co
            mesh.uv_layers.active.data[li].uv=(.805+(co.x-xmin)*uv_scale,.28+(co.z-zmin)*uv_scale)
    assert .28+(zmax-zmin)*uv_scale<.99
print('Atlas UV projected and packed',flush=True)
base=image('T_RSGMech_BaseColor')
mask=image('T_RSGMech_LineMask',True)
original_materials=list(mesh.materials)
original_indices=[face.material_index for face in mesh.polygons]
flat=[]
for original in original_materials:
    name=original.name.removeprefix('Reference_')
    mat=bpy.data.materials.new('BakeFlat_'+name)
    mat.use_nodes=True
    nt=mat.node_tree
    nt.nodes.clear()
    output=node(nt,'ShaderNodeOutputMaterial')
    emission=node(nt,'ShaderNodeEmission')
    emission.inputs['Color'].default_value=(*linear(palette[name]),1)
    nt.links.new(emission.outputs[0],output.inputs['Surface'])
    target=node(nt,'ShaderNodeTexImage','BakeTarget')
    target.image=base
    nt.nodes.active=target
    flat.append(mat)
mesh.materials.clear()
for mat in flat: mesh.materials.append(mat)
for face,index in zip(mesh.polygons,original_indices): face.material_index=index
scene.render.engine='CYCLES'
scene.cycles.device='CPU'
scene.cycles.samples=1
scene.render.bake.use_selected_to_active=False
scene.render.bake.use_clear=True
scene.render.bake.margin=4
for modifier in obj.modifiers: modifier.show_render=False
bpy.ops.object.bake(type='EMIT')
# Stable flat palette texels for lower-LOD geometry, independent of line UVs.
palette_swatches={}
raw=list(base.pixels[:])
for i,(name,hexcode) in enumerate(palette.items()):
    material_index=next(j for j,m in enumerate(original_materials) if m.name=='Reference_'+name)
    candidates=[p for p in mesh.polygons if p.material_index==material_index]
    if candidates:
        face=max(candidates,key=lambda p:p.area)
        center=sum((mesh.uv_layers.active.data[li].uv for li in face.loop_indices),Vector((0,0)))/len(face.loop_indices)
        pixel=(int(center.y*2048)*2048+int(center.x*2048))*4
        rgba=raw[pixel:pixel+4]
    else:
        rgba=[*linear(hexcode),1]
    x,y=.825+.04*(i%4),.05+.10*(i//4)
    palette_swatches[name]=[x,y]
    for py in range(int((y-.03)*2048),int((y+.03)*2048)):
        for px in range(int((x-.015)*2048),int((x+.015)*2048)):
            offset=(py*2048+px)*4
            raw[offset:offset+4]=rgba
base.pixels.foreach_set(raw)
base.update()
base.save()
print('2K BaseColor emitted without baked lighting',flush=True)

# Flag actual component boundaries and mechanical creases, excluding coplanar
# tessellation diagonals. Barycentric distances give UV-pixel based line width.
adjacent=defaultdict(list)
for face in mesh.polygons:
    verts=list(face.vertices)
    assert len(verts)==3
    for i in range(3): adjacent[tuple(sorted((verts[i],verts[(i+1)%3])))].append(face)
flags={}
for key,faces in adjacent.items():
    structural=len(faces)!=2
    if len(faces)==2:
        a,b=faces
        structural=(a.material_index!=b.material_index or a.normal.dot(b.normal)<math.cos(math.radians(80)))
    if (mesh.vertices[key[0]].co-mesh.vertices[key[1]].co).length<.04:
        structural=False
    flags[key]=bool(structural)
bary=mesh.attributes.new('RSG_LineBarycentric','FLOAT_VECTOR','CORNER')
threshold=mesh.attributes.new('RSG_LineThreshold','FLOAT_VECTOR','CORNER')
enabled=mesh.attributes.new('RSG_StructuralEdges','FLOAT_VECTOR','CORNER')
emitmask=mesh.attributes.new('RSG_FunctionalLamp','FLOAT','CORNER')
uv=mesh.uv_layers.active.data
for face in mesh.polygons:
    vis=list(face.vertices)
    loops=list(face.loop_indices)
    points=[uv[li].uv.copy() for li in loops]
    area2=abs((points[1].x-points[0].x)*(points[2].y-points[0].y)-(points[1].y-points[0].y)*(points[2].x-points[0].x))
    limits=[]
    tags=[]
    for i in range(3):
        j,k=(i+1)%3,(i+2)%3
        height=area2/max((points[j]-points[k]).length,1e-10)
        limits.append(min(.12,.45/(2048*max(height,1e-8))))
        tags.append(float(flags[tuple(sorted((vis[j],vis[k])))]))
    amber=original_materials[face.material_index].name=='Reference_Amber'
    for i,li in enumerate(loops):
        bary.data[li].vector=tuple(float(i==j) for j in range(3))
        threshold.data[li].vector=limits
        enabled.data[li].vector=tags
        emitmask.data[li].value=float(amber)

line_material=bpy.data.materials.new('Bake_StructuralLineMask')
line_material.use_nodes=True
nt=line_material.node_tree
nt.nodes.clear()
attrs=[]
for name in ('RSG_LineBarycentric','RSG_LineThreshold','RSG_StructuralEdges'):
    attr=node(nt,'ShaderNodeAttribute')
    attr.attribute_name=name
    split=node(nt,'ShaderNodeSeparateXYZ')
    nt.links.new(attr.outputs['Vector'],split.inputs[0])
    attrs.append(split)
terms=[]
for axis in 'XYZ':
    less=node(nt,'ShaderNodeMath')
    less.operation='LESS_THAN'
    nt.links.new(attrs[0].outputs[axis],less.inputs[0])
    nt.links.new(attrs[1].outputs[axis],less.inputs[1])
    mul=node(nt,'ShaderNodeMath')
    mul.operation='MULTIPLY'
    nt.links.new(less.outputs[0],mul.inputs[0])
    nt.links.new(attrs[2].outputs[axis],mul.inputs[1])
    terms.append(mul)
max0=node(nt,'ShaderNodeMath'); max0.operation='MAXIMUM'
nt.links.new(terms[0].outputs[0],max0.inputs[0]); nt.links.new(terms[1].outputs[0],max0.inputs[1])
max1=node(nt,'ShaderNodeMath'); max1.operation='MAXIMUM'
nt.links.new(max0.outputs[0],max1.inputs[0]); nt.links.new(terms[2].outputs[0],max1.inputs[1])
combine=node(nt,'ShaderNodeCombineXYZ')
nt.links.new(max1.outputs[0],combine.inputs['X'])
lamp=node(nt,'ShaderNodeAttribute'); lamp.attribute_name='RSG_FunctionalLamp'
nt.links.new(lamp.outputs['Fac'],combine.inputs['Y'])
emission=node(nt,'ShaderNodeEmission')
nt.links.new(combine.outputs[0],emission.inputs['Color'])
output=node(nt,'ShaderNodeOutputMaterial')
nt.links.new(emission.outputs[0],output.inputs['Surface'])
target=node(nt,'ShaderNodeTexImage','BakeTarget'); target.image=mask
nt.nodes.active=target
indices=[face.material_index for face in mesh.polygons]
mesh.materials.clear(); mesh.materials.append(line_material)
for face in mesh.polygons: face.material_index=0
mask.scale(4096,4096)
scene.render.bake.margin=8
bpy.ops.object.bake(type='EMIT')
mask.scale(2048,2048)
mask_raw=list(mask.pixels[:])
for x,y in palette_swatches.values():
    for py in range(int((y-.03)*2048),int((y+.03)*2048)):
        for px in range(int((x-.015)*2048),int((x+.015)*2048)):
            offset=(py*2048+px)*4
            mask_raw[offset:offset+4]=[0,0,0,1]
mask.pixels.foreach_set(mask_raw); mask.update()
mask.save()
print('2K independent structural line / lamp mask emitted',flush=True)

# Portable one-section body shader: fixed reference light, exactly three bands.
mat=bpy.data.materials.new('M_RSGMech_Toon3_LOD0')
mat.use_nodes=True
mat.diffuse_color=(.8,.6,.4,1)
nt=mat.node_tree; nt.nodes.clear()
color=node(nt,'ShaderNodeTexImage','BaseColor_2K'); color.image=base
line=node(nt,'ShaderNodeTexImage','StructureLineMask_2K'); line.image=mask
split=node(nt,'ShaderNodeSeparateXYZ'); nt.links.new(line.outputs['Color'],split.inputs[0])
strength=node(nt,'ShaderNodeValue','InternalLineStrength'); strength.outputs[0].default_value=1
linefac=node(nt,'ShaderNodeMath'); linefac.operation='MULTIPLY'
nt.links.new(split.outputs['X'],linefac.inputs[0]); nt.links.new(strength.outputs[0],linefac.inputs[1])
geom=node(nt,'ShaderNodeNewGeometry')
dot=node(nt,'ShaderNodeVectorMath','ReferenceKeyLight'); dot.operation='DOT_PRODUCT'
dot.inputs[1].default_value=Vector((.35,-.55,.76)).normalized()
nt.links.new(geom.outputs['Normal'],dot.inputs[0])
bands=node(nt,'ShaderNodeValToRGB','ThreeToneBands')
bands.color_ramp.interpolation='CONSTANT'
bands.color_ramp.elements.remove(bands.color_ramp.elements[1])
for i,(at,factor) in enumerate(((0,.42),(.12,.74),(.55,1))):
    element=bands.color_ramp.elements[0] if i==0 else bands.color_ramp.elements.new(at)
    element.position=at; element.color=(factor,factor,factor,1)
nt.links.new(dot.outputs['Value'],bands.inputs[0])
shade=node(nt,'ShaderNodeMixRGB','BaseColorTimesThreeBands'); shade.blend_type='MULTIPLY'; shade.inputs[0].default_value=1
nt.links.new(color.outputs['Color'],shade.inputs[1]); nt.links.new(bands.outputs['Color'],shade.inputs[2])
ink=node(nt,'ShaderNodeMixRGB','StructuralInk'); ink.blend_type='MIX'
nt.links.new(linefac.outputs[0],ink.inputs[0]); nt.links.new(shade.outputs[0],ink.inputs[1])
ink.inputs[2].default_value=(*linear(palette['Ink']),1)
emission=node(nt,'ShaderNodeEmission'); nt.links.new(ink.outputs[0],emission.inputs['Color'])
output=node(nt,'ShaderNodeOutputMaterial'); nt.links.new(emission.outputs[0],output.inputs['Surface'])
mat['ThreeToneFactors']='0.42 / 0.74 / 1.0'
mat['ThreeToneThresholds']='0 / 0.12 / 0.55'
mat['LineMaskChannels']='R internal structural lines; G functional lamps; B reserved'
mat['ApprovedReference']='A-v3'
mesh.materials.clear(); mesh.materials.append(mat)
for face in mesh.polygons: face.material_index=0
for modifier in obj.modifiers: modifier.show_render=True
base.pack(); mask.pack()
scene.render.engine='BLENDER_EEVEE'
scene.render.use_freestyle=False
for view in ('Hero','Front','Left','Back'):
    im=bpy.data.images.load(str(ROOT/f'References_A_v3/RSG_A_v3_{view}_SourceStyle.png'),check_existing=True)
    im.name='Approved_A_v3_'+view; im.pack()
report={'atlas_resolution_px':[2048,2048],'body_material_sections':1,
        'base_color':'Textures/T_RSGMech_BaseColor.png','line_mask':'Textures/T_RSGMech_LineMask.png',
        'mask_channels':{'R':'structural_lines','G':'functional_lamp_mask','B':'reserved'},
        'baked_lighting':False,'structural_edge_count':sum(flags.values()),
        'coplanar_tessellation_edges_drawn':False,'tone_factors':[.42,.74,1],
        'tone_thresholds':[0,.12,.55],'smooth_angle_degrees':42,
        'normals_method':'area weighted angle-limited, same 42-degree rule as A-v3',
        'crease_threshold_degrees':80,
        'line_mask_baked_at_4K_then_filtered_to_2K':True,
        'freestyle_used_in_production_render':False,'textures_packed_in_blend':True}
report['user_marked_head_front_panel_uv']='contiguous front projection reserved in right atlas strip'
report['user_marked_head_front_panel_triangles']=len(head_panel_faces)
report['palette_swatch_uv']=palette_swatches
report['lower_LOD_base_color_UV']='UV_PaletteBase; structural mask remains on UV0_Atlas2K'
(OUT/'atlas_build_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'RSGMech_Atlas_Shading.blend'))
print(json.dumps(report),flush=True)
