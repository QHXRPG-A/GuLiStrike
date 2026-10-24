"""Apply the user's toe-shape feedback without changing the rest of B2."""
from pathlib import Path
from datetime import datetime
import bpy,bmesh,runpy,json,math,hashlib,shutil
from mathutils import Vector
HERE=Path(__file__).parent
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2')
PREFIX='GMA4B2_';TARGET=OUT/'GroundMech_A4B2_Editable_B_v2.blend'

def main():
    assert Path(bpy.data.filepath).resolve()==TARGET.resolve() and bpy.context.mode=='OBJECT'
    h=runpy.run_path(str(HERE/'repair_b2_heel_connection.py'),run_name='toe_helpers')
    ns=runpy.run_path(str(HERE/'build_a4_b2.py'),run_name='toe_definitions');scope=ns['mesh'].__globals__
    names=[PREFIX+'Foot_%s_%s_%s'%(part,s,i) for part in ('SeparateToe','ToeCream') for s in ('R','L') for i in range(2)]
    all_names={n+t for n in names for t in ('','_RED','_OUTLINE','_OUTLINE_RED')}
    untouched={o.name:h['mesh_digest'](o) for o in bpy.data.objects if o.type=='MESH' and o.name not in all_names}
    backup=OUT/'Backups'/('Before_TaperedToes_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.blend')
    backup.parent.mkdir(exist_ok=True)
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(backup),copy=True)
    win=bpy.context.window or bpy.context.window_manager.windows[0];win.scene=bpy.data.scenes['GroundMech_A4B2_Construction']
    rig=bpy.data.objects[PREFIX+'Rig_BLUE']
    bones=[(b.name,b.parent.name if b.parent else None,[list(r) for r in b.matrix_local]) for b in rig.data.bones]
    mats={r:bpy.data.materials[PREFIX+r] for r in ('Mechanism','Sand','Outline')}
    scope.update(RIG=rig,GROUPS={'Legs':bpy.data.collections[PREFIX+'Legs']},MATS=mats,PARTS=[])
    built={}
    for s,sign in [('R',-1),('L',1)]:built.update(ns['build_toe_pair'](s,sign))
    body=[]
    for suffix,made in built.items():
        obj=bpy.data.objects[PREFIX+suffix];old=obj.data
        linked=[o for o in bpy.data.objects if o.type=='MESH' and o.data==old]
        assert set(o.name for o in linked)=={obj.name,obj.name+'_RED'}
        for linked_obj in linked:
            linked_obj.data=made.data;linked_obj['assembly_revision']='tapered_toes_20261023'
            new_bevel=next(m for m in made.modifiers if m.type=='BEVEL')
            next(m for m in linked_obj.modifiers if m.type=='BEVEL').width=new_bevel.width
        bpy.data.objects.remove(made,do_unlink=True);body.append(obj)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in body:obj.select_set(True)
    bpy.context.view_layer.objects.active=body[0]
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.03)
    bpy.ops.object.mode_set(mode='OBJECT');bpy.context.view_layer.update()
    deps=bpy.context.evaluated_depsgraph_get();geometry=[];meshes={}
    for obj in body:
        me=h['evaluated_mesh'](obj,deps);me.calc_loop_triangles();meshes[obj.name]=me
        bm=bmesh.new();bm.from_mesh(me);nonmanifold=sum(not e.is_manifold for e in bm.edges);bm.free()
        badarea=sum(t.area<1e-10 for t in me.loop_triangles)
        rigid=all(len(v.groups)==1 and abs(v.groups[0].weight-1)<1e-6 for v in obj.data.vertices)
        assert nonmanifold==0 and badarea==0 and rigid,obj.name
        geometry.append({'name':obj.name,'bone':obj['rigid_bone'],'nonmanifold_edges':nonmanifold,'degenerate_triangles':badarea,'rigid_weight_1':rigid,'triangles':len(me.loop_triangles)})
        outline=me.copy()
        for v,n in zip(outline.vertices,[v.normal.copy() for v in outline.vertices]):v.co+=n*.009
        bm=bmesh.new();bm.from_mesh(outline);deform=bm.verts.layers.deform.active
        if deform:
            for v in bm.verts:v[deform].clear()
        bmesh.ops.reverse_faces(bm,faces=list(bm.faces));bm.to_mesh(outline);bm.free()
        outline.materials.clear();outline.materials.append(mats['Outline'])
        for p in outline.polygons:p.material_index=0
        for tail in ('_OUTLINE','_OUTLINE_RED'):
            shell=bpy.data.objects[obj.name+tail];shell.data=outline;shell.vertex_groups.clear()
            vg=shell.vertex_groups.new(name=obj['rigid_bone']);vg.add(list(range(len(outline.vertices))),1,'REPLACE')
    contacts=[]
    for s,sign in [('R',-1),('L',1)]:
        bridge=h['evaluated_mesh'](bpy.data.objects[PREFIX+'Foot_BlueBridge_'+s],deps)
        for i,x in enumerate([-.30,.30]):
            toe=meshes[PREFIX+'Foot_SeparateToe_%s_%s'%(s,i)]
            panel=meshes[PREFIX+'Foot_ToeCream_%s_%s'%(s,i)]
            joint_point=ns['foot_transform']((x,-.90,.26),sign)
            mount_point=ns['foot_transform']((x,-1.22,.23+.18*(.13/.34)-.003),sign)
            toe_joined=h['contains'](toe,joint_point) and h['contains'](bridge,joint_point)
            panel_seated=h['contains'](toe,mount_point) and h['contains'](panel,mount_point)
            assert toe_joined and panel_seated,(s,i)
            contacts.append({'side':s,'toe':i,'toe_bridge_overlap':toe_joined,'sand_panel_seated':panel_seated})
        bpy.data.meshes.remove(bridge)
    for me in meshes.values():bpy.data.meshes.remove(me)
    assert untouched=={o.name:h['mesh_digest'](o) for o in bpy.data.objects if o.type=='MESH' and o.name not in all_names}
    assert bones==[(b.name,b.parent.name if b.parent else None,[list(r) for r in b.matrix_local]) for b in rig.data.bones]
    bpy.ops.object.select_all(action='DESELECT')
    for obj in body:
        if 'SeparateToe' in obj.name:obj.select_set(True)
    bpy.context.view_layer.objects.active=bpy.data.objects[PREFIX+'Foot_SeparateToe_R_0']
    for area in win.screen.areas:
        if area.type=='VIEW_3D':
            v=area.spaces.active;v.shading.type='MATERIAL';v.shading.use_scene_world=True;v.overlay.show_overlays=False
            v.region_3d.view_location=Vector((0,-.30,.52));v.region_3d.view_rotation=(Vector((0,-.30,.52))-Vector((6,-12,6))).to_track_quat('-Z','Y')
            v.region_3d.view_distance=5.0;v.region_3d.view_perspective='ORTHO'
    bpy.context.view_layer.update()
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(TARGET))
    feedback=Path('C:/Users/a/AppData/Local/Temp/codex-clipboard-1837367f-acf9-4d5e-9ecc-742c09b0c94e.png')
    if feedback.exists():shutil.copy2(feedback,OUT/'References/User_B2_SquareToes.png')
    report={'success':True,'revision':'tapered_toes_20261023','blend':str(TARGET),'backup':str(backup),
            'user_feedback':'这里也是，方方正正的不好看','toe_count_per_foot':2,
            'old_new_leading_width_m':[.55,.32],'old_new_front_reach_m':[1.65,1.55],
            'sloped_front_cap_offset_m':.07,'sand_panel_inset_depth_m':.008,
            'geometry':geometry,'contacts':contacts,'shared_red_geometry':True,'bones_unchanged':True,
            'other_geometry_including_integrated_heel_and_back_unchanged':True,
            'visual_review':'User review pending; no assistant output-image inspection',
            'sha256':hashlib.sha256(TARGET.read_bytes()).hexdigest()}
    (OUT/'tapered_toes_revision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return report

if __name__=='__main__':
    result=main()
    print(json.dumps(result,ensure_ascii=False))
