"""Apply A4 back armor corrections to the open B2, keeping other edits intact."""
from pathlib import Path
from datetime import datetime
import bpy, bmesh, json, hashlib, math, runpy
from mathutils import Vector

HERE=Path(__file__).parent
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2')
PREFIX='GMA4B2_'
TARGET=OUT/'GroundMech_A4B2_Editable_B_v2.blend'

def main(original_backup=None):
    assert Path(bpy.data.filepath).resolve()==TARGET.resolve()
    assert bpy.context.mode=='OBJECT'
    helpers=runpy.run_path(str(HERE/'repair_b2_heel_connection.py'),run_name='repair_helpers')
    digest=helpers['mesh_digest'];evaluated_mesh=helpers['evaluated_mesh']
    ns=runpy.run_path(str(HERE/'build_a4_b2.py'),run_name='back_repair_definitions')
    scope=ns['mesh'].__globals__
    old_names=['Back_UpperSpineCrest','Back_UpperPanel','Back_MiddlePanel','Back_LowerPanel']
    old_names += [part+'_'+s for s in ('R','L') for part in ('Back_DeepMechanismShell','Back_BlueShoulderLobe')]
    new_names=[part+'_'+s for s in ('R','L') for part in ('Back_PodSupport','TopPod_RearConcentricRims','TopPod_RearAmberLens')]
    ignored={PREFIX+n+t for n in old_names+new_names for t in ('','_RED','_OUTLINE','_OUTLINE_RED')}
    untouched={o.name:digest(o) for o in bpy.data.objects if o.type=='MESH' and o.name not in ignored}
    rigs={s:bpy.data.objects[PREFIX+'Rig_'+s] for s in ('BLUE','RED')}
    bones={s:[(b.name,b.parent.name if b.parent else None,[list(r) for r in b.matrix_local]) for b in rig.data.bones] for s,rig in rigs.items()}
    backup=OUT/'Backups'/('Before_BackAssembly_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.blend')
    backup.parent.mkdir(exist_ok=True)
    was_dirty=bpy.data.is_dirty
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(backup),copy=True)
    win=bpy.context.window or bpy.context.window_manager.windows[0]
    win.scene=bpy.data.scenes['GroundMech_A4B2_Construction']
    roles=('TeamPrimary','TeamSecondary','Mechanism','Ink','Amber','Outline')
    mats={role:bpy.data.materials[PREFIX+('Blue_'+role if role.startswith('Team') else role)] for role in roles}
    scope.update(RIG=rigs['BLUE'],GROUPS={'Torso':bpy.data.collections[PREFIX+'Torso']},MATS=mats,PARTS=[])
    built=ns['build_back_armor_revision']()
    built.update(ns['build_top_pod_rear_faces']())
    changed=[];added=[];body=[]
    for suffix,made in built.items():
        name=PREFIX+suffix
        existing=bpy.data.objects.get(name)
        if existing and existing!=made:
            obj=existing
            old=obj.data
            assert old!=made.data
            linked=[o for o in bpy.data.objects if o.type=='MESH' and o.data==old]
            assert set(o.name for o in linked).issubset({name,name+'_RED'})
            for o in linked:
                o.data=made.data;o['assembly_revision']='back_assembly_20261023'
                for source_mod in made.modifiers:
                    if source_mod.type=='BEVEL':
                        target_mod=next(m for m in o.modifiers if m.type=='BEVEL')
                        target_mod.width=source_mod.width;target_mod.segments=source_mod.segments
            bpy.data.objects.remove(made,do_unlink=True)
        else:
            obj=made;assert obj.name==name
        (changed if suffix in old_names else added).append(name)
        body.append(obj)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in body:obj.select_set(True)
    bpy.context.view_layer.objects.active=body[0]
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.03)
    bpy.ops.object.mode_set(mode='OBJECT');bpy.context.view_layer.update()
    deps=bpy.context.evaluated_depsgraph_get()
    shells=[];geometry=[]
    for obj in body:
        me=evaluated_mesh(obj,deps);me.calc_loop_triangles()
        bm=bmesh.new();bm.from_mesh(me)
        nonmanifold=sum(not e.is_manifold for e in bm.edges);bm.free()
        degenerate=sum(t.area<1e-10 for t in me.loop_triangles)
        rigid=all(len(v.groups)==1 and abs(v.groups[0].weight-1)<1e-6 for v in obj.data.vertices)
        assert not nonmanifold and not degenerate and rigid,obj.name
        geometry.append({'name':obj.name,'bone':'cockpit','role':obj.get('color_role'),'triangles':len(me.loop_triangles),
                         'uv_layers':len(me.uv_layers),'nonmanifold_edges':nonmanifold,'degenerate_triangles':degenerate,'rigid_weight_1':rigid})
        if obj.get('color_role')=='Amber':
            bpy.data.meshes.remove(me)
            continue
        for v,n in zip(me.vertices,[v.normal.copy() for v in me.vertices]):v.co+=n*.009
        bm=bmesh.new();bm.from_mesh(me)
        deform=bm.verts.layers.deform.active
        if deform:
            for v in bm.verts:v[deform].clear()
        bmesh.ops.reverse_faces(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free()
        me.materials.clear();me.materials.append(mats['Outline'])
        for p in me.polygons:p.material_index=0
        shell=bpy.data.objects.get(obj.name+'_OUTLINE')
        if shell:shell.data=me
        else:
            shell=bpy.data.objects.new(obj.name+'_OUTLINE',me)
            bpy.data.collections[PREFIX+'Silhouette_Only'].objects.link(shell)
            shell.parent=rigs['BLUE'];shell['author']=scope['OWNER'];shell['source_part']=obj.name
            shell['rigid_bone']='cockpit';shell['outline_only']=True
            modifier=shell.modifiers.new('Rigid shell skin','ARMATURE');modifier.object=rigs['BLUE']
        shell.vertex_groups.clear();group=shell.vertex_groups.new(name='cockpit');group.add(list(range(len(me.vertices))),1,'REPLACE')
        shell.visible_shadow=False;shell.visible_diffuse=False;shell.visible_glossy=False
        shell.visible_transmission=False;shell.visible_volume_scatter=False
        shells.append(shell)
    red=bpy.data.collections[PREFIX+'TEAM_RED']
    for obj in body+shells:
        copy=bpy.data.objects.get(obj.name+'_RED')
        if copy:copy.data=obj.data
        else:copy=obj.copy();copy.name=obj.name+'_RED';red.objects.link(copy)
        copy.parent=rigs['RED']
        if obj in shells:
            copy.vertex_groups.clear();group=copy.vertex_groups.new(name='cockpit');group.add(list(range(len(obj.data.vertices))),1,'REPLACE')
        for mod in copy.modifiers:
            if mod.type=='ARMATURE':mod.object=rigs['RED']
        for slot in copy.material_slots:
            role=slot.material.get('color_role') if slot.material else None
            if role in ('TeamPrimary','TeamSecondary'):
                slot.link='OBJECT';slot.material=bpy.data.materials[PREFIX+'Red_'+role]
    all_touched={o.name for o in body+shells}|{o.name+'_RED' for o in body+shells}
    remaining={o.name:digest(o) for o in bpy.data.objects if o.type=='MESH' and o.name not in all_touched}
    assert untouched==remaining,'Unrelated geometry changed'
    assert bones=={s:[(b.name,b.parent.name if b.parent else None,[list(r) for r in b.matrix_local]) for b in rig.data.bones] for s,rig in rigs.items()}
    fin_extent=max(abs(v.co.x) for i in range(4) for v in bpy.data.objects[PREFIX+'Cooling_Blade_%02d'%i].data.vertices)
    support_inner=min(abs(v.co.x) for s in ('R','L') for v in bpy.data.objects[PREFIX+'Back_PodSupport_'+s].data.vertices)
    wall_inner=min(abs(v.co.x) for s in ('R','L') for v in bpy.data.objects[PREFIX+'Back_DeepMechanismShell_'+s].data.vertices)
    cover_outer=max(abs(v.co.x) for s in ('R','L') for v in bpy.data.objects[PREFIX+'Back_BlueShoulderLobe_'+s].data.vertices)
    assert support_inner>fin_extent and wall_inner>cover_outer
    bpy.ops.object.select_all(action='DESELECT')
    for label in ('R','L'):bpy.data.objects[PREFIX+'Back_BlueShoulderLobe_'+label].select_set(True)
    bpy.context.view_layer.objects.active=bpy.data.objects[PREFIX+'Back_BlueShoulderLobe_R']
    for area in win.screen.areas:
        if area.type=='VIEW_3D':
            v=area.spaces.active;v.shading.type='MATERIAL';v.shading.use_scene_world=True;v.overlay.show_overlays=False
            v.region_3d.view_location=Vector((0,.40,3.45))
            v.region_3d.view_rotation=(Vector((0,.40,3.45))-Vector((0,14,4.8))).to_track_quat('-Z','Y')
            v.region_3d.view_distance=7;v.region_3d.view_perspective='ORTHO'
    bpy.context.view_layer.update()
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(TARGET))
    report={'success':True,'revision':'back_assembly_20261023','blend':str(TARGET),'backup':str(original_backup or backup),'resume_backup':str(backup) if original_backup else None,
            'preserved_unsaved_state':was_dirty,'changed_parts':changed,'added_parts':added,'geometry':geometry,
            'fin_side_clearance_m':support_inner-fin_extent,'wall_outside_blue_cover_m':wall_inner-cover_outer,
            'shared_red_geometry':True,'other_geometry_including_heel_repair_unchanged':True,'bones_unchanged':True,
            'reference':'References/Approved_A4_ThreeViews.png; back view centre x=1217.5, floor=464, scale=4.88/435',
            'visual_review':'No assistant output-image inspection; user B review pending',
            'sha256':hashlib.sha256(TARGET.read_bytes()).hexdigest()}
    (OUT/'back_assembly_revision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return report

if __name__=='__main__':
    result=main()
    print(json.dumps(result,ensure_ascii=False))
