"""Geometry/data-only checks for closed shells, badge clearance, lamps and palette."""
import bpy,bmesh,json,numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT=Path(__file__).resolve().parent
PRODUCTION=json.loads((ROOT/'reduction-report.json').read_text(encoding='utf-8'))
SCENES=list(bpy.data.scenes);STATIC=next(s for s in SCENES if s.name.startswith('00'))
bpy.context.window.scene=STATIC;bpy.context.view_layer.update()
REPORT={'closed_armor':[],'lamps':[],'lamp_animation':{},'team_modes':{},'palette':{},'checks':{}}

def world(data,matrix):
    arr=np.empty(len(data)*3,dtype=np.float32);data.foreach_get('co',arr);m=np.asarray(matrix)
    return arr.reshape(-1,3)@m[:3,:3].T+m[:3,3]

def geometry(obj,component=None):
    points=[obj.matrix_world@v.co for v in obj.data.vertices]
    labels=[a.value for a in obj.data.attributes['source_component'].data] if component is not None else None
    faces=[tuple(p.vertices) for p in obj.data.polygons if component is None or labels[p.vertices[0]]==component]
    return points,faces

def topology(points,faces):
    bm=bmesh.new();vmap={i:bm.verts.new(points[i]) for i in set(v for f in faces for v in f)}
    for face in faces:bm.faces.new([vmap[i] for i in face])
    remaining=set(bm.verts);components=0
    while remaining:
        components+=1;stack=[remaining.pop()]
        while stack:
            v=stack.pop()
            for edge in v.link_edges:
                other=edge.other_vert(v)
                if other in remaining:remaining.remove(other);stack.append(other)
    result={'boundary_edges':sum(e.is_boundary for e in bm.edges),'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),'degenerate_faces':sum(f.calc_area()<1e-11 for f in bm.faces),'connected_components':components}
    bm.free();return result

for leg in ('Leg_Front_L','Leg_Front_R','Leg_Rear_L','Leg_Rear_R'):
    host=next(o for o in STATIC.objects if o.name.startswith(leg) and '深灰' in o.name)
    points,faces=geometry(host,1);stats=topology(points,faces);stats['leg']=leg
    REPORT['closed_armor'].append(stats)
    host_tree=BVHTree.FromPolygons(points,faces,all_triangles=True)
    lights=[o for o in STATIC.objects if o.get('is_team_light') and o.get('rigid_leg')==leg]
    assert len(lights)==2
    housing=next(o for o in lights if o['team_light_part']=='Housing');lens=next(o for o in lights if o['team_light_part']=='Lens')
    hp,hf=geometry(housing);lp,lf=geometry(lens)
    ht=BVHTree.FromPolygons(hp,hf,all_triangles=True);lt=BVHTree.FromPolygons(lp,lf,all_triangles=True)
    record={'leg':leg,'housing':topology(hp,hf),'lens':topology(lp,lf),'housing_armor_intersections':len(ht.overlap(host_tree)),'lens_housing_intersections':len(lt.overlap(ht)),'lens_role_7':all(abs(d.color[3]-7/255)<.001 for d in lens.data.color_attributes['Color'].data)}
    REPORT['lamps'].append(record)

# Upper armor and all lamp vertices must keep their mutual distances in every key.
max_distance_error=0.;max_loop_error=0.;states_checked=0
for scene in SCENES:
    if scene==STATIC:continue
    bpy.context.window.scene=scene;bpy.context.view_layer.update()
    for lamp in [o for o in scene.objects if o.get('is_team_light')]:
        host=lamp.parent
        labels=np.array([a.value for a in host.data.attributes['source_component'].data]);ids=np.flatnonzero(labels==1)
        host_keys=host.data.shape_keys.key_blocks;lamp_keys=lamp.data.shape_keys.key_blocks
        assert len(host_keys)==len(lamp_keys)
        reference=world(host_keys[0].data,host.matrix_world)[ids]
        anchors=sorted(set(int(f(reference[:,a])) for a in range(3) for f in (np.argmin,np.argmax)))
        a=reference[anchors];b=world(lamp_keys[0].data,lamp.matrix_world)
        baseline=np.linalg.norm(a[:,None,:]-b[None,:,:],axis=2)
        for hk,lk in zip(host_keys,lamp_keys):
            a=world(hk.data,host.matrix_world)[ids][anchors];b=world(lk.data,lamp.matrix_world)
            error=float(np.max(np.abs(np.linalg.norm(a[:,None,:]-b[None,:,:],axis=2)-baseline)))
            max_distance_error=max(max_distance_error,error);states_checked+=1
        max_loop_error=max(max_loop_error,float(np.max(np.linalg.norm(world(lamp_keys[0].data,lamp.matrix_world)-world(lamp_keys[-1].data,lamp.matrix_world),axis=1))))
REPORT['lamp_animation']={'states_checked':states_checked,'maximum_host_lamp_distance_error_m':max_distance_error,'maximum_loop_error_m':max_loop_error}

bpy.context.window.scene=STATIC;bpy.context.view_layer.update()
badge=next(o for o in STATIC.objects if o.name.startswith('Rear_Armor') and '蓝灰' in o.name)
points,faces=geometry(badge);REPORT['badge_topology']=topology(points,faces)
tree=KDTree(len(points))
for i,p in enumerate(points):tree.insert(p,i)
tree.balance();REPORT['badge_symmetry_error_m']=max(tree.find(Vector((-p.x,p.y,p.z)))[2] for p in points)
occluder_points=[];occluder_faces=[]
for obj in STATIC.objects:
    if obj.type!='MESH' or obj==badge or obj.get('is_lineart') or 'Studio' in obj.name:continue
    p,f=geometry(obj);base=len(occluder_points);occluder_points.extend(p);occluder_faces.extend(tuple(base+i for i in face) for face in f)
others=BVHTree.FromPolygons(occluder_points,occluder_faces,all_triangles=True)
occluded=0;top_samples=0;minimum_clearance=100.
for polygon in badge.data.polygons:
    if polygon.normal.z<.7:continue
    a,b,c=[points[i] for i in polygon.vertices]
    for i in range(21):
        for j in range(21-i):
            p=a*(i/20)+b*(j/20)+c*((20-i-j)/20)
            # Upward hits would cover the colored surface; downward hits measure seating.
            hit=others.ray_cast(p+Vector((0,0,.0001)),Vector((0,0,1)),10)[0]
            if hit is not None:occluded+=1
            below=others.ray_cast(p-Vector((0,0,.0001)),Vector((0,0,-1)),1)[0]
            if below is not None:minimum_clearance=min(minimum_clearance,p.z-below.z)
            top_samples+=1
REPORT['badge_surface']={'top_samples':top_samples,'occluded_samples':occluded,'minimum_sampled_top_clearance_m':minimum_clearance}

def linear(hexcode):
    values=[int(hexcode[i:i+2],16)/255 for i in (0,2,4)]
    return [v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in values]

for name,hexcode in PRODUCTION['palette_and_team_lights']['recolored_materials'].items():
    mat=bpy.data.materials[name];value=mat.node_tree.nodes['组件纯色基色'].outputs[0].default_value
    REPORT['palette'][name]={'hex':hexcode,'maximum_linear_error':max(abs(a-b) for a,b in zip(value[:3],linear(hexcode[1:]))) }
controller=next(o for o in bpy.data.objects if o.get('team_light_controller'))
material=bpy.data.materials['机甲 · 阵营灯 · TeamColor']
for mode,hexcode in ((0,'2C3735'),(1,'6AA4BE'),(2,'A34053')):
    controller['TeamPreview']=mode;controller.update_tag();STATIC.frame_set(mode+1);bpy.context.view_layer.update()
    evaluated=material.evaluated_get(bpy.context.evaluated_depsgraph_get())
    actual=evaluated.node_tree.nodes['阵营主色'].outputs[0].default_value
    strength=evaluated.node_tree.nodes['阵营灯发光'].inputs['Strength'].default_value
    REPORT['team_modes'][str(mode)]={'color_error':max(abs(a-b) for a,b in zip(actual[:3],linear(hexcode))),'emission_strength':strength,'expected_emission_strength':1.8 if mode else 0.}
controller['TeamPreview']=1;controller.update_tag();STATIC.frame_set(1);bpy.context.view_layer.update()
checks=REPORT['checks']
checks['four_upper_shells_closed_connected']=len(REPORT['closed_armor'])==4 and all(r['nonmanifold_edges']==0 and r['degenerate_faces']==0 and r['connected_components']==1 for r in REPORT['closed_armor'])
checks['four_lamps_closed_and_mounted']=len(REPORT['lamps'])==4 and all(r['housing']['nonmanifold_edges']==0 and r['lens']['nonmanifold_edges']==0 and r['housing_armor_intersections']>0 and r['lens_housing_intersections']>0 and r['lens_role_7'] for r in REPORT['lamps'])
checks['lamps_follow_armor_without_drift']=max_distance_error<.000005 and max_loop_error<.000005
checks['badge_closed_and_symmetric']=REPORT['badge_topology']['nonmanifold_edges']==0 and REPORT['badge_symmetry_error_m']<.000005
checks['badge_top_not_occluded']=occluded==0 and minimum_clearance>.003
checks['neutral_palette_exact']=all(r['maximum_linear_error']<.000001 for r in REPORT['palette'].values())
checks['three_team_modes_work']=all(r['color_error']<.000001 and abs(r['emission_strength']-r['expected_emission_strength'])<.000001 for r in REPORT['team_modes'].values())
REPORT['all_new_feature_checks_passed']=all(checks.values());REPORT['visual_acceptance']='待用户复审，未渲染或助手读图。'
(ROOT/'repairs-lights-validation.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(REPORT,ensure_ascii=False),flush=True)
assert REPORT['all_new_feature_checks_passed']
