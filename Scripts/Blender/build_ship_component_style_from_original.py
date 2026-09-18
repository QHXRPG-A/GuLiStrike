"""Modify the actual original component meshes; retain their mechanical core.

User steering: build on the original models instead of replacing the mechanical
body with approximate newly constructed primitives. References A remain approved.
The v1 independent reconstruction is retained as a process study. This v2 uses
the preserved original rigged Blender sources and freshly exported Thor FBX.
"""
import bpy
import bmesh
import collections
import importlib.util
import json
import math
from pathlib import Path
from mathutils import Vector
from mathutils.kdtree import KDTree

PROJECT=Path('D:/UE5.7/test1')
spec=importlib.util.spec_from_file_location('style_geometry',PROJECT/'Scripts/Blender/build_ship_component_style_models.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
g.OUT=g.ROOT/'Production/v2';g.PREVIEW=g.ROOT/'Previews/v2/Geometry'
g.OUT.mkdir(parents=True,exist_ok=True);g.PREVIEW.mkdir(parents=True,exist_ok=True)
PROVENANCE={}


def source_mesh(key):
    if key!='Thor_MissilePod':
        path=g.ROOT/'Source/ExistingAuthoring'/('SC_'+key+'.blend')
        with bpy.data.libraries.load(str(path),link=False) as (source,target):
            target.objects=[name for name in source.objects if name in ('SKM_SC_'+key,'Armature')]
        for obj in target.objects:
            if obj: bpy.context.scene.collection.objects.link(obj)
        body=next(o for o in target.objects if o and o.type=='MESH')
        rig=next(o for o in target.objects if o and o.type=='ARMATURE')
        rig.animation_data_clear()
        for bone in rig.pose.bones:bone.matrix_basis.identity()
        bpy.context.view_layer.update()
        evaluated=body.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh=evaluated.to_mesh()
        points=[evaluated.matrix_world@v.co for v in mesh.vertices]
        faces=[list(p.vertices) for p in mesh.polygons]
        bones=[]
        groups={vg.index:vg.name for vg in body.vertex_groups}
        for v in mesh.vertices:
            weights=[(w.weight,groups.get(w.group,'')) for w in v.groups]
            bones.append(max(weights)[1] if weights else 'Root')
        evaluated.to_mesh_clear()
        for obj in target.objects:
            if obj:bpy.data.objects.remove(obj,do_unlink=True)
    else:
        path=g.ROOT/'Source/FBX_static_v1/SM_SC_Thor_MissilePod.fbx'
        before=set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=str(path),use_anim=False)
        imported=[o for o in bpy.data.objects if o not in before]
        body=next(o for o in imported if o.type=='MESH')
        g.select([body]);bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
        points=[body.matrix_world@v.co for v in body.data.vertices]
        faces=[list(p.vertices) for p in body.data.polygons];bones=['Root']*len(points)
        for obj in imported:bpy.data.objects.remove(obj,do_unlink=True)
    return path,points,faces,bones


def polygons_for_panels(obj, predicate, count=1):
    # Read coplanar face regions on a temporary mesh. The original vertex positions
    # and triangles in the retained source piece are not modified by this operation.
    mesh=obj.data.copy();bm=bmesh.new();bm.from_mesh(mesh)
    bmesh.ops.dissolve_limit(bm,angle_limit=.0001,use_dissolve_boundaries=False,verts=list(bm.verts),edges=list(bm.edges))
    bm.normal_update()
    candidates=[]
    for face in bm.faces:
        center=face.calc_center_median();normal=face.normal.copy()
        if predicate(center,normal):
            candidates.append((face.calc_area(),[v.co.copy() for v in face.verts],normal))
    candidates.sort(key=lambda x:x[0],reverse=True)
    result=candidates[:count]
    bm.free();bpy.data.meshes.remove(mesh)
    return result


