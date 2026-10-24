"""Repair only the live B2 foot bridges; retain unsaved scene edits in a backup."""
from pathlib import Path
from datetime import datetime
import bpy, bmesh, hashlib, json, math, runpy, shutil
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

HERE=Path(__file__).parent
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2')
PREFIX='GMA4B2_'
TARGET=OUT/'GroundMech_A4B2_Editable_B_v2.blend'

def mesh_digest(o):
    h=hashlib.sha256()
    for v in o.data.vertices:h.update(str(tuple(v.co)).encode())
    for p in o.data.polygons:h.update(str(tuple(p.vertices)).encode())
    return h.hexdigest()

def evaluated_mesh(o,deps):
    ev=o.evaluated_get(deps)
    me=bpy.data.meshes.new_from_object(ev,depsgraph=deps,preserve_all_data_layers=True)
    return me

def contains(me,p):
    tree=BVHTree.FromPolygons([v.co for v in me.vertices],[list(f.vertices) for f in me.polygons],all_triangles=False)
    direction=Vector((.93673,.24391,.25173)).normalized()
    current=Vector(p);count=0
    for _ in range(64):
        location,normal,index,distance=tree.ray_cast(current,direction,100)
        if location is None:break
        count+=1;current=location+direction*1e-5
    return bool(count%2)

