"""Apply the six user annotations to the open B2 without resetting the scene."""
from pathlib import Path
from datetime import datetime
import bpy,bmesh,runpy,json,math,hashlib,shutil
from mathutils import Vector
from mathutils.bvhtree import BVHTree

HERE=Path(__file__).parent
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2')
PREFIX='GMA4B2_';TARGET=OUT/'GroundMech_A4B2_Editable_B_v2.blend'
FEEDBACK=[
 ('9508ca33-c2bf-4fe8-bb82-c8fbcdfd440c','GunLength','机枪调长50%'),
 ('9ad35723-4509-4b24-acb4-73e498a26a43','GunModule','机枪是个独立部件，需要拆分一下，现在都融合起来了'),
 ('4728e009-0aa3-4c9f-82dd-3cbcf8823834','FittedPanels','这些丑陋的矩形面处理一下，适配模型'),
 ('73c46d6a-d22c-45ef-bdc1-ef7b8332e3b3','ArmFrame','后面这个架子收敛一些，现在太突兀了'),
 ('9135c608-7d53-4e24-a09d-9d45afc2ba41','LegGuards','去看参考图，这一块是怎么做的'),
 ('9206f763-15fe-4e24-a519-9386f6b46977','FootSeparation','这一块都融合起来了，需要做一下分离'),
]


def rig_contract(rig):
    return {'bones':[(b.name,b.parent.name if b.parent else None,[list(r) for r in b.matrix_local]) for b in rig.data.bones],
            'drivers':[(d.data_path,d.array_index,d.driver.expression) for d in rig.animation_data.drivers] if rig.animation_data else [],
            'pose':[(b.name,list(b.location),list(b.rotation_euler),list(b.scale)) for b in rig.pose.bones]}


def inside(tree,point):
    direction=Vector((.93673,.24391,.25173)).normalized();p=Vector(point);n=0
    for _ in range(64):
        loc,normal,index,dist=tree.ray_cast(p,direction,100)
        if loc is None:break
        n+=1;p=loc+direction*.00001
    return bool(n%2)


