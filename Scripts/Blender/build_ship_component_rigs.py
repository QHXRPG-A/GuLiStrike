"""Build the approved seven rigid turrets in an isolated background Blender.

Usage: blender --background --factory-startup --python this_file -- inspect|build
All measurements and authoring specifications are in original UE centimeters.
"""
import bpy
import bmesh
import json
import math
import sys
from pathlib import Path
from mathutils import Vector, Matrix, Quaternion
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'outputs/ship-component-rigs'
ART = ROOT / 'ArtSource/Ships/ShipComponentRigs'
BASELINE = json.loads((OUT / 'source-baseline.json').read_text(encoding='utf-8'))['assets']
NAMES = ['Autocannon', 'Bottom_Twin_Barrel_Turret', 'CIWS', 'High_Rate_Fire_Cannon',
         'Single_Barrel_Turret', 'Triple_Barrel_Turret', 'Twin_Barrel_Turret']
SPECS = {
    'Autocannon': dict(pivot=(.008, -237.346, 99.581), sign=1,
        moving=[0,16,101,186,271,297,323,349,365,384,406,669,689], muzzles=[101,186,16]),
    'Bottom_Twin_Barrel_Turret': dict(pivot=(0,-220,27.012), sign=-1,
        moving=[104,199,271,279,301,358,642,714,724,732], muzzles=[199,642]),
    'CIWS': dict(pivot=(0,20,18.818), sign=1,
        moving=[16,57,194,243,284,325,345,365], muzzles=[16,284,243]),
    'High_Rate_Fire_Cannon': dict(pivot=(.01,540,-80.739), sign=1,
        moving=[112,168,223,251,445], muzzles=[445,251]),
    'Single_Barrel_Turret': dict(pivot=(0,-145,5.357), sign=1,
        moving=[98,147,155,163,171,211,233,322,542,570,598,626,654,682,710,730,738], muzzles=[171]),
    'Triple_Barrel_Turret': dict(pivot=(-.105,-510,129.919), sign=1,
        moving=[0,40,125,135,169,179,189,197,205,213,221,306,330,338,346,354,362,386,394,402,410,418,503,527,537,569,579,589,621,631,671,703,713], muzzles=[40,221,418]),
    'Twin_Barrel_Turret': dict(pivot=(0,-220,-27.01), sign=1,
        moving=[0,57,152,224,232,268,642,714,724,732], muzzles=[642,152]),
}