def main():
    assert Path(bpy.data.filepath).resolve()==TARGET.resolve(),'Unexpected active file'
    assert bpy.context.mode=='OBJECT','Leave Edit/Pose mode before this repair'
    names={PREFIX+'Foot_BlueBridge_'+s+t for s in ('R','L') for t in ('','_RED','_OUTLINE','_OUTLINE_RED')}
    originals={n:bpy.data.objects[n] for n in names}
    retained={o.name:mesh_digest(o) for o in bpy.data.objects if o.type=='MESH' and o.name not in names}
    rigs={s:bpy.data.objects[PREFIX+'Rig_'+s] for s in ('BLUE','RED')}
    bone_contract={s:[(b.name,b.parent.name if b.parent else None,[list(r) for r in b.matrix_local]) for b in rig.data.bones] for s,rig in rigs.items()}
    action_names=sorted(a.name for a in bpy.data.actions)
    backup=OUT/'Backups'/('Before_HeelConnection_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.blend')
    backup.parent.mkdir(exist_ok=True)
    dirty_before=bpy.data.is_dirty
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(backup),copy=True)

    win=bpy.context.window or bpy.context.window_manager.windows[0]
    win.scene=bpy.data.scenes['GroundMech_A4B2_Construction']
    ns=runpy.run_path(str(HERE/'build_a4_b2.py'),run_name='heel_repair_definitions')
    scope=ns['mesh'].__globals__
    assert ns['build_foot_bridge'].__globals__ is scope
    scope.update(RIG=rigs['BLUE'],GROUPS={'Legs':bpy.data.collections[PREFIX+'Legs']},
                 MATS={'TeamPrimary':bpy.data.materials[PREFIX+'Blue_TeamPrimary']},PARTS=[])
    changed=[]
    for label,sign in [('R',-1),('L',1)]:
        obj=originals[PREFIX+'Foot_BlueBridge_'+label]
        old_mesh=obj.data
        linked=[o for o in bpy.data.objects if o.type=='MESH' and o.data==old_mesh]
        assert sorted(o.name for o in linked)==sorted([obj.name,obj.name+'_RED']),'Unexpected mesh users'
        made=ns['build_foot_bridge'](label,sign)
        new_mesh=made.data
        for o in linked:
            o.data=new_mesh
            o['assembly_revision']='heel_connection_20261023'
            o['heel_overlap_local_y_m']=.14
        bpy.data.objects.remove(made,do_unlink=True)
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True);bpy.context.view_layer.objects.active=obj
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.03)
        bpy.ops.object.mode_set(mode='OBJECT')
        bpy.context.view_layer.update()
        deps=bpy.context.evaluated_depsgraph_get()
        outline_mesh=evaluated_mesh(obj,deps)
        for v,n in zip(outline_mesh.vertices,[v.normal.copy() for v in outline_mesh.vertices]):v.co+=n*.009
        bm=bmesh.new();bm.from_mesh(outline_mesh)
        deform=bm.verts.layers.deform.active
        if deform:
            for v in bm.verts:v[deform].clear()
        bmesh.ops.reverse_faces(bm,faces=list(bm.faces));bm.to_mesh(outline_mesh);bm.free()
        outline_mesh.materials.clear();outline_mesh.materials.append(bpy.data.materials[PREFIX+'Outline'])
        for p in outline_mesh.polygons:p.material_index=0
        for suffix in ('_OUTLINE','_OUTLINE_RED'):
            shell=originals[obj.name+suffix]
            shell.data=outline_mesh
            shell.vertex_groups.clear()
            group=shell.vertex_groups.new(name='Foot_'+label)
            group.add(list(range(len(outline_mesh.vertices))),1.0,'REPLACE')
        changed.append(obj.name)

    bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
    contacts=[];geometry=[]
    for label,sign in [('R',-1),('L',1)]:
        bridge=originals[PREFIX+'Foot_BlueBridge_'+label]
        me=evaluated_mesh(bridge,deps)
        bm=bmesh.new();bm.from_mesh(me)
        bad=sum(not e.is_manifold for e in bm.edges);bm.free();me.calc_loop_triangles()
        degenerate=sum(t.area<1e-10 for t in me.loop_triangles)
        weights=all(len(v.groups)==1 and abs(v.groups[0].weight-1)<1e-6 for v in bridge.data.vertices)
        assert bad==0 and degenerate==0 and weights
        geometry.append({'object':bridge.name,'base_vertices':len(bridge.data.vertices),'triangles':len(me.loop_triangles),
                         'nonmanifold_edges':bad,'degenerate_triangles':degenerate,'single_rigid_weight':weights,
                         'bone':'Foot_'+label,'uv_layers':len(me.uv_layers)})
        for i,x in enumerate([-.16,.16]):
            heel=bpy.data.objects[PREFIX+'Foot_Heel_%s_%s'%(label,i)]
            heel_mesh=evaluated_mesh(heel,deps)
            p=ns['foot_transform']((x,.245,.20),sign)
            both=contains(me,p) and contains(heel_mesh,p)
            assert both,'No solid overlap between bridge and heel '+heel.name
            contacts.append({'bridge':bridge.name,'heel':heel.name,'point_m':list(p),'inside_both_evaluated_solids':both})
            bpy.data.meshes.remove(heel_mesh)
        bpy.data.meshes.remove(me)
        assert originals[bridge.name+'_RED'].data==bridge.data
    assert retained=={o.name:mesh_digest(o) for o in bpy.data.objects if o.type=='MESH' and o.name not in names},'Unrelated geometry changed'
    assert bone_contract=={s:[(b.name,b.parent.name if b.parent else None,[list(r) for r in b.matrix_local]) for b in rig.data.bones] for s,rig in rigs.items()}
    assert action_names==sorted(a.name for a in bpy.data.actions)

    bpy.ops.object.select_all(action='DESELECT')
    for name in changed:originals[name].select_set(True)
    bpy.context.view_layer.objects.active=originals[changed[0]]
    for area in win.screen.areas:
        if area.type=='VIEW_3D':
            view=area.spaces.active
            view.shading.type='MATERIAL';view.shading.use_scene_world=True;view.overlay.show_overlays=False
            view.region_3d.view_location=Vector((.60,.02,.63))
            view.region_3d.view_rotation=(Vector((.60,.02,.63))-Vector((8,12,6))).to_track_quat('-Z','Y')
            view.region_3d.view_distance=5.4;view.region_3d.view_perspective='ORTHO'
    assert 'FINISHED' in bpy.ops.wm.save_as_mainfile(filepath=str(TARGET))
    feedback=Path('C:/Users/a/AppData/Local/Temp/codex-clipboard-b0b5a81a-d9f4-442d-be68-1c26f7b27c4e.png')
    if feedback.is_file():shutil.copy2(feedback,OUT/'References/User_B2_HeelGap.png')
    report={'success':True,'revision':'heel_connection_20261023','blend':str(TARGET),'backup':str(backup),
            'preserved_unsaved_state':dirty_before,'changed_parts':changed,'shared_red_geometry':True,
            'old_local_y_gap_m':.28,'new_local_y_overlap_m':.14,'geometry':geometry,'contacts':contacts,
            'unrelated_geometry_unchanged':True,'bones_unchanged':True,'actions_unchanged':True,
            'visual_review':'User review pending; no output-image inspection performed',
            'sha256':hashlib.sha256(TARGET.read_bytes()).hexdigest()}
    (OUT/'heel_connection_revision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return report

if __name__=='__main__':
    result=main()
    print(json.dumps(result,ensure_ascii=False))
