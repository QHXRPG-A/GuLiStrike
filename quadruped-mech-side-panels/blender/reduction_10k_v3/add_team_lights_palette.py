"""Add four rigid team lamps and recolor painted shells from the project palette."""
import bpy,bmesh,json,hashlib,math,numpy as np
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree

ROOT=Path(__file__).resolve().parent
FILE=Path(bpy.data.filepath)
REPORT=json.loads((ROOT/'reduction-report.json').read_text(encoding='utf-8'))
SCENES=list(bpy.data.scenes);STATIC=next(s for s in SCENES if s.name.startswith('00'))
bpy.context.window.scene=STATIC;bpy.context.view_layer.update()
PALETTE={'main':'D5C09C','accent':'FEE4D9','frame':'2C3735','friendly':'6AA4BE','enemy':'A34053'}
DETAIL={'palette_srgb':PALETTE,'recolored_materials':{},'lamps':[],'new_objects':[],'art_revision':'1.15'}

def linear(hexcode):
    result=[]
    for start in (0,2,4):
        value=int(hexcode[start:start+2],16)/255
        result.append(value/12.92 if value<=.04045 else ((value+.055)/1.055)**2.4)
    return (*result,1.)

def recolor(material,color):
    node=material.node_tree.nodes.get('组件纯色基色');assert node,material.name
    node.outputs[0].default_value=linear(color);material.diffuse_color=linear(color)
    material['项目色库_sRGB']='#'+color;material['固定中立配色']=True
    DETAIL['recolored_materials'][material.name]='#'+color

for material in bpy.data.materials:
    if material.name.startswith('机甲 ·'):
        if '蓝灰' in material.name:recolor(material,PALETTE['main'])
        elif '米白' in material.name:recolor(material,PALETTE['accent'])
        elif '深灰腿部护甲' in material.name or '黑色主体框架' in material.name:recolor(material,PALETTE['frame'])
for obj in STATIC.objects:
    if obj.name.startswith('Side_Panel'):
        for slot in obj.material_slots:recolor(slot.material,PALETTE['main'])
rear=next(o for o in STATIC.objects if o.name.startswith('Rear_Armor') and '蓝灰' in o.name and not o.get('is_lineart'))
rear_material=rear.data.materials[0].copy();rear_material.name='机甲 · 中立乳白尾部嵌板'
recolor(rear_material,PALETTE['accent'])
for scene in SCENES:
    for o in scene.objects:
        if not o.get('is_lineart') and o.type=='MESH' and o.get('asset_source_name',o.name)==rear.name:o.data.materials[0]=rear_material

controller=bpy.data.objects.new('阵营灯控制 · 0中立 1己方 2敌方',None)
controller.empty_display_type='PLAIN_AXES';controller.empty_display_size=.12
controller['TeamPreview']=1;controller['说明']='0：中立熄灯；1：己方蓝；2：敌方红。仅改变四个腿部阵营灯。'
controller.id_properties_ui('TeamPreview').update(min=0,max=2,soft_min=0,soft_max=2,description='0 中立熄灯 / 1 己方蓝 / 2 敌方红')
controller['team_light_controller']=True
for scene in SCENES:scene.collection.objects.link(controller)

lamp_material=bpy.data.materials.new('机甲 · 阵营灯 · TeamColor');lamp_material.use_nodes=True
lamp_material.diffuse_color=linear(PALETTE['friendly']);lamp_material['PaintRole']=7
lamp_material['TeamPrimary']='GuLi_TeamPrimary';lamp_material['EmissionStrength']=1.8
tree=lamp_material.node_tree;tree.nodes.clear()
color=tree.nodes.new('ShaderNodeRGB');color.name='阵营主色';color.outputs[0].default_value=linear(PALETTE['friendly'])
emission=tree.nodes.new('ShaderNodeEmission');emission.name='阵营灯发光';emission.inputs['Strength'].default_value=1.8
output=tree.nodes.new('ShaderNodeOutputMaterial');tree.links.new(color.outputs[0],emission.inputs['Color']);tree.links.new(emission.outputs[0],output.inputs['Surface'])
def driver(socket,index,expression):
    curve=socket.driver_add('default_value',index) if index is not None else socket.driver_add('default_value')
    drv=curve.driver;drv.type='SCRIPTED';var=drv.variables.new();var.name='team';var.type='SINGLE_PROP'
    var.targets[0].id=controller;var.targets[0].data_path='["TeamPreview"]';drv.expression=expression
for i in range(3):driver(color.outputs[0],i,f'{linear(PALETTE["friendly"])[i]:.10f} if team == 1 else {linear(PALETTE["enemy"])[i]:.10f} if team == 2 else {linear(PALETTE["frame"])[i]:.10f}')
driver(emission.inputs['Strength'],None,'1.8 if team > 0 else 0.0')
frame_material=bpy.data.materials.get('机甲 · 黑色主体框架');assert frame_material

anim_path=FILE.parent/'create_vertex_animations.py'
namespace={'__file__':str(anim_path),'__name__':'lamp_mount_reference'}
exec(compile(anim_path.read_text(encoding='utf-8').split('\nbase_scene = bpy.context.scene',1)[0],str(anim_path),'exec'),namespace)

