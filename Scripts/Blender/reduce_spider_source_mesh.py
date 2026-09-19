import bpy,bmesh,sys,time,json,traceback
from pathlib import Path
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
t0=time.time();report={'success':False,'steps':[]}
def checkpoint(label,**kw):
    report['steps'].append({'step':label,'seconds':time.time()-t0,**kw});(m.OUT/'spider_reduce_status.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(label,kw,flush=True)
try:
    bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1'];m.visible(['SpiderMech'])
    o=m.meshes('SpiderMech')[0];m.active(o)
    bm=bmesh.new();bm.from_mesh(o.data);layer=bm.verts.layers.deform.active
    candidates=bmesh.ops.find_doubles(bm,verts=list(bm.verts),dist=.0001)['targetmap']
    def same_weights(a,b):
        aa=a[layer];bb=b[layer]
        return set(aa.keys())==set(bb.keys()) and all(abs(aa[i]-bb[i])<.0001 for i in aa.keys())
    targetmap={a:b for a,b in candidates.items() if same_weights(a,b)}
    checkpoint('coincident_vertex_pairs',candidates=len(candidates),compatible=len(targetmap))
    bmesh.ops.weld_verts(bm,targetmap=targetmap)
    bm.to_mesh(o.data);bm.free();o.data.update();checkpoint('welded',vertices=len(o.data.vertices))
    mod=o.modifiers.new('OriginalMesh_To_22K','DECIMATE');mod.decimate_type='COLLAPSE';mod.ratio=22000/839778;mod.use_collapse_triangulate=True
    # Source geometry is symmetric. Avoid the costly global symmetry matcher;
    # preserve the source geometry and validate actual silhouettes afterward.
    mod.use_symmetry=False;o.modifiers.move(len(o.modifiers)-1,0)
    bpy.context.view_layer.update();ev=o.evaluated_get(bpy.context.evaluated_depsgraph_get());ev.data.calc_loop_triangles()
    checkpoint('decimated',triangles=len(ev.data.loop_triangles))
    construction=m.OUT/'Construction';construction.mkdir(exist_ok=True)
    bpy.data.libraries.write(str(construction/'SpiderMech_ReductionEditable.blend'),set(m.objects('SpiderMech')),path_remap='RELATIVE',fake_user=True,compress=True)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    o.data.calc_loop_triangles();o['source_triangles']=839778;o['reduced_triangles']=len(o.data.loop_triangles);o['reduction_method']='source mesh compatible-weight weld and geometric collapse; original UVs and material slots retained'
    checkpoint('applied',triangles=len(o.data.loop_triangles),vertices=len(o.data.vertices))
    bpy.data.libraries.write(str(m.OUT/'SpiderMech_Reduced_Source_v1.blend'),set(m.objects('SpiderMech')),path_remap='RELATIVE',fake_user=True,compress=True)
    report['success']=True;checkpoint('complete')
except Exception:report['error']=traceback.format_exc();checkpoint('failed')