def dump(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')


def ue_point(p):
    return Vector((p.x * 100, -p.y * 100, p.z * 100))


def blender_point(p):
    return Vector((p[0] * .01, -p[1] * .01, p[2] * .01))


def load_source(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1
    bpy.ops.import_scene.fbx(filepath=str(ART / 'SourceFBX' / ('SM_SC_' + name + '.fbx')), use_anim=False)
    meshes = [o for o in scene.objects if o.type == 'MESH']
    if len(meshes) != 1:
        raise RuntimeError('Expected one exported static mesh: ' + name)
    obj = meshes[0]
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    source = BASELINE['SM_SC_' + name]
    kd = KDTree(len(source['geometry']['points']))
    for i, p in enumerate(source['geometry']['points']):
        kd.insert(Vector(p), i)
    kd.balance()
    seed_by_id = {i: c['seed'] for c in source['components'] for i in c['ids']}
    matches = [kd.find(ue_point(v.co)) for v in obj.data.vertices]
    error = max(m[2] for m in matches)
    if error > .01:
        raise RuntimeError('Source coordinate roundtrip mismatch: %s %.6f cm' % (name, error))
    seeds = [seed_by_id[m[1]] for m in matches]
    return obj, source, seeds, error


def inspect():
    report = {}
    for name in NAMES:
        obj, src, seeds, error = load_source(name)
        report[name] = {'vertex_count': len(obj.data.vertices), 'source_vertex_count': src['vertices'],
                        'max_ue_coordinate_error_cm': error, 'uv_layers': [x.name for x in obj.data.uv_layers],
                        'materials': [m.name for m in obj.data.materials],
                        'has_custom_normals': obj.data.has_custom_normals,
                        'parts': [{'seed': c['seed'], 'v': len(c['ids']),
                                   'min': [round(x, 3) for x in c['min']],
                                   'max': [round(x, 3) for x in c['max']]} for c in src['components']]}
    dump(OUT / 'blender-source-inspection.json', report)
    print('TURRET_SOURCE_INSPECTION', json.dumps({n: {k: v for k, v in r.items() if k != 'parts'} for n, r in report.items()}))


def collision_audit(obj, seeds, spec, step=5):
    """Report surface crossings by source part; no collision assets are made."""
    moving = set(spec['moving'])
    obj.data.calc_loop_triangles()
    fixed_tris, moving_tris, fixed_seeds, moving_seeds = [], [], [], []
    for tri in obj.data.loop_triangles:
        ids = list(tri.vertices)
        classifications = {seeds[i] in moving for i in ids}
        if len(classifications) != 1:
            raise RuntimeError('A source face spans different rigid groups')
        if True in classifications:
            moving_tris.append(ids); moving_seeds.append(seeds[ids[0]])
        else:
            fixed_tris.append(ids); fixed_seeds.append(seeds[ids[0]])
    points = [v.co.copy() for v in obj.data.vertices]
    fixed_bvh = BVHTree.FromPolygons(points, fixed_tris, all_triangles=True)
    pivot = blender_point(spec['pivot'])
    rows = {}
    for angle in range(-15,76,step):
        q = Quaternion((1,0,0), math.radians(-spec['sign']*angle))
        posed = [pivot+q@(p-pivot) for p in points]
        bvh = BVHTree.FromPolygons(posed,moving_tris,all_triangles=True)
        pairs = fixed_bvh.overlap(bvh)
        by_part = {}
        for a,b in pairs:
            key = '%d/%d' % (fixed_seeds[a],moving_seeds[b])
            by_part[key] = by_part.get(key,0)+1
        rows[str(angle)] = by_part
    return rows


def source_piece(obj, indices, label):
    """Copy a subset, preserving UV channels and imported corner normals."""
    faces=[p for p in obj.data.polygons if p.index in indices]
    vids=sorted({i for p in faces for i in p.vertices});remap={v:i for i,v in enumerate(vids)}
    mesh=bpy.data.meshes.new(label)
    mesh.from_pydata([obj.data.vertices[i].co[:] for i in vids],[],[[remap[i] for i in p.vertices] for p in faces])
    mesh.update()
    source_loops=[i for p in faces for i in p.loop_indices]
    for layer in obj.data.uv_layers:
        dst=mesh.uv_layers.new(name=layer.name)
        for d,i in zip(dst.data,source_loops):d.uv=layer.data[i].uv
    for layer in obj.data.color_attributes:
        dst=mesh.color_attributes.new(name=layer.name,type=layer.data_type,domain=layer.domain)
        source_ids=source_loops if layer.domain=='CORNER' else vids
        for d,i in zip(dst.data,source_ids):d.color=layer.data[i].color
    for mat in obj.data.materials:mesh.materials.append(mat)
    normals=[obj.data.corner_normals[i].vector[:] for i in source_loops]
    mesh.normals_split_custom_set(normals)
    piece=bpy.data.objects.new(label,mesh);bpy.context.scene.collection.objects.link(piece)
    return piece


def swept_cutters(obj,seeds,spec):
    moving=set(spec['moving']);pivot=blender_point(spec['pivot'])
    lanes=spec['muzzles'] if len(spec['muzzles']) in [2,3] and 'Turret' in obj.name else [None]
    centers=[]
    source=BASELINE[obj.name]
    for seed in lanes:
        if seed is None:centers.append(0)
        else:
            part=next(p for p in source['components'] if p['seed']==seed)
            centers.append((part['min'][0]+part['max'][0])*.005)
    cutters=[]
    for lane,center in enumerate(centers):
        points=[v.co.copy() for v in obj.data.vertices if seeds[v.index] in moving
                and min(range(len(centers)),key=lambda i:abs(v.co.x-centers[i]))==lane]
        samples=set()
        for angle in range(-16,77):
            q=Quaternion((1,0,0),math.radians(-spec['sign']*angle))
            for p in points:
                v=pivot+q@(p-pivot)
                # One centimeter clearance in X and small radial safety margin.
                v.x=center+(v.x-center)*1.01
                v.y=pivot.y+(v.y-pivot.y)*1.004
                v.z=pivot.z+(v.z-pivot.z)*1.004
                samples.add(tuple(round(x,5) for x in v))
        bm=bmesh.new()
        for p in samples:bm.verts.new(p)
        result=bmesh.ops.convex_hull(bm,input=list(bm.verts),use_existing_faces=False)
        remove=[v for v in bm.verts if not v.link_faces]
        if remove:bmesh.ops.delete(bm,geom=remove,context='VERTS')
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        mesh=bpy.data.meshes.new('ClearanceSweep');bm.to_mesh(mesh);bm.free()
        cutter=bpy.data.objects.new('ClearanceSweep',mesh);bpy.context.scene.collection.objects.link(cutter)
        cutters.append(cutter)
    return cutters


def clearance_geometry(obj,seeds,spec,audit):
    """Cut only fixed parts crossed by the moving assembly's swept volume.

The cut faces form the inner lining of the new elevation openings. Original
moving parts keep their imported geometry, UVs and normals without alteration.
"""
    affected={int(key.split('/')[0]) for row in audit.values() for key in row}
    cutters=swept_cutters(obj,seeds,spec)
    moving=set(spec['moving']);parts=[];changes=[]
    for seed in sorted(set(seeds)):
        indices={p.index for p in obj.data.polygons if seeds[p.vertices[0]]==seed}
        part=source_piece(obj,indices,'Part_%d'%seed)
        before=len(part.data.polygons)
        if seed in affected:
            bm=bmesh.new();bm.from_mesh(part.data)
            boundary=[e for e in bm.edges if e.is_boundary]
            if boundary:bmesh.ops.holes_fill(bm,edges=boundary,sides=0)
            bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
            if boundary:bm.to_mesh(part.data)
            bm.free()
            bpy.context.view_layer.objects.active=part
            for cutter in cutters:
                mod=part.modifiers.new('ElevationClearance','BOOLEAN')
                mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
                mod.use_hole_tolerant=True
                bpy.ops.object.modifier_apply(modifier=mod.name)
            # Some Boolean results retain loose construction vertices.
            bm=bmesh.new();bm.from_mesh(part.data)
            loose=[v for v in bm.verts if not v.link_faces]
            if loose:bmesh.ops.delete(bm,geom=loose,context='VERTS');bm.to_mesh(part.data)
            bm.free()
            changes.append({'source_part':seed,'faces_before':before,'faces_after':len(part.data.polygons),
                            'boundary_edges_closed':len(boundary)})
        group=part.vertex_groups.new(name='BarrelPitch' if seed in moving else 'Root')
        if len(part.data.vertices):group.add(list(range(len(part.data.vertices))),1,'REPLACE')
        part['source_part_seed']=seed
        parts.append(part)
    for cutter in cutters:bpy.data.objects.remove(cutter,do_unlink=True)
    original_name=obj.name
    bpy.data.objects.remove(obj,do_unlink=True)
    parts=[p for p in parts if len(p.data.polygons)]
    new_seeds=[p['source_part_seed'] for p in parts for v in p.data.vertices]
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:p.select_set(True)
    bpy.context.view_layer.objects.active=parts[0]
    bpy.ops.object.join()
    result=parts[0];result.name=original_name
    # Merge identical material slots introduced by assembling the pieces.
    mat=result.data.materials[0]
    result.data.materials.clear();result.data.materials.append(mat)
    for p in result.data.polygons:p.material_index=0
    return result,new_seeds,changes


def preview_scene(obj, spec, output_prefix):
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.studiolight_rotate_z = .6
    scene.display.shading.color_type = 'MATERIAL'
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = 'BOTH'
    scene.display.shading.background_type = 'WORLD'
    scene.world = scene.world or bpy.data.worlds.new('TurretPreviewWorld')
    scene.world.color = (.04,.05,.065)
    scene.render.resolution_x = 960
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    cam = bpy.data.objects.new('PreviewCamera',bpy.data.cameras.new('PreviewCamera'))
    scene.collection.objects.link(cam); scene.camera=cam
    cam.data.type='ORTHO';cam.data.clip_end=10000
    return cam


def build_one(name, render=True):
    obj, src, seeds, error = load_source(name)
    spec = SPECS[name]
    moving = set(spec['moving'])
    if not moving.issubset({c['seed'] for c in src['components']}):
        raise RuntimeError('Invalid part seed for '+name)
    audit = collision_audit(obj,seeds,spec)
    obj,seeds,clearance=clearance_geometry(obj,seeds,spec,audit)
    bpy.context.view_layer.objects.active=obj
    triangulate=obj.modifiers.new('DeterministicTriangulation','TRIANGULATE')
    triangulate.quad_method='FIXED';triangulate.ngon_method='CLIP'
    if hasattr(triangulate,'keep_custom_normals'):triangulate.keep_custom_normals=True
    bpy.ops.object.modifier_apply(modifier=triangulate.name)
    final_audit=collision_audit(obj,seeds,spec,step=1)
    if any(row for row in final_audit.values()):
        raise RuntimeError('Residual fixed/moving surface crossings: '+name)
    # A single deform object with exactly two rigid weight groups.
    obj.name = 'SKM_SC_' + name
    obj.data.name = obj.name + '_Geometry'
    root_group = obj.vertex_groups.get('Root') or obj.vertex_groups.new(name='Root')
    barrel_group = obj.vertex_groups.get('BarrelPitch') or obj.vertex_groups.new(name='BarrelPitch')
    root_ids = [i for i,s in enumerate(seeds) if s not in moving]
    barrel_ids = [i for i,s in enumerate(seeds) if s in moving]
    expected=sorted(tuple(round(x,3) for x in src['geometry']['points'][i])
                    for c in src['components'] if c['seed'] in moving for i in c['ids'])
    actual=sorted(tuple(round(x,3) for x in ue_point(obj.data.vertices[i].co)) for i in barrel_ids)
    if len(expected)!=len(actual) or any(max(abs(a-b) for a,b in zip(p,q))>.002 for p,q in zip(expected,actual)):
        raise RuntimeError('Moving geometry or vertex partition changed: '+name)
    root_group.add(root_ids,1,'REPLACE');barrel_group.add(barrel_ids,1,'REPLACE')
    arm = bpy.data.objects.new('Armature',bpy.data.armatures.new('SK_SC_' + name))
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active=arm
    bpy.ops.object.select_all(action='DESELECT');arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    root=arm.data.edit_bones.new('Root');root.head=(0,0,0);root.tail=(1,0,0)
    root.align_roll(Vector((0,0,1)))
    barrel=arm.data.edit_bones.new('BarrelPitch')
    barrel.head=blender_point(spec['pivot']);barrel.tail=barrel.head+Vector((0,-1,0))
    barrel.parent=root;barrel.use_connect=False
    barrel.align_roll(Vector((0,0,spec['sign'])))
    bpy.ops.object.mode_set(mode='OBJECT')
    arm.show_in_front=True;arm.data.display_type='STICK'
    obj.parent=arm
    modifier=obj.modifiers.new('RigidTurretSkin','ARMATURE');modifier.object=arm
    points=src['geometry']['points']
    muzzles=[]
    for seed in spec['muzzles']:
        ids=next(c['ids'] for c in src['components'] if c['seed']==seed)
        tip=max(points[i][1] for i in ids)
        ring=[points[i] for i in ids if abs(points[i][1]-tip)<.025]
        p=[(min(v[a] for v in ring)+max(v[a] for v in ring))*.5 for a in range(3)]
        muzzles.append(p)
    muzzles.sort(key=lambda p:(p[0],p[2]))
    meta={'name':name,'source_coordinate_error_cm':error,'pivot_cm':list(spec['pivot']),
          'positive_elevation_mesh_z_sign':spec['sign'],'source_part_seeds':spec['moving'],
          'root_weighted_vertices':len(root_ids),'barrel_weighted_vertices':len(barrel_ids),
          'muzzles_cm':muzzles,'source_intersections':audit,
          'clearance_changes':clearance,'final_intersections':final_audit,
          'source_materials':src['materials'],'uv_channels':len(obj.data.uv_layers)}
    arm['turret_elevation_min']=-15.0;arm['turret_elevation_max']=75.0
    arm['positive_elevation_mesh_z_sign']=spec['sign']
    arm['source_asset']=src['path']
    obj['rigid_weights']='Root or BarrelPitch, exactly one influence at weight 1'
    for i,p in enumerate(muzzles):
        e=bpy.data.objects.new('Socket_'+str(i+1),None)
        bpy.context.scene.collection.objects.link(e)
        e.empty_display_type='ARROWS';e.empty_display_size=max(obj.dimensions)*.025
        # Reference marker only; real mesh sockets are authored in UE after import.
        e.location=blender_point(p);e.rotation_euler=(0,0,-math.pi/2)
        e['ue_socket_parent']='BarrelPitch';e.hide_render=True
    (ART/'Blender').mkdir(parents=True,exist_ok=True)
    (ART/'Exports').mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(ART/'Blender'/('SC_'+name+'.blend')))
    bpy.ops.object.select_all(action='DESELECT');arm.select_set(True);obj.select_set(True)
    bpy.context.view_layer.objects.active=arm
    bpy.ops.export_scene.fbx(filepath=str(ART/'Exports'/('SKM_SC_'+name+'.fbx')),
        use_selection=True,object_types={'ARMATURE','MESH'},use_mesh_modifiers=True,
        add_leaf_bones=False,bake_anim=False,axis_forward='-Y',axis_up='Z',
        primary_bone_axis='X',secondary_bone_axis='Z',use_armature_deform_only=True,
        apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='OFF',
        use_tspace=True,path_mode='STRIP')
    if render:
        cam=preview_scene(obj,spec,name)
        pb=arm.pose.bones['BarrelPitch']
        mats=list(obj.data.materials)
        orig_indices=[p.material_index for p in obj.data.polygons]
        fixedmat=bpy.data.materials.new('InspectionFixed');fixedmat.diffuse_color=(.30,.39,.43,1)
        movingmat=bpy.data.materials.new('InspectionMoving');movingmat.diffuse_color=(.70,.39,.16,1)
        obj.data.materials.clear();obj.data.materials.append(fixedmat);obj.data.materials.append(movingmat)
        for poly in obj.data.polygons:
            poly.material_index=int(seeds[poly.vertices[0]] in moving)
        # Uniform camera over the complete range, at the appropriate mounted side.
        positions=[v.co.copy() for v in obj.data.vertices if v.index in root_ids]
        pivot=blender_point(spec['pivot'])
        for angle in [-15,0,30,75]:
            positions += [pivot+Quaternion((1,0,0),math.radians(-spec['sign']*angle))@(v.co-pivot) for v in obj.data.vertices if v.index in barrel_ids]
        lo=Vector(tuple(min(p[a] for p in positions) for a in range(3)))
        hi=Vector(tuple(max(p[a] for p in positions) for a in range(3)))
        center=(lo+hi)*.5
        extent=max(hi-lo)
        cam.location=center+Vector((1.1,-1.5,1.1*spec['sign'])).normalized()*extent*3
        cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
        inverse_rotation=cam.rotation_euler.to_matrix().transposed()
        projected=[inverse_rotation@(p-center) for p in positions]
        pmin=Vector(tuple(min(p[a] for p in projected) for a in range(3)))
        pmax=Vector(tuple(max(p[a] for p in projected) for a in range(3)))
        cam.location+=cam.rotation_euler.to_matrix()@Vector(((pmin.x+pmax.x)*.5,(pmin.y+pmax.y)*.5,0))
        cam.data.ortho_scale=max(pmax.x-pmin.x,(pmax.y-pmin.y)*4/3)*1.18
        for angle in [-15,0,30,75]:
            delta=Matrix.Rotation(math.radians(-spec['sign']*angle),4,'X')
            pb.matrix=Matrix.Translation(pivot)@delta@Matrix.Translation(-pivot)@arm.data.bones['BarrelPitch'].matrix_local
            bpy.context.view_layer.update()
            bpy.context.scene.render.filepath=str(OUT/(name+'_%+03d.png'%angle))
            bpy.ops.render.render(write_still=True)
        pb.matrix_basis=Matrix.Identity(4)
        obj.data.materials.clear()
        for mat in mats:obj.data.materials.append(mat)
        for poly,i in zip(obj.data.polygons,orig_indices):poly.material_index=i
    dump(OUT/(name+'-authoring.json'),meta)
    print('TURRET_BUILT',name,json.dumps({'weights':[len(root_ids),len(barrel_ids)],'muzzles':muzzles,
        'sweep_poses':len(final_audit),'surface_crossings':sum(sum(p.values()) for p in final_audit.values())}),flush=True)
    return meta


def build(names=None,render=True):
    meta={name:build_one(name,render) for name in (names or NAMES)}
    dump(OUT/'authoring-manifest.json',meta)


def audit_sources():
    """Record final neutral vertices, rigid groups and the complete motion bounds.

    The JSON is consumed by the UE verification stage. Socket markers are also
    bone-parented in the editable sources so they follow manual preview poses.
    """
    manifest=json.loads((OUT/'authoring-manifest.json').read_text(encoding='utf-8'))
    result={}
    for name in NAMES:
        path=ART/'Blender'/('SC_'+name+'.blend')
        bpy.ops.wm.open_mainfile(filepath=str(path))
        obj=bpy.data.objects['SKM_SC_'+name]
        arm=bpy.data.objects['Armature']
        spec=SPECS[name]
        points=[list(ue_point(obj.matrix_world@v.co)) for v in obj.data.vertices]
        weights=[]
        for v in obj.data.vertices:
            groups=[g for g in v.groups if g.weight>0]
            assert len(groups)==1 and abs(groups[0].weight-1)<1e-6
            weights.append(obj.vertex_groups[groups[0].group].name)
        lo=[min(p[a] for p in points) for a in range(3)]
        hi=[max(p[a] for p in points) for a in range(3)]
        for angle_step in range(-60,301):
            angle=math.radians(angle_step/4)*spec['sign']
            c,s=math.cos(angle),math.sin(angle)
            for p,w in zip(points,weights):
                if w=='Root':continue
                dy,dz=p[1]-spec['pivot'][1],p[2]-spec['pivot'][2]
                q=[p[0],spec['pivot'][1]+c*dy-s*dz,spec['pivot'][2]+s*dy+c*dz]
                lo=[min(a,b) for a,b in zip(lo,q)]
                hi=[max(a,b) for a,b in zip(hi,q)]
        for e in [o for o in bpy.data.objects if o.type=='EMPTY' and o.name.startswith('Socket_')]:
            world=e.matrix_world.copy()
            e.parent=arm;e.parent_type='BONE';e.parent_bone='BarrelPitch'
            bpy.context.view_layer.update()
            e.matrix_world=world
        bpy.context.view_layer.update()
        bpy.ops.wm.save_as_mainfile(filepath=str(path))
        result[name]={'points_cm':points,'weights':weights,'motion_bounds_cm':[lo,hi],
                      'reference_bounds_cm':[[min(p[a] for p in points) for a in range(3)],
                                             [max(p[a] for p in points) for a in range(3)]],
                      'uv_channels':len(obj.data.uv_layers),
                      'triangles':len(obj.data.polygons)}
        manifest[name]['motion_bounds_cm']=[lo,hi]
        dump(OUT/(name+'-authoring.json'),manifest[name])
    dump(OUT/'final-source-geometry.json',result)
    dump(OUT/'authoring-manifest.json',manifest)
    print('TURRET_SOURCE_AUDIT_COMPLETE',len(result),flush=True)


if __name__ == '__main__':
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else ['inspect']
    if args[0]=='inspect':inspect()
    elif args[0]=='audit-sources':audit_sources()
    else:build([n for n in args[1:] if n in NAMES] or None,render='--no-render' not in args)
