"""Replace the user's rejected rear heel cuboids with compact continuous wedges."""
from pathlib import Path
from datetime import datetime
import bpy,bmesh,runpy,json,math,hashlib,shutil
from mathutils import Vector

HERE=Path(__file__).parent
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2')
PREFIX='GMA4B2_'
TARGET=OUT/'GroundMech_A4B2_Editable_B_v2.blend'

def main():
    assert Path(bpy.data.filepath).resolve()==TARGET.resolve() and bpy.context.mode=='OBJECT'
    helpers=runpy.run_path(str(HERE/'repair_b2_heel_connection.py'),run_name='heel_helpers')
    ns=runpy.run_path(str(HERE/'build_a4_b2.py'),run_name='heel_definitions');scope=ns['mesh'].__globals__
    old_suffixes=['Foot_Heel_%s_%s'%(s,i) for s in ('R','L') for i in range(2)]
    new_suffixes=[p+s for s in ('R','L') for p in ('Foot_BlueBridge_','Foot_IntegratedHeel_')]
    touched={PREFIX+n+t for n in old_suffixes+new_suffixes for t in ('','_RED','_OUTLINE','_OUTLINE_RED')}
    unchanged={o.name:helpers['mesh_digest'](o) for o in bpy.data.objects if o.type=='MESH' and o.name not in touched}
    rigs={s:bpy.data.objects[PREFIX+'Rig_'+s] for s in ('BLUE','RED')}
    bones={s:[(b.name,b.parent.name if b.parent else None,[list(r) for r in b.matrix_local]) for b in rig.data.bones] for s,rig in rigs.items()}
    backup=OUT/'Backups'/('Before_IntegratedHeels_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.blend')
    backup.parent.mkdir(exist_ok=True)
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(backup),copy=True)
    win=bpy.context.window or bpy.context.window_manager.windows[0];win.scene=bpy.data.scenes['GroundMech_A4B2_Construction']
    mats={r:bpy.data.materials[PREFIX+('Blue_'+r if r.startswith('Team') else r)] for r in ('TeamPrimary','Mechanism','Outline')}
    scope.update(RIG=rigs['BLUE'],GROUPS={'Legs':bpy.data.collections[PREFIX+'Legs']},MATS=mats,PARTS=[])
    body=[]
    for label,sign in [('R',-1),('L',1)]:
        for constructor,stem in [(ns['build_foot_bridge'],'Foot_BlueBridge_'),(ns['build_integrated_heel'],'Foot_IntegratedHeel_')]:
            canonical=PREFIX+stem+label
            made=constructor(label,sign);existing=bpy.data.objects.get(canonical)
            if existing and existing!=made:
                old=existing.data
                for obj in [o for o in bpy.data.objects if o.type=='MESH' and o.data==old]:
                    assert obj.name in (canonical,canonical+'_RED')
                    obj.data=made.data;obj['assembly_revision']='integrated_heel_20261023'
                    obj['heel_overlap_local_y_m']=.26
                bpy.data.objects.remove(made,do_unlink=True);made=existing
            body.append(made)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in body:obj.select_set(True)
    bpy.context.view_layer.objects.active=body[0]
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.03)
    bpy.ops.object.mode_set(mode='OBJECT');bpy.context.view_layer.update()
    deps=bpy.context.evaluated_depsgraph_get();shells=[];checks=[];eval_meshes={}
    for obj in body:
        me=helpers['evaluated_mesh'](obj,deps);me.calc_loop_triangles();eval_meshes[obj.name]=me
        bm=bmesh.new();bm.from_mesh(me)
        nonmanifold=sum(not e.is_manifold for e in bm.edges)
        remaining=set(bm.verts);components=0
        while remaining:
            stack=[remaining.pop()];components+=1
            while stack:
                v=stack.pop()
                for e in v.link_edges:
                    other=e.other_vert(v)
                    if other in remaining:remaining.remove(other);stack.append(other)
        bm.free()
        badarea=sum(t.area<1e-10 for t in me.loop_triangles)
        rigid=all(len(v.groups)==1 and abs(v.groups[0].weight-1)<1e-6 for v in obj.data.vertices)
        assert not nonmanifold and not badarea and components==1 and rigid,obj.name
        checks.append({'name':obj.name,'bone':obj['rigid_bone'],'triangles':len(me.loop_triangles),'connected_components':components,
                       'nonmanifold_edges':nonmanifold,'degenerate_triangles':badarea,'rigid_weight_1':rigid})
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
            shell=bpy.data.objects.new(obj.name+'_OUTLINE',outline);bpy.data.collections[PREFIX+'Silhouette_Only'].objects.link(shell)
            shell.parent=rigs['BLUE'];shell['author']=scope['OWNER'];shell['outline_only']=True;shell['source_part']=obj.name;shell['rigid_bone']=obj['rigid_bone']
            mod=shell.modifiers.new('Rigid shell skin','ARMATURE');mod.object=rigs['BLUE']
        shell.vertex_groups.clear();group=shell.vertex_groups.new(name=obj['rigid_bone']);group.add(list(range(len(outline.vertices))),1,'REPLACE')
        for prop in ('visible_shadow','visible_diffuse','visible_glossy','visible_transmission','visible_volume_scatter'):setattr(shell,prop,False)
        shells.append(shell)
    contacts=[]
    for label,sign in [('R',-1),('L',1)]:
        bridge=eval_meshes[PREFIX+'Foot_BlueBridge_'+label];heel=eval_meshes[PREFIX+'Foot_IntegratedHeel_'+label]
        for x in (-.12,.12):
            point=ns['foot_transform']((x,.17,.18),sign)
            both=helpers['contains'](bridge,point) and helpers['contains'](heel,point)
            assert both,'Heel/bridge gap '+label
            contacts.append({'side':label,'point_m':list(point),'inside_bridge_and_heel':both})
    for me in eval_meshes.values():bpy.data.meshes.remove(me)
    red=bpy.data.collections[PREFIX+'TEAM_RED']
    for obj in body+shells:
        copy=bpy.data.objects.get(obj.name+'_RED')
        if copy:copy.data=obj.data
        else:copy=obj.copy();copy.name=obj.name+'_RED';red.objects.link(copy)
        copy.parent=rigs['RED']
        if obj in shells:
            copy.vertex_groups.clear();group=copy.vertex_groups.new(name=obj['rigid_bone']);group.add(list(range(len(obj.data.vertices))),1,'REPLACE')
        for mod in copy.modifiers:
            if mod.type=='ARMATURE':mod.object=rigs['RED']
        for slot in copy.material_slots:
            if slot.material and slot.material.get('color_role')=='TeamPrimary':slot.link='OBJECT';slot.material=bpy.data.materials[PREFIX+'Red_TeamPrimary']
    removed=[]
    for suffix in old_suffixes:
        for tail in ('','_RED','_OUTLINE','_OUTLINE_RED'):
            obj=bpy.data.objects.get(PREFIX+suffix+tail)
            if obj:
                assert obj.get('author')==scope['OWNER']
                removed.append(obj.name);bpy.data.objects.remove(obj,do_unlink=True)
    assert unchanged=={o.name:helpers['mesh_digest'](o) for o in bpy.data.objects if o.type=='MESH' and o.name not in touched}
    assert bones=={s:[(b.name,b.parent.name if b.parent else None,[list(r) for r in b.matrix_local]) for b in rig.data.bones] for s,rig in rigs.items()}
    bpy.ops.object.select_all(action='DESELECT')
    for label in ('R','L'):bpy.data.objects[PREFIX+'Foot_IntegratedHeel_'+label].select_set(True)
    bpy.context.view_layer.objects.active=bpy.data.objects[PREFIX+'Foot_IntegratedHeel_R']
    for area in win.screen.areas:
        if area.type=='VIEW_3D':
            v=area.spaces.active;v.shading.type='MATERIAL';v.shading.use_scene_world=True;v.overlay.show_overlays=False
            v.region_3d.view_location=Vector((0,.20,.58));v.region_3d.view_rotation=(Vector((0,.20,.58))-Vector((7,12,6))).to_track_quat('-Z','Y')
            v.region_3d.view_distance=5.1;v.region_3d.view_perspective='ORTHO'
    bpy.context.view_layer.update()
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(TARGET))
    feedback=Path('C:/Users/a/AppData/Local/Temp/codex-clipboard-bb3aab6a-80fe-4574-b61c-4c84d5d65c2d.png')
    if feedback.exists():shutil.copy2(feedback,OUT/'References/User_B2_HeelBlocks.png')
    report={'success':True,'revision':'integrated_heel_20261023','blend':str(TARGET),'backup':str(backup),
            'user_feedback':'这两块东西很丑（用户附图标注）','replaced_old_parts':removed,
            'heel_count_per_foot':1,'old_new_local_extents_m':{'max_width':[.94,.74],'rear_reach':[.64,.48],'maximum_height':[.36,.31]},
            'bridge_overlap_local_y_m':.26,'geometry':checks,'solid_contacts':contacts,
            'shared_red_geometry':True,'bones_unchanged':True,'unrelated_geometry_including_back_revision_unchanged':True,
            'visual_review':'User review pending; no assistant output-image inspection',
            'sha256':hashlib.sha256(TARGET.read_bytes()).hexdigest()}
    (OUT/'integrated_heel_revision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return report

if __name__=='__main__':
    result=main()
    print(json.dumps(result,ensure_ascii=False))
