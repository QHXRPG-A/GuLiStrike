import bpy,json,sys,math
from pathlib import Path
from mathutils import Matrix,Vector,Quaternion
sys.path.insert(0,str(Path(__file__).parent))
import build_palette_only as common
O=common.O
bpy.ops.wm.open_mainfile(filepath=str(O/'Models/ResourceFactory_PaletteOnly_B_v1.blend'),load_ui=False)
report=json.loads((O/'Reports/ResourceFactory_production.json').read_text(encoding='utf8'))
before=set(bpy.data.objects)
bpy.ops.import_scene.fbx(filepath=str(O/'References/ExistingFactoryAccessRampCube_Original_RenderLODs.fbx'))
source=next(o for o in bpy.data.objects if o not in before and o.type=='MESH')
# This is the existing Blueprint component, not new model geometry. Use the
# recorded original transform verbatim (UE centimetres converted to metres).
source.matrix_world=Matrix.Translation(Vector((31.99676863,0,.27006964)))@Quaternion((.999768,0,.021547,0)).to_matrix().to_4x4()@Matrix.Diagonal((16.014871,36,.15,1))@source.matrix_world
source.name='Original_BP_ResourceProcessingFactory_AccessRamp'
fingerprint=common.signature(source)
for team in ('Blue','Red'):
    s=bpy.data.scenes[team+'_ResourceFactory'];bpy.context.window.scene=s
    col=bpy.data.collections['Bv1_'+team+'_ResourceFactory_LOD0']
    o=source.copy();o.data=source.data.copy();o.name=team+'_ResourceFactory_AccessRamp';col.objects.link(o);o.hide_render=False;o.hide_set(False)
    attr,proportion=common.paint(o,['Sand']*len(o.data.polygons),team)
    o.data.materials[0]=common.material(o.data.materials[0],attr,team,'ResourceFactory')
    assert common.signature(o)==fingerprint
    rec=next(r for r in report['teams'] if r['team']==team)
    rec['objects'].append({'name':o.name,'source':'BP_ResourceProcessingFactory.AccessRamp (/Engine/BasicShapes/Cube)','LOD':0,'signature':fingerprint,'color_area_fractions':proportion,'vertices':len(o.data.vertices),'polygons':len(o.data.polygons),'UVs':[u.name for u in o.data.uv_layers]})
    old=s.camera;s.camera=None;bpy.data.objects.remove(old,do_unlink=True)
    rec['view']=common.fit(s,list(col.objects),(1,-1,.85))
    rec['original_Blueprint_AccessRamp_transform_m']=[list(r) for r in o.matrix_world]
bpy.data.objects.remove(source,do_unlink=True)
report['original_Blueprint_component_source']='ArtSource/LocalTeamColorReview_20261008/input-export.json'
bpy.context.window.scene=bpy.data.scenes['Blue_ResourceFactory']
bpy.ops.wm.save_as_mainfile(filepath=str(O/'Models/ResourceFactory_PaletteOnly_B_v1.blend'))
(O/'Reports/ResourceFactory_production.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('EXISTING_FACTORY_ASSEMBLY_PRESERVED',flush=True)
