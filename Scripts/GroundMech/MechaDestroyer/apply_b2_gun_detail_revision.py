"""Apply only the reference-led cannon revision to the current editable B2."""
from pathlib import Path
from datetime import datetime
import bpy,bmesh,runpy,math,json,hashlib
from mathutils import Vector
from mathutils.bvhtree import BVHTree

HERE=Path(__file__).parent
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2')
TARGET=OUT/'GroundMech_A4B2_Editable_B_v2.blend';PREFIX='GMA4B2_'


def main():
    assert Path(bpy.data.filepath).resolve()==TARGET.resolve() and bpy.context.mode=='OBJECT'
    helpers=runpy.run_path(str(HERE/'repair_b2_heel_connection.py'),run_name='solid_helpers')
    previous=runpy.run_path(str(HERE/'apply_b2_annotation_revision.py'),run_name='contract_helpers')
    ns=runpy.run_path(str(HERE/'build_a4_b2.py'),run_name='detail_definitions');scope=ns['mesh'].__globals__
    rigs={t:bpy.data.objects[PREFIX+'Rig_'+t] for t in ('BLUE','RED')}
    contracts={t:previous['rig_contract'](r) for t,r in rigs.items()}
    pose_modes={t:r.data.pose_position for t,r in rigs.items()}
    before={o.name:helpers['mesh_digest'](o) for o in bpy.data.objects if o.type=='MESH'}
    sockets={o.name:[list(row) for row in o.matrix_basis] for o in bpy.data.objects if o.type=='EMPTY' and o.name.startswith(PREFIX+'Muzzle')}
    actions=sorted(a.name for a in bpy.data.actions)
    was_dirty=bpy.data.is_dirty
    backup=OUT/'Backups'/('Before_GunReferenceDetail_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.blend')
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(backup),copy=True)
    win=bpy.context.window or bpy.context.window_manager.windows[0]
    win.scene=bpy.data.scenes['GroundMech_A4B2_Construction']
    for rig in rigs.values():rig.data.pose_position='REST'
    roles=('TeamPrimary','TeamSecondary','Cream','Sand','Mechanism','Ink','Amber','Outline')
    mats={r:bpy.data.materials[PREFIX+('Blue_'+r if r.startswith('Team') else r)] for r in roles}
    scope.update(RIG=rigs['BLUE'],MATS=mats,PARTS=[],GROUPS={g:bpy.data.collections[PREFIX+g] for g in ('Arms_R','Arms_L')})
    made_parts=ns['build_gun_detail_revision']();body=[];added=[];changed=[]
    for suffix,made in made_parts.items():
        name=PREFIX+suffix;obj=bpy.data.objects.get(name)
        if obj is not None and obj!=made:
            old=obj.data
            assert {o.name for o in bpy.data.objects if o.type=='MESH' and o.data==old}<={name,name+'_RED'}
            obj.data=made.data
            for key,value in made.items():obj[key]=value
            for mod in list(obj.modifiers):obj.modifiers.remove(mod)
            for src in made.modifiers:
                mod=obj.modifiers.new(src.name,src.type)
                if src.type=='BEVEL':mod.width=src.width;mod.segments=src.segments;mod.use_clamp_overlap=True
                elif src.type=='WEIGHTED_NORMAL':mod.keep_sharp=True;mod.weight=50
                elif src.type=='ARMATURE':mod.object=rigs['BLUE'];mod.use_vertex_groups=True
            bpy.data.objects.remove(made,do_unlink=True);changed.append(name)
        else:obj=made;added.append(name)
        for slot in obj.material_slots:slot.link='DATA'
        obj.vertex_groups.clear();vg=obj.vertex_groups.new(name=obj['rigid_bone']);vg.add(list(range(len(obj.data.vertices))),1,'REPLACE')
        col=bpy.data.collections[PREFIX+obj['assembly']]
        if obj.name not in col.objects:col.objects.link(obj)
        for old_col in list(obj.users_collection):
            if old_col!=col:old_col.objects.unlink(obj)
        body.append(obj)
    bpy.context.view_layer.update();bpy.ops.object.select_all(action='DESELECT')
    for obj in body:obj.select_set(True)
    bpy.context.view_layer.objects.active=body[0]
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.03)
    bpy.ops.object.mode_set(mode='OBJECT');bpy.context.view_layer.update()
    deps=bpy.context.evaluated_depsgraph_get();geometry=[];shells=[];cache={}
    for obj in body:
        me=helpers['evaluated_mesh'](obj,deps);me.calc_loop_triangles();cache[obj.name]=me
        bm=bmesh.new();bm.from_mesh(me);nonmanifold=sum(not e.is_manifold for e in bm.edges);bm.free()
        degenerate=sum(t.area<1e-10 for t in me.loop_triangles)
        rigid=all(len(v.groups)==1 and abs(v.groups[0].weight-1)<1e-6 for v in obj.data.vertices)
        assert not nonmanifold and not degenerate and rigid,(obj.name,nonmanifold,degenerate,rigid)
        geometry.append({'name':obj.name,'bone':obj['rigid_bone'],'triangles':len(me.loop_triangles),
                         'nonmanifold_edges':nonmanifold,'degenerate_triangles':degenerate,'rigid_weight_1':rigid,'uv_layers':len(me.uv_layers)})
        if obj['color_role'] in ('Ink','Amber'):continue
        outline=me.copy()
        for v,n in zip(outline.vertices,[v.normal.copy() for v in outline.vertices]):v.co+=n*.009
        bm=bmesh.new();bm.from_mesh(outline);deform=bm.verts.layers.deform.active
        if deform:
            for v in bm.verts:v[deform].clear()
        bmesh.ops.reverse_faces(bm,faces=list(bm.faces));bm.to_mesh(outline);bm.free()
        outline.materials.clear();outline.materials.append(mats['Outline'])
        for p in outline.polygons:p.material_index=0
        shell=bpy.data.objects.get(obj.name+'_OUTLINE')
        if shell:shell.data=outline
        else:
            shell=bpy.data.objects.new(obj.name+'_OUTLINE',outline)
            bpy.data.collections[PREFIX+'Silhouette_Only'].objects.link(shell)
            shell.parent=rigs['BLUE'];shell['author']=scope['OWNER'];shell['source_part']=obj.name;shell['outline_only']=True
            mod=shell.modifiers.new('Rigid shell skin','ARMATURE');mod.object=rigs['BLUE']
        shell['rigid_bone']=obj['rigid_bone'];shell['assembly_revision']=scope['GUN_DETAIL_REVISION']
        shell.vertex_groups.clear();vg=shell.vertex_groups.new(name=obj['rigid_bone']);vg.add(list(range(len(outline.vertices))),1,'REPLACE')
        for attr in ('visible_shadow','visible_diffuse','visible_glossy','visible_transmission','visible_volume_scatter'):setattr(shell,attr,False)
        shells.append(shell)
    for obj in body+shells:
        red=bpy.data.objects.get(obj.name+'_RED')
        if red:red.data=obj.data
        else:
            red=obj.copy();red.name=obj.name+'_RED'
            col=bpy.data.collections[PREFIX+obj['assembly']+'_RED'] if not obj.get('outline_only') else bpy.data.collections[PREFIX+'TEAM_RED']
            col.objects.link(red)
        red.parent=rigs['RED']
        for key,value in obj.items():red[key]=value
        red.vertex_groups.clear();vg=red.vertex_groups.new(name=obj['rigid_bone']);vg.add(list(range(len(obj.data.vertices))),1,'REPLACE')
        for mod in list(red.modifiers):red.modifiers.remove(mod)
        for src in obj.modifiers:
            mod=red.modifiers.new(src.name,src.type)
            if src.type=='BEVEL':mod.width=src.width;mod.segments=src.segments;mod.use_clamp_overlap=True
            elif src.type=='WEIGHTED_NORMAL':mod.keep_sharp=True;mod.weight=50
            elif src.type=='ARMATURE':mod.object=rigs['RED'];mod.use_vertex_groups=True
        for slot in red.material_slots:
            slot.link='DATA';role=slot.material.get('color_role') if slot.material else None
            if role in ('TeamPrimary','TeamSecondary'):slot.link='OBJECT';slot.material=bpy.data.materials[PREFIX+'Red_'+role]

    # Solid intersection checks for the new mounting and raised armor parts.
    trees={};bounds={}
    def get(name):
        name=PREFIX+name
        if name not in cache:cache[name]=helpers['evaluated_mesh'](bpy.data.objects[name],deps)
        if name not in trees:
            me=cache[name];vs=[v.co.copy() for v in me.vertices]
            trees[name]=BVHTree.FromPolygons(vs,[list(p.vertices) for p in me.polygons],all_triangles=False)
            bounds[name]=([min(v[i] for v in vs) for i in range(3)],[max(v[i] for v in vs) for i in range(3)])
        return trees[name],bounds[name]
    def contact(a,b):
        ta,ba=get(a);tb,bb=get(b)
        lo=[max(ba[0][i],bb[0][i])+1e-5 for i in range(3)];hi=[min(ba[1][i],bb[1][i])-1e-5 for i in range(3)]
        if any(lo[i]>=hi[i] for i in range(3)):return None
        for x in (.12,.3,.5,.7,.88):
            for y in (.12,.3,.5,.7,.88):
                for z in (.12,.3,.5,.7,.88):
                    p=Vector(tuple(lo[i]+(hi[i]-lo[i])*f for i,f in enumerate((x,y,z))))
                    if previous['inside'](ta,p) and previous['inside'](tb,p):return list(p)
        return None
    contacts=[];dimensions=[]
    for sign,label in [(-1,'R'),(1,'L')]:
        pairs=[('Cannon_FrontArmorCollar_'+label,'Cannon_Volume_'+label),
               ('Cannon_TopReceiver_'+label,'Cannon_Volume_'+label),
               ('Cannon_TopRearLatch_'+label,'Cannon_TopReceiver_'+label),
               ('Cannon_MuzzleCollars_'+label,'Cannon_Volume_'+label),
               ('Cannon_MuzzleCollars_'+label,'Cannon_OpenBore_'+label),
               ('Cannon_VisibleAmberCore_'+label,'Cannon_OpenBore_'+label)]
        for word in ('A','B'):
            pairs.extend([('Cannon_SideCheek_'+label+word,'Cannon_Volume_'+label),
                          ('Cannon_CreamSide_'+label+word,'Cannon_SideCheek_'+label+word)])
        for side in (-1,1):
            pairs.extend([('Cannon_OpenLowerRail_%s_%s'%(label,side),'Cannon_CageRear_%s_%s'%(label,side)),
                          ('Cannon_OpenLowerRail_%s_%s'%(label,side),'Cannon_CageFront_'+label)])
        for a,b in pairs:
            p=contact(a,b);contacts.append({'a':a,'b':b,'connected':p is not None,'common_interior_point':p})
        c=Vector((sign*1.72,-.66,3.045));d=Vector((sign*.40,-.91,-.18)).normalized()
        vertices=[v.co for o in bpy.data.collections[PREFIX+'Weapon_Module_'+label].objects for v in o.data.vertices]
        axial=[(v-c).dot(d) for v in vertices];length=max(axial)-min(axial)
        assert abs(length-2.0175)<1e-5
        dimensions.append({'side':label,'axial_length_m':length,'length_factor_vs_original':length/1.345,
                           'muzzle_front_axial_m':max(axial),'nominal_outer_bore_radius_m':.22,'entrance_radius_m':.15})
    touched={o.name for o in body+shells}|{o.name+'_RED' for o in body+shells}
    assert {n:h for n,h in before.items() if n not in touched}=={o.name:helpers['mesh_digest'](o) for o in bpy.data.objects if o.type=='MESH' and o.name not in touched}
    assert sockets=={o.name:[list(row) for row in o.matrix_basis] for o in bpy.data.objects if o.name in sockets}
    assert contracts=={t:previous['rig_contract'](r) for t,r in rigs.items()}
    assert actions==sorted(a.name for a in bpy.data.actions)
    failures=[x for x in contacts if not x['connected']]
    report={'revision':scope['GUN_DETAIL_REVISION'],'success':not failures,'backup':str(backup),'preserved_unsaved_state':was_dirty,
            'user_request':'枪的细节再多一点，跟参考图对齐','references':['References/Approved_A4_Concept.png','References/Approved_UpperBody_Detail.png'],
            'changed_parts':changed,'added_parts':added,'geometry':geometry,'contacts':contacts,'contact_failures':failures,'dimensions':dimensions,
            'bones_drivers_actions_and_sockets_unchanged':True,'non_weapon_geometry_unchanged':True,'shared_red_geometry':True,
            'assistant_output_image_inspection':False,'approval_B':'changes_requested'}
    (OUT/'gun_detail_revision_working.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    for me in cache.values():bpy.data.meshes.remove(me)
    for t,rig in rigs.items():rig.data.pose_position=pose_modes[t]
    bpy.context.view_layer.update()
    assert not failures,failures
    # Focus the editable view on the reference-driven cannon detailing.
    bpy.ops.object.select_all(action='DESELECT')
    bpy.data.objects[PREFIX+'Cannon_Volume_R'].select_set(True)
    bpy.context.view_layer.objects.active=bpy.data.objects[PREFIX+'Cannon_Volume_R']
    for area in win.screen.areas:
        if area.type=='VIEW_3D':
            space=area.spaces.active;space.shading.type='MATERIAL';space.shading.use_scene_world=True;space.overlay.show_overlays=False
            target=Vector((-1.92,-1.10,3.40));eye=Vector((-10,-13,7))
            space.region_3d.view_location=target;space.region_3d.view_rotation=(target-eye).to_track_quat('-Z','Y')
            space.region_3d.view_distance=5.0;space.region_3d.view_perspective='ORTHO'
    old_report=json.loads((OUT/'geometry_report.json').read_text(encoding='utf8'))
    blue=bpy.data.collections[PREFIX+'TEAM_BLUE']
    scope.update(SCENE=win.scene,BLUE=blue,PARTS=[o for o in blue.all_objects if o.type=='MESH' and not o.get('outline_only')],SOCKETS=old_report['sockets'])
    geom=ns['audit_geometry']([o for o in blue.all_objects if o.get('outline_only')],rigs['RED'])
    assert geom['success'],geom['errors']
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(TARGET))
    report.update(blend=str(TARGET),sha256=hashlib.sha256(TARGET.read_bytes()).hexdigest(),body_parts=geom['body_parts'],body_triangles=geom['body_triangles'])
    (OUT/'gun_detail_revision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return {k:report[k] for k in ('success','revision','backup','changed_parts','added_parts','contact_failures','dimensions','sha256','body_parts','body_triangles')}


if __name__=='__main__':
    result=main()
    print(json.dumps(result,ensure_ascii=False))