def make_prism(profile,origin,u,v,n,bottom,top,name,material):
    pts=[origin+u*x+v*y+n*h for h in (bottom,top) for x,y in profile]
    length=len(profile);faces=[]
    for i in range(length):
        j=(i+1)%length;faces.extend(((i,j,j+length),(i,j+length,i+length)))
    for i in range(1,length-1):faces.extend(((0,i+1,i),(length,length+i,length+i+1)))
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(pts,[],faces);mesh.materials.append(material);mesh.update()
    bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
    attr=mesh.color_attributes.new(name='Color',type='BYTE_COLOR',domain='CORNER')
    role=7 if material==lamp_material else 0
    for datum in attr.data:datum.color=(1.,1.,1.,role/255.)
    return mesh

def rigid_transform(rest,posed):
    ac=rest.mean(axis=0);bc=posed.mean(axis=0)
    u,s,vt=np.linalg.svd((rest-ac).T@(posed-bc));rotation=u@vt
    if np.linalg.det(rotation)<0:u[:,-1]*=-1;rotation=u@vt
    return rotation,bc-ac@rotation

def world_positions(data,matrix):
    array=np.empty(len(data)*3,dtype=np.float32);data.foreach_get('co',array);m=np.asarray(matrix,dtype=np.float64)
    return array.reshape(-1,3)@m[:3,:3].T+m[:3,3]

for leg,code in (('Leg_Front_L','FL'),('Leg_Front_R','FR'),('Leg_Rear_L','RL'),('Leg_Rear_R','RR')):
    host=next(o for o in STATIC.objects if o.name.startswith(leg) and '深灰' in o.name)
    labels=np.array([a.value for a in host.data.attributes['source_component'].data]);indices=np.flatnonzero(labels==1)
    coords=[host.matrix_world@v.co for v in host.data.vertices]
    host_tree=BVHTree.FromPolygons(coords,[tuple(p.vertices) for p in host.data.polygons if labels[p.vertices[0]]==1],all_triangles=True)
    data=namespace['LEGS'][leg];sign=data['sign'];axis=(data['knee']-data['hip']).normalized()
    width=.10;length=.25;cut=.012
    housing_profile=[(-width/2+cut,-length/2),(width/2-cut,-length/2),(width/2,-length/2+cut),(width/2,length/2-cut),(width/2-cut,length/2),(-width/2+cut,length/2),(-width/2,length/2-cut),(-width/2,-length/2+cut)]
    candidates=[]
    for polygon in host.data.polygons:
        if labels[polygon.vertices[0]]!=1:continue
        normal=(host.matrix_world.to_3x3()@polygon.normal).normalized()
        center=sum((coords[i] for i in polygon.vertices),Vector())/3
        if normal.x*sign>.65 and (center-data['hip']).length>.22 and (center-data['knee']).length>.25:
            candidates.append((polygon.area,center,normal))
    candidates.sort(key=lambda row:(-row[0],-row[1].z))
    for fraction in (.42,.38,.46,.34,.50):
        mid=data['hip'].lerp(data['knee'],fraction)
        origin,n,face,_=host_tree.ray_cast(Vector((sign*3.5,mid.y,mid.z)),Vector((-sign,0,0)))
        if origin is not None and n.x*sign>.6:candidates.append((0,origin,n))
    for _,origin,n in candidates:
        v=(axis-n*axis.dot(n)).normalized();u=v.cross(n).normalized()
        sampled=[]
        for a in np.linspace(-width/2,width/2,9):
            for b in np.linspace(-length/2,length/2,17):
                hit=host_tree.ray_cast(origin+u*float(a)+v*float(b)+n*.3,-n)[0]
                if hit is None:sampled=[];break
                sampled.append((hit-origin).dot(n))
            if not sampled:break
        if sampled and max(sampled)-min(sampled)<.055:break
    else:raise RuntimeError(('No sufficiently flat supported lamp seat',leg))
    base=min(sampled)-.009;top=max(sampled)+.009
    housing=make_prism(housing_profile,origin,u,v,n,base,top,f'TeamLamp_{code}_Housing',frame_material)
    lw=.054;lh=.209
    lens=make_prism([(-lw/2,-lh/2),(lw/2,-lh/2),(lw/2,lh/2),(-lw/2,lh/2)],origin,u,v,n,top-.002,top+.004,f'TeamLamp_{code}_Lens',lamp_material)
    rest=world_positions(host.data.vertices,host.matrix_world)[indices]
    report={'leg':leg,'anchor':list(origin),'normal':list(n),'housing_depth_m':top-base,'housing_embed_min_m':.009,'lens_overlap_m':.002,'width_m':width,'length_m':length,'triangles':len(housing.polygons)+len(lens.polygons),'scenes':[]}
    for scene in SCENES:
        bpy.context.window.scene=scene;bpy.context.view_layer.update()
        target=next(o for o in scene.objects if o.get('asset_source_name',o.name)==host.name and not o.get('is_lineart'))
        collection=bpy.data.collections.get('阵营灯 · '+scene.name) or bpy.data.collections.new('阵营灯 · '+scene.name)
        if collection.name not in scene.collection.children:scene.collection.children.link(collection)
        for template,role in ((housing,'Housing'),(lens,'Lens')):
            mesh=template if scene==STATIC else template.copy()
            name=template.name if scene==STATIC else template.name+' · '+('行进' if scene.name.startswith('01') else '采矿')
            obj=bpy.data.objects.new(name,mesh);collection.objects.link(obj)
            obj.parent=target;obj.matrix_parent_inverse=target.matrix_world.inverted();obj.matrix_basis=Matrix.Identity(4)
            obj['is_team_light']=True;obj['team_light_part']=role;obj['rigid_leg']=leg;obj['PaintRole']=7 if role=='Lens' else 0
            obj['asset_source_name']=template.name
            if scene!=STATIC:
                rest_lamp=np.asarray([v.co for v in template.vertices],dtype=np.float64)
                target_labels=np.array([a.value for a in target.data.attributes['source_component'].data]);target_indices=np.flatnonzero(target_labels==1)
                for old_key in target.data.shape_keys.key_blocks:
                    posed=world_positions(old_key.data,target.matrix_world)[target_indices]
                    rotation,translation=rigid_transform(rest,posed)
                    key=obj.shape_key_add(name=old_key.name,from_mix=False)
                    key.data.foreach_set('co',(rest_lamp@rotation+translation).astype(np.float32).ravel());key.interpolation=old_key.interpolation
                keys=mesh.shape_keys;reference=target.data.shape_keys;keys.use_relative=False;keys.eval_time=reference.eval_time
                keys.animation_data_create();keys.animation_data.action=reference.animation_data.action
                if reference.animation_data.action_slot:keys.animation_data.action_slot=reference.animation_data.action_slot
            DETAIL['new_objects'].append(obj.name)
        report['scenes'].append(scene.name)
    DETAIL['lamps'].append(report);print('TEAM_LAMP',leg,report,flush=True)

