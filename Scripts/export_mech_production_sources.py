"""Isolated UE source extraction for Blender production. Never saves UE assets."""
import unreal, json, hashlib, traceback
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Source/Production_v1'
OUT.mkdir(parents=True,exist_ok=True)
report={'success':False,'meshes':{},'materials':{},'textures':{},'assemblies':{},'errors':[]}
def dump(): (OUT/'source_snapshot.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
def record(p):return {'path':str(p.relative_to(ROOT)),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
def objpath(x):return x.get_path_name() if x else None
def vec(x):return [x.x,x.y,x.z]
def tx(t):return {'location':vec(t.translation),'rotation':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],'scale':vec(t.scale3d)}
def value(x):
    if isinstance(x,unreal.Object):return objpath(x)
    if isinstance(x,unreal.LinearColor):return [x.r,x.g,x.b,x.a]
    if isinstance(x,(str,float,int,bool)):return x
    return str(x)
def expr(m,e,seen=None):
    if not e:return None
    if seen is None:seen=set()
    if e.get_name() in seen:return {'ref':e.get_name()}
    seen.add(e.get_name())
    d={'name':e.get_name(),'type':e.get_class().get_name()}
    for k in ['constant','r','g','b','a','default_value','texture','parameter_name','const_a','const_b','const_alpha','const_coordinate','coordinate_index']:
        try:d[k]=value(e.get_editor_property(k))
        except Exception:pass
    try:d['inputs']=[expr(m,i,seen) for i in unreal.MaterialEditingLibrary.get_inputs_for_material_expression(m,e)]
    except Exception:pass
    return d
def export_asset(o,p,exporter,options=None):
    if not p.exists():
        t=unreal.AssetExportTask();t.object=o;t.filename=str(p);t.automated=True;t.prompt=False;t.replace_identical=False;t.exporter=exporter
        if options:t.options=options
        if not unreal.Exporter.run_asset_export_task(t):raise RuntimeError('Export failed '+objpath(o))
    if not p.is_file():raise RuntimeError('Missing '+str(p))
    return record(p)
def mat_export(m):
    key=objpath(m)
    if key in report['materials']:return
    d={'name':m.get_name(),'class':m.get_class().get_name(),'inputs':{},'textures':[]}
    report['materials'][key]=d
    base=m
    while isinstance(base,unreal.MaterialInstanceConstant):base=base.parent
    d['base']=objpath(base);d['shading_model']=str(base.get_editor_property('shading_model'))
    for prop in ['BASE_COLOR','METALLIC','ROUGHNESS','SPECULAR','EMISSIVE_COLOR','NORMAL','AMBIENT_OCCLUSION']:
        p=getattr(unreal.MaterialProperty,'MP_'+prop)
        d['inputs'][prop]=expr(base,unreal.MaterialEditingLibrary.get_material_property_input_node(base,p))
    for t in unreal.MaterialEditingLibrary.get_used_textures(base):
        k=objpath(t);d['textures'].append(k)
        if k in report['textures']:continue
        p=OUT/(t.get_name()+'.tga')
        report['textures'][k]={'name':t.get_name(),'srgb':bool(t.get_editor_property('srgb')),'export':export_asset(t,p,unreal.TextureExporterTGA())}

try:
    if '-MechProductionWorker' not in unreal.SystemLibrary.get_command_line():raise RuntimeError('Isolated worker required')
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    inv=json.loads((ROOT/'Source/source_inventory.json').read_text(encoding='utf-8'))
    names={'SpiderMech':'SpiderMech','Mecha_01':'Mecha_01','mecha_02':'Mecha_02','FireWeapon_01':'FireWeapon_01','MissileWeapon_01':'MissileWeapon_01','Weapons_Machinegun_lvl1':'Machinegun_lvl1','Mech_Legs_Lt':'Mech_Legs_Lt','Cockpit_Jet':'Cockpit_Jet','HalfShoulder_Box':'HalfShoulder_Box','lowPoly_missile_01':'Missile_01'}
    options=unreal.FbxExportOption();options.ascii=False;options.collision=False;options.level_of_detail=False
    options.set_editor_property('bake_material_inputs',unreal.FbxMaterialBakeMode.DISABLED)
    for row in inv['meshes']:
        name=row['path'].split('.')[-1]
        if name not in names:continue
        key=names[name];o=unreal.load_asset(row['path']);sk=isinstance(o,unreal.SkeletalMesh)
        d=dict(row);report['meshes'][key]=d
        d['fbx']=export_asset(o,OUT/(key+'.fbx'),unreal.SkeletalMeshExporterFBX() if sk else unreal.StaticMeshExporterFBX(),options)
        for slot in row['materials']:
            if slot['path']:mat_export(unreal.load_asset(slot['path']))
        if sk and hasattr(unreal,'SkeletonService'):
            try:d['bones']=[{'name':str(b.bone_name),'parent':str(b.parent_bone_name),'local':tx(b.local_transform),'global':tx(b.global_transform)} for b in unreal.SkeletonService.list_bones(row['path'])]
            except Exception as ex:d['bone_note']=str(ex)
        dump()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cls=unreal.load_class(None,'/Game/Assets/MechaController/Mech_Constructor_Lt_Med/Blueprints/Mech_Lightest_Blueprint.Mech_Lightest_Blueprint_C')
    a=actors.spawn_actor_from_class(cls,unreal.Vector(),unreal.Rotator(),transient=True)
    comps=[]
    for c in a.get_components_by_class(unreal.MeshComponent):
        mesh=c.get_editor_property('skeletal_mesh_asset') if isinstance(c,unreal.SkeletalMeshComponent) else c.get_editor_property('static_mesh') if isinstance(c,unreal.StaticMeshComponent) else None
        comps.append({'name':c.get_name(),'mesh':objpath(mesh),'world_transform':tx(c.get_world_transform()),'relative_transform':tx(c.get_relative_transform()),'parent':objpath(c.get_attach_parent()),'socket':str(c.get_attach_socket_name())})
    report['assemblies']['Mech_Lightest']=comps
    actors.destroy_actor(a)
    report['success']=True
except Exception:report['errors'].append(traceback.format_exc())
finally:
    dump()
    unreal.SystemLibrary.quit_editor()
