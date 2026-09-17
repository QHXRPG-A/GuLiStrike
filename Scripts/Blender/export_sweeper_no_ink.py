"""Remove Sweeper's drawn ink and outline shell, keeping the approved body."""
import bpy,sys,json,traceback
from pathlib import Path

ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/StyleAdjust_20260917/Models'
OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'Scripts/Blender'))
import build_tactical_from_drawings as author

def run():
    source=ROOT/'ArtSource/StylePass_20260917/Models/Sweeper_Cel.blend'
    bpy.ops.wm.open_mainfile(filepath=str(source))
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    shells=[o for o in meshes if any(m and 'Contour' in m.name for m in o.data.materials)]
    bodies=[o for o in meshes if o not in shells]
    assert len(bodies)==1 and len(shells)==1
    body=bodies[0];arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
    before=[tuple(v.co) for v in body.data.vertices]
    old_triangles=sum(len(p.vertices)-2 for o in meshes for p in o.data.polygons)
    for shell in shells:bpy.data.objects.remove(shell,do_unlink=True)
    mat=body.data.materials[0].copy();mat.name='M_Sweeper_Cel_NoInk';body.data.materials[0]=mat
    nodes,links=mat.node_tree.nodes,mat.node_tree.links
    multiply=next(n for n in nodes if n.type=='MIX_RGB' and n.blend_type=='MULTIPLY')
    emission=next(n for n in nodes if n.type=='EMISSION')
    links.new(multiply.outputs[0],emission.inputs['Color'])
    for n in list(nodes):
        if n.type=='TEX_IMAGE' and n.image and 'LineMask' in n.image.name:nodes.remove(n)
        elif n.type=='MIX_RGB' and n.blend_type!='MULTIPLY':nodes.remove(n)
    assert before==[tuple(v.co) for v in body.data.vertices]
    author.OUT=OUT;author.PARTS=[body]
    for view in ('three_quarter','front','side'):author.render('Sweeper_NoInk',view)
    body.hide_set(False);author.active(body)
    fbx=dict(use_selection=True,global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',add_leaf_bones=False,bake_anim=False,path_mode='COPY',use_custom_props=True)
    bpy.ops.export_scene.fbx(filepath=str(OUT/'SM_Sweeper_NoInk.fbx'),object_types={'MESH'},**fbx)
    arm.select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_Sweeper_NoInk.fbx'),object_types={'MESH','ARMATURE'},use_armature_deform_only=True,**fbx)
    bpy.context.scene.name='Sweeper_NoInk_Review'
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Sweeper_NoInk.blend'))
    lo,hi,triangles,sym=author.evaluated_stats([body])
    r={'success':True,'source':str(source),'body_geometry_unchanged':True,'triangles_before':old_triangles,'body_triangles':triangles,'contour_triangles':0,'materials':[m.name for m in body.data.materials],'uv_layers':[u.name for u in body.data.uv_layers],'bounds_m':[list(lo),list(hi)],'symmetry_error_m':sym,'bones':[b.name for b in arm.data.bones],'war_machine_unchanged':True}
    (OUT/'sweeper_no_ink.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
    print('SWEEPER_NO_INK_READY',json.dumps(r),flush=True)

try:run()
except Exception:
    (OUT/'export_error.txt').write_text(traceback.format_exc(),encoding='utf8');raise