def main():
    assert Path(bpy.data.filepath).resolve()==TARGET.resolve()
    assert bpy.context.mode=='OBJECT'
    helpers=runpy.run_path(str(HERE/'repair_b2_heel_connection.py'),run_name='revision_helpers')
    ns=runpy.run_path(str(HERE/'build_a4_b2.py'),run_name='annotation_definitions');scope=ns['mesh'].__globals__
    rigs={k:bpy.data.objects[PREFIX+'Rig_'+k] for k in ('BLUE','RED')}
    contracts={k:rig_contract(rig) for k,rig in rigs.items()}
    original={o.name:helpers['mesh_digest'](o) for o in bpy.data.objects if o.type=='MESH'}
    actions=sorted(a.name for a in bpy.data.actions)
    dirty_before=bpy.data.is_dirty
    backup=OUT/'Backups'/('Before_SixAnnotations_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.blend')
    backup.parent.mkdir(exist_ok=True)
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(backup),copy=True)
    win=bpy.context.window or bpy.context.window_manager.windows[0]
    previous_scene=win.scene.name
    win.scene=bpy.data.scenes['GroundMech_A4B2_Construction']
    roles=('TeamPrimary','TeamSecondary','Cream','Sand','Mechanism','Ink','Amber','Outline')
    mats={r:bpy.data.materials[PREFIX+('Blue_'+r if r.startswith('Team') else r)] for r in roles}
    old_report=json.loads((OUT/'geometry_report.json').read_text(encoding='utf8'))
    scope.update(RIG=rigs['BLUE'],MATS=mats,PARTS=[],SOCKETS=old_report['sockets'],
        GROUPS={g:bpy.data.collections[PREFIX+g] for g in ('Arms_R','Arms_L','Legs')})
    made_parts=ns['build_annotation_revision']()
    body=[];added=[];changed=[]
    for suffix,made in made_parts.items():
        name=PREFIX+suffix;existing=bpy.data.objects.get(name)
        if existing is not None and existing!=made:
            obj=existing;old=obj.data
            assert {o.name for o in bpy.data.objects if o.type=='MESH' and o.data==old}<={name,name+'_RED'}
            obj.data=made.data
            for key,value in made.items():obj[key]=value
            for obsolete in ('heel_overlap_local_y_m',):
                if obsolete in obj:del obj[obsolete]
            for mod in list(obj.modifiers):
                if mod.type in ('BEVEL','WEIGHTED_NORMAL'):obj.modifiers.remove(mod)
            for source in made.modifiers:
                if source.type=='BEVEL':
                    m=obj.modifiers.new(source.name,'BEVEL');m.width=source.width;m.segments=source.segments;m.use_clamp_overlap=True
                    obj.modifiers.move(len(obj.modifiers)-1,0)
                elif source.type=='WEIGHTED_NORMAL':
                    m=obj.modifiers.new(source.name,'WEIGHTED_NORMAL');m.keep_sharp=True;m.weight=50
                    obj.modifiers.move(len(obj.modifiers)-1,1)
            bpy.data.objects.remove(made,do_unlink=True);changed.append(name)
        else:obj=made;added.append(name)
        # Meshes encode the new material roles. Clear stale object slot overrides.
        for slot in obj.material_slots:slot.link='DATA'
        obj.vertex_groups.clear();vg=obj.vertex_groups.new(name=obj['rigid_bone'])
        vg.add(list(range(len(obj.data.vertices))),1,'REPLACE')
        body.append(obj)

    bpy.ops.object.select_all(action='DESELECT')
    for obj in body:obj.select_set(True)
    bpy.context.view_layer.objects.active=body[0]
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.03)
    bpy.ops.object.mode_set(mode='OBJECT');bpy.context.view_layer.update()
    deps=bpy.context.evaluated_depsgraph_get();geometry=[];shells=[];cache={}
    for obj in body:
        me=helpers['evaluated_mesh'](obj,deps);me.calc_loop_triangles()
        bm=bmesh.new();bm.from_mesh(me);nonmanifold=sum(not e.is_manifold for e in bm.edges);bm.free()
        degenerate=sum(t.area<1e-10 for t in me.loop_triangles)
        rigid=all(len(v.groups)==1 and abs(v.groups[0].weight-1)<1e-6 for v in obj.data.vertices)
        assert not nonmanifold and not degenerate and rigid,(obj.name,nonmanifold,degenerate,rigid)
        geometry.append({'name':obj.name,'bone':obj['rigid_bone'],'triangles':len(me.loop_triangles),
                         'nonmanifold_edges':nonmanifold,'degenerate_triangles':degenerate,'rigid_weight_1':rigid,'uv_layers':len(me.uv_layers)})
        cache[obj.name]=me
        if obj.get('color_role') in ('Ink','Amber'):continue
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
            shell.parent=rigs['BLUE'];shell['author']=scope['OWNER'];shell['source_part']=obj.name
            shell['outline_only']=True
            m=shell.modifiers.new('Rigid shell skin','ARMATURE');m.object=rigs['BLUE']
        shell['rigid_bone']=obj['rigid_bone'];shell['assembly_revision']=scope['ANNOTATION_REVISION']
        shell.vertex_groups.clear();vg=shell.vertex_groups.new(name=obj['rigid_bone'])
        vg.add(list(range(len(outline.vertices))),1,'REPLACE')
        for attr in ('visible_shadow','visible_diffuse','visible_glossy','visible_transmission','visible_volume_scatter'):setattr(shell,attr,False)
        shells.append(shell)

    red=bpy.data.collections[PREFIX+'TEAM_RED']
    for obj in body+shells:
        copy=bpy.data.objects.get(obj.name+'_RED')
        if copy:copy.data=obj.data
        else:copy=obj.copy();copy.name=obj.name+'_RED';red.objects.link(copy)
        copy.parent=rigs['RED']
        for key,value in obj.items():copy[key]=value
        copy.vertex_groups.clear();vg=copy.vertex_groups.new(name=obj['rigid_bone'])
        vg.add(list(range(len(obj.data.vertices))),1,'REPLACE')
        # Match the complete modifier list, including changed bevel widths.
        for m in list(copy.modifiers):copy.modifiers.remove(m)
        for source in obj.modifiers:
            m=copy.modifiers.new(source.name,source.type)
            if source.type=='BEVEL':m.width=source.width;m.segments=source.segments;m.use_clamp_overlap=source.use_clamp_overlap
            elif source.type=='WEIGHTED_NORMAL':m.keep_sharp=True;m.weight=50
            elif source.type=='ARMATURE':m.object=rigs['RED'];m.use_vertex_groups=True
        for slot in copy.material_slots:
            slot.link='DATA'
            role=slot.material.get('color_role') if slot.material else None
            if role in ('TeamPrimary','TeamSecondary'):
                slot.link='OBJECT';slot.material=bpy.data.materials[PREFIX+'Red_'+role]

    # Separate editable weapon collections; retain the source rigid bone contract.
    for side in ('R','L'):
        for team in ('BLUE','RED'):
            suffix='' if team=='BLUE' else '_RED'
            name=PREFIX+'Weapon_Module_'+side+suffix
            col=bpy.data.collections.get(name)
            if col is None:col=bpy.data.collections.new(name)
            parent=bpy.data.collections[PREFIX+'Arms_'+side] if team=='BLUE' else red
            if col.name not in parent.children:parent.children.link(col)
            col['assembly']='Independent cannon casing, cradle, coupling and rotating barrel'
            for obj in body:
                if not obj.name.startswith(PREFIX+'Cannon_') or obj['assembly']!='Weapon_Module_'+side:continue
                target=bpy.data.objects[obj.name+suffix]
                if target.name not in col.objects:col.objects.link(target)
                for owner in list(target.users_collection):
                    if owner!=col:owner.objects.unlink(target)

    # Update both team's muzzle empties from bone-local bind coordinates.
    sockets=[]
    for spec in scope['SOCKETS']:
        if spec['name'] not in ('Muzzle','Muzzle_L'):continue
        rest=Vector(spec['direction']).to_track_quat('X','Z').to_matrix().to_4x4()
        rest.translation=Vector(spec['position'])
        local=rigs['BLUE'].data.bones[spec['bone']].matrix_local.inverted()@rest
        spec['bone_local_matrix']=[list(r) for r in local]
        for team,rig in rigs.items():
            obj=bpy.data.objects[PREFIX+spec['name']+('' if team=='BLUE' else '_RED')]
            expected=rig.matrix_world@rig.pose.bones[spec['bone']].matrix@local
            obj.matrix_world=expected;bpy.context.view_layer.update()
            error=max(abs(obj.matrix_world[i][j]-expected[i][j]) for i in range(4) for j in range(4))
            assert error<1e-5
            sockets.append({'name':obj.name,'bone':obj.parent_bone,'position':list(obj.matrix_world.translation),'matrix_error':error})

    # Dimensional and assembly checks use meshes only, never output images.
    bpy.context.view_layer.update();trees={};bounds={}
    def data(name):
        key=PREFIX+name
        if key not in cache:cache[key]=helpers['evaluated_mesh'](bpy.data.objects[key],deps)
        me=cache[key]
        if key not in trees:
            vs=[v.co.copy() for v in me.vertices]
            trees[key]=BVHTree.FromPolygons(vs,[list(p.vertices) for p in me.polygons],all_triangles=False)
            bounds[key]=([min(v[i] for v in vs) for i in range(3)],[max(v[i] for v in vs) for i in range(3)])
        return trees[key],bounds[key]
    def overlap(a,b):
        ta,ba=data(a);tb,bb=data(b)
        lo=[max(ba[0][i],bb[0][i])+1e-5 for i in range(3)]
        hi=[min(ba[1][i],bb[1][i])-1e-5 for i in range(3)]
        if any(lo[i]>=hi[i] for i in range(3)):return None
        fractions=(.15,.3,.5,.7,.85)
        for x in fractions:
            for y in fractions:
                for z in fractions:
                    p=Vector(tuple(lo[i]+(hi[i]-lo[i])*f for i,f in enumerate((x,y,z))))
                    if inside(ta,p) and inside(tb,p):return list(p)
        return None
    pairs=[('Cannon_IndependentMount','Elbow_DarkAxle'),('Cannon_IndependentMount','Cannon_Volume'),
           ('Cannon_Volume','Cannon_MuzzleCollars'),('Cannon_MuzzleCollars','Cannon_OpenBore'),
           ('UpperArm_SideCream','UpperArm_SideMount'),('UpperArm_SideMount','UpperArm_ForwardArmor'),
           ('Hip_OuterSand','Hip_BlueSideFacet'),('Knee_OuterSandArmor','Knee_CapMount'),
           ('Knee_InnerBlueArmor','Knee_InnerCapMount'),('Knee_InnerCapMount','Shin_HiddenCore'),
           ('Shin_SandSideInsert','Calf_BackArmor'),('Ankle_RearGuard','Ankle_DarkBearing'),
           ('Ankle_LargeBlueBoot','Foot_InstepLink'),('Foot_BlueBridge','Foot_RecessedChassis'),
           ('Foot_RecessedChassis','Ankle_DarkBearing'),('Foot_RecessedChassis','Foot_IntegratedHeel')]
    contacts=[]
    for side in ('R','L'):
        for a,b in pairs:
            a+='_'+side;b+='_'+side;p=overlap(a,b)
            contacts.append({'a':a,'b':b,'connected':p is not None,'common_interior_point':p})
        for i in range(2):
            a='Foot_RecessedChassis_'+side;b='Foot_SeparateToe_%s_%s'%(side,i);p=overlap(a,b)
            contacts.append({'a':a,'b':b,'connected':p is not None,'common_interior_point':p})
        for word in ('A','B'):
            a='Cannon_CreamSide_'+side+word;b='Cannon_Volume_'+side;p=overlap(a,b)
            contacts.append({'a':a,'b':b,'connected':p is not None,'common_interior_point':p})
    dimensions=[]
    for sign,side in [(-1,'R'),(1,'L')]:
        c=Vector((sign*1.72,-.66,3.045));d=Vector((sign*.40,-.91,-.18)).normalized()
        vs=[v.co for part in ('Cannon_IndependentMount','Cannon_Volume','Cannon_MuzzleCollars','Cannon_OpenBore')
            for v in bpy.data.objects[PREFIX+part+'_'+side].data.vertices]
        axial=[(v-c).dot(d) for v in vs];length=max(axial)-min(axial)
        ratio=length/(scope['GUN_OLD_FRONT']-scope['GUN_REAR'])
        assert abs(ratio-1.5)<1e-5,(side,ratio)
        boot=bpy.data.objects[PREFIX+'Ankle_LargeBlueBoot_'+side]
        bridge=bpy.data.objects[PREFIX+'Foot_BlueBridge_'+side]
        gap=min(v.co.z for v in boot.data.vertices)-max(v.co.z for v in bridge.data.vertices)
        assert gap>.05
        dimensions.append({'side':side,'gun_old_length_m':1.345,'gun_new_length_m':length,'length_factor':ratio,
                           'muzzle_outer_radius_m':.22,'muzzle_inner_radius_m':.15,
                           'muzzle_translation_m':scope['GUN_EXTENSION'],'upper_boot_to_lower_ledge_min_z_gap_m':gap})
    touched={o.name for o in body+shells}|{o.name+'_RED' for o in body+shells}
    untouched={n:v for n,v in original.items() if n not in touched}
    after={o.name:helpers['mesh_digest'](o) for o in bpy.data.objects if o.type=='MESH' and o.name not in touched}
    assert untouched==after,'Unrelated geometry changed'
    assert contracts=={k:rig_contract(rig) for k,rig in rigs.items()}
    assert actions==sorted(a.name for a in bpy.data.actions)
    feedback=[]
    for id,slug,text in FEEDBACK:
        src=Path('C:/Users/a/AppData/Local/Temp')/('codex-clipboard-'+id+'.png')
        dst=OUT/'References'/('User_B2_'+slug+'.png')
        if src.exists():shutil.copy2(src,dst)
        feedback.append({'text':text,'path':'References/'+dst.name,'source':str(src),'sha256':hashlib.sha256(dst.read_bytes()).hexdigest()})
    failed=[c for c in contacts if not c['connected']]
    report={'success':not failed,'revision':scope['ANNOTATION_REVISION'],'backup':str(backup),'preserved_unsaved_state':dirty_before,
            'previous_scene':previous_scene,'changed_parts':changed,'added_parts':added,'dimensions':dimensions,'contacts':contacts,
            'contact_failures':failed,'geometry':geometry,'sockets':sockets,'source_socket_specs':scope['SOCKETS'],
            'user_feedback':feedback,'bones_and_drivers_unchanged':True,'unrelated_geometry_unchanged':True,
            'shared_red_geometry':True,'output_image_inspection':False,'approval_B':'changes_requested'}
    (OUT/'annotation_revision_working.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    for me in cache.values():bpy.data.meshes.remove(me)
    assert not failed,failed
    # A dedicated editable view exposes the elongated modules and split foot.
    bpy.ops.object.select_all(action='DESELECT')
    for side in ('R','L'):bpy.data.objects[PREFIX+'Cannon_Volume_'+side].select_set(True)
    bpy.context.view_layer.objects.active=bpy.data.objects[PREFIX+'Cannon_Volume_R']
    for area in win.screen.areas:
        if area.type=='VIEW_3D':
            space=area.spaces.active;space.shading.type='MATERIAL';space.shading.use_scene_world=True
            space.overlay.show_overlays=False
            target=Vector((0,-.10,2.44));eye=Vector((8,-14,8))
            space.region_3d.view_location=target;space.region_3d.view_rotation=(target-eye).to_track_quat('-Z','Y')
            space.region_3d.view_distance=9.5;space.region_3d.view_perspective='ORTHO'
    bpy.context.view_layer.update()
    scope.update(SCENE=win.scene,BLUE=bpy.data.collections[PREFIX+'TEAM_BLUE'],
                 PARTS=[o for o in bpy.data.collections[PREFIX+'TEAM_BLUE'].all_objects if o.type=='MESH' and not o.get('outline_only')])
    geom=ns['audit_geometry']([o for o in bpy.data.collections[PREFIX+'TEAM_BLUE'].all_objects if o.get('outline_only')],rigs['RED'])
    assert geom['success'],geom['errors']
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(TARGET))
    report['blend']=str(TARGET);report['sha256']=hashlib.sha256(TARGET.read_bytes()).hexdigest()
    report['body_parts']=geom['body_parts'];report['body_triangles']=geom['body_triangles']
    (OUT/'annotation_revision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return {k:report[k] for k in ('success','revision','backup','changed_parts','added_parts','dimensions','contact_failures','sha256','body_parts','body_triangles')}


if __name__=='__main__':
    result=main()
    print(json.dumps(result,ensure_ascii=False))