for scene in SCENES:
    bpy.context.window.scene=scene;scene.frame_set(1);bpy.context.view_layer.update()
    dg=bpy.context.evaluated_depsgraph_get();counts={'body':0,'outline':0,'laser':0,'studio':0}
    for obj in scene.objects:
        if obj.type!='MESH':continue
        category='outline' if obj.get('is_lineart') else 'studio' if 'Studio ground' in obj.name else 'laser' if 'Laser' in obj.name else 'body'
        evaluated=obj.evaluated_get(dg);mesh=evaluated.to_mesh();mesh.calc_loop_triangles();counts[category]+=len(mesh.loop_triangles);evaluated.to_mesh_clear()
    counts['asset_total']=counts['body']+counts['outline']+counts['laser'];counts['scene_total']=counts['asset_total']+counts['studio']
    assert counts['scene_total']<=10000
    REPORT['scenes'][scene.name]['triangles']=counts;scene['减面统计']=json.dumps(counts,ensure_ascii=False)

REPORT['body_materials_unchanged']=False;REPORT['palette_and_team_lights']=DETAIL
REPORT['new_team_light_objects_per_scene']=8;REPORT['team_light_triangles_per_scene']=160
controller['TeamPreview']=1;controller.update_tag()
walk=next(s for s in SCENES if s.name.startswith('01'));bpy.context.window.scene=walk;walk.frame_set(1);bpy.context.view_layer.update()
notes=bpy.data.texts.get('减面交付说明');notes.clear();notes.write('''四足机甲 10k v3 修复与阵营灯候选 / 2026-11-13

米砂外壳 #D5C09C；乳白嵌板 #FEE4D9；深灰结构 #2C3735。沿用项目规范 v1.15。
四条腿各有一盏外侧阵营灯，独立灯座与灯罩，随上腿护甲刚性运动。
在大纲选择“阵营灯控制 · 0中立 1己方 2敌方”，修改自定义属性 TeamPreview：
0 中立熄灯；1 己方蓝 #6AA4BE；2 敌方红 #A34053。四盏灯共享控制，固定外壳不变色。

上腿主甲与原外侧护板已焊接成闭合壳，取消跨关节软变形；尾部嵌板重建为对称轮廓。
静态 / 行进本体含灯 7912，描边1942，合计9854三角面。
采矿含120面激光共9974；再加12面演示地面为9986三角面。
行进1–29帧、采矿1–73帧，24 fps，保留源动画时序及激光。
原文件、v1、v2保留。视觉效果待用户复审；未渲染、未截图、未导入UE。
''')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(FILE),check_existing=False,compress=True)
REPORT['output_sha256']=hashlib.sha256(FILE.read_bytes()).hexdigest();REPORT['output_bytes']=FILE.stat().st_size
(ROOT/'reduction-report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
(ROOT/'team-lights-palette.json').write_text(json.dumps(DETAIL,ensure_ascii=False,indent=2),encoding='utf-8')
print('V3_FINALIZED',REPORT['scenes'],REPORT['output_sha256'],flush=True)