def skin_from_face(name, polygon, mat, bone, scale=.88, offset=.04, thickness=.06):
    _,coords,normal=polygon;center=sum(coords,Vector())/len(coords)
    coords=[center+(p-center)*scale for p in coords]
    vertices=[]
    for distance in (offset,offset+thickness):
        for p in coords:
            q=p+normal*distance;vertices.append((q.x,-q.y,q.z))
    n=len(coords);faces=[tuple(range(n-1,-1,-1)),tuple(range(n,n*2))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    skin=g.make_mesh(name,vertices,faces,mat,bone,min(thickness*.2,.025))
    skin['source_face_based']=True
    return skin


def piece_material(key,seed,bone):
    if key=='Twin_Barrel_Turret':
        if seed in (152,642):return 'Slate'
        if seed in (0,57):return 'Navy'
        if seed in (224,232,724,732):return 'Ochre'
        if seed in (136,454):return 'Slate'
        if seed in (462,482,502):return 'Slate'
        if seed==326:return 'Slate'
        return 'Pearl'
    if key=='CIWS':
        if seed in (16,243,284):return 'Slate'
        if seed in (57,194):return 'Cyan'
        if seed in (325,345,365):return 'Steel'
        if seed in (154,178):return 'Navy'
        return 'Pearl'
    if seed in (866,0,641,450,215):return 'Slate'
    if seed in (386,621,631,856,1091):return 'Navy'
    return 'Pearl'


def original_based(key):
    data=json.loads((g.ROOT/'Source'/(key+'_original_geometry.json')).read_text(encoding='utf-8'))
    path,points,faces,bones=source_mesh(key)
    kd=KDTree(len(data['geometry']['points']))
    by_id={v:c['seed'] for c in data['components'] for v in c['ids']}
    for i,p in enumerate(data['geometry']['points']):kd.insert(g.point([v/100 for v in p]),i)
    kd.balance()
    closest=[kd.find(p) for p in points]
    vertex_seeds=[by_id[row[1]] for row in closest]
    buckets=collections.defaultdict(list)
    for face in faces:
        seed=collections.Counter(vertex_seeds[v] for v in face).most_common(1)[0][0]
        bone=collections.Counter(bones[v] for v in face).most_common(1)[0][0]
        if bone not in ('Root','BarrelPitch'):raise RuntimeError('Unexpected source bone '+bone)
        buckets[(seed,bone)].append(face)
    parts={};original_count=0
    scale=.004 if key=='CIWS' else .035
    for (seed,bone),polygons in sorted(buckets.items()):
        indices=sorted({v for f in polygons for v in f});remap={v:i for i,v in enumerate(indices)}
        ue_points=[(points[i].x,-points[i].y,points[i].z) for i in indices]
        obj=g.make_mesh('Original_'+key+'_Part_%04d'%seed,ue_points,
                       [[remap[v] for v in f] for f in polygons],piece_material(key,seed,bone),bone,scale)
        obj['original_source_file']=str(path.relative_to(g.ROOT));obj['original_component_seed']=seed
        obj['original_vertices_retained']=True;obj['core_change']='none; added bevel is an editable modifier'
        parts[seed]=obj;original_count+=len(polygons)
    assert original_count==len(faces),'Every original face must remain represented'
    PROVENANCE[key]={'source':str(path.relative_to(g.ROOT)),'source_vertices':len(points),'source_faces':len(faces),
                     'retained_original_faces':original_count,'retained_original_pieces':len(parts),
                     'original_core_vertex_displacement_m':0.0,
                     'note':'All original core faces retained with their original rest coordinates. Armor additions are separate editable pieces; bevels remain modifiers.'}
    if key=='Twin_Barrel_Turret':
        for seed in (0,57):
            part=parts[seed]
            top=polygons_for_panels(part,lambda c,n:n.z>.50 and c.z>0,2)
            for i,face in enumerate(top):
                skin_from_face('Twin_Receiver_Pearl_Skin_%d_%d'%(seed,i),face,'Pearl','BarrelPitch',.94,.035,.075)
            if top:
                skin_from_face('Twin_Receiver_Ochre_Cover_%d'%seed,top[0],'Ochre','BarrelPitch',.54,.12,.10)
        for seed in (122,240):
            part=parts[seed]
            side=polygons_for_panels(part,lambda c,n:abs(n.x)>.75,1)
            for face in side:skin_from_face('Twin_Ochre_Side_Armor_%d'%seed,face,'Ochre','Root',.79,.045,.07)
        for seed in (278,296,314,426):
            top=polygons_for_panels(parts[seed],lambda c,n:n.z>.5,1)
            for face in top:skin_from_face('Twin_Layered_Rear_Armor_%d'%seed,face,'Pearl','Root',.94,.025,.075)
        # Existing cooling fins remain in place and receive crisp regular bevels;
        # the two originally open gaps get matching covers from the same source shape.
        for seed in (438,522,538,554,578,594,610,626):
            parts[seed]['function']='Original cooling fin; preserved geometry'
        pivot=[v/100 for v in g.SNAPSHOT['parts'][key]['bones'][1]['local']['location']]
        for side in (-1,1):
            g.tube('Twin_Fixed_Bearing_Trim_'+str(side),(side*9.08,pivot[1],pivot[2]),1.0,.78,.06,'X','Steel','Root',48)
    elif key=='CIWS':
        frame=parts[97]
        frame.data.materials.append(g.MATS['Navy'])
        for p in frame.data.polygons:
            if p.normal.z<-.5 or p.center.z<-.72:p.material_index=1
        shroud=parts[57]
        # Keep the original section, add a narrow manufactured bevel and a thin
        # conforming cyan cover rather than replacing the entire receiver.
        top=polygons_for_panels(shroud,lambda c,n:n.z>.55,1)
        for face in top:skin_from_face('CIWS_Conforming_Cyan_Shroud',face,'Cyan','BarrelPitch',.9,.010,.022)
        for sign in (-1,1):
            side=polygons_for_panels(frame,lambda c,n:sign*n.x>.75 and sign*c.x>0,1)
            for face in side:
                skin_from_face('CIWS_Cyan_Flank_'+str(sign),face,'Cyan','Root',.55,.012,.027)
            pivot=[v/100 for v in g.SNAPSHOT['parts'][key]['bones'][1]['local']['location']]
            g.tube('CIWS_Fixed_Bearing_Trim_'+str(sign),(sign*.98,pivot[1],pivot[2]),.30,.23,.022,'X','Steel','Root',48)
        for seed in (0,146):
            top=polygons_for_panels(parts[seed],lambda c,n:n.z>.6,1)
            for face in top:skin_from_face('CIWS_Pearl_Shoulder_Layer_'+str(seed),face,'Pearl','Root',.84,.01,.025)
        # The rear four-slot structures are cut into new covers only; the original
        # frame remains the protected mechanical core.
        for sign in (-1,1):
            g.box('CIWS_Rear_Vent_Cover_'+str(sign),(sign*1.43,-1.75,.14),(.30,.92,.65),'Pearl','Root',.013,chamfer=.03)
            for i in range(4):
                y=-2.07+i*.20
                g.prism_x('CIWS_Rear_Vent_'+str(sign)+'_'+str(i),[(y-.034,-.08),(y+.034,-.08),(y+.034,.38),(y-.034,.38)],sign*1.584,sign*1.592,'Navy','Root',.004)
    else:
        cores=(866,0,641,450,215)
        for cell,seed in enumerate(cores,1):
            part=parts[seed]
            outer,holes,top=g.thor_top_measurements(data,seed)
            for name in ('Brick','Pearl','Steel','Navy'):part.data.materials.append(g.MATS[name])
            for p in part.data.polygons:
                center=p.center;xy=(center.x,-center.y)
                near_bore=min(math.dist(xy,hole) for hole in holes)<1.68
                if center.z>2.89 and p.normal.z>.85:p.material_index=1
                elif near_bore and center.z>2.3:p.material_index=3
                elif near_bore:p.material_index=4
                elif center.z>1.70:p.material_index=2
            walls=polygons_for_panels(part,lambda c,n:abs(n.z)<.3 and min(math.dist((c.x,-c.y),h) for h in holes)>1.8,3)
            for i,face in enumerate(walls):skin_from_face('Thor_Cell%02d_Conforming_Pearl_Armor_%d'%(cell,i),face,'Pearl','Root',.81,.03,.08)
            for hole,(x,y) in enumerate(holes,1):
                g.tube('Thor_Cell%02d_Machined_Mouth_%d'%(cell,hole),(x,y,top-.10),1.56,1.285,.19,'Z','Steel','Root',64)
        for seed in (1165,1081,1155,1145,396):
            face=polygons_for_panels(parts[seed],lambda c,n:abs(n.z)<.3,1)
            for polygon in face:
                skin_from_face('Thor_Layered_Flank_'+str(seed),polygon,'Pearl','Root',.90,.035,.08)
                skin_from_face('Thor_Service_Hatch_'+str(seed),polygon,'Pearl','Root',.44,.125,.055)
    PROVENANCE[key]['new_skin_pieces']=len(g.PARTS)-len(parts)


g.twin=lambda:original_based('Twin_Barrel_Turret')
g.ciws=lambda:original_based('CIWS')
g.thor=lambda:original_based('Thor_MissilePod')

if __name__=='__main__':
    for key in ('Twin_Barrel_Turret','CIWS','Thor_MissilePod'):
        g.build(key)
        (g.OUT/(key+'_original_modification_provenance.json')).write_text(json.dumps(PROVENANCE[key],ensure_ascii=False,indent=2),encoding='utf-8')
        print('ORIGINAL_MODEL_RETAINED',key,json.dumps(PROVENANCE[key]),flush=True)
    print('SHIP_STYLE_ORIGINAL_BASED_GEOMETRY_COMPLETE',flush=True)
