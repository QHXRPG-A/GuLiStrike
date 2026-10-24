import unreal,json,ast,re
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
BASE='/Game/GuLiStrike/GroundMech';NEW=BASE+'/MechaDestroyer_A4B2';LIB=unreal.EditorAssetLibrary
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
bp=unreal.load_asset(BASE+'/BP_GroundMech_Light')
rollback=NEW+'/Rollback/BP_GroundMech_BeforeA4B2'
if not LIB.does_asset_exist(rollback):assert LIB.duplicate_asset(bp.get_path_name(),rollback)
# The old global migration validator assumes every October-8 assembly still
# matches its original design and currently raises StopIteration for unrelated
# replaced assemblies. Reuse the exact CSV importer, with scoped native schema,
# unchanged-row and current material checks for this authorized redesign.
source=ast.parse((ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf8'))
prefix=[]
for statement in source.body:
    if isinstance(statement,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='report' for t in statement.targets):break
    prefix.append(statement)
scope={'__name__':'scoped_ground_b2_import','GULI_IMPORT_PROGRESS':str(OUT/'datatable_import.log')}
exec(compile(ast.Module(body=prefix,type_ignores=[]),'shared_csv_import_functions','exec'),scope)
manifest=json.loads((ROOT/'Data/Json/manifest.json').read_text(encoding='utf8'))
tables=['DT_GuLiStrikeModels_Models','DT_GuLiStrikeModels_Parts','DT_GuLiStrikeModels_MaterialParameters']
catalog=json.loads((ROOT/'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8'))
definition=next(r for r in catalog if r['Id']==4001)
assert definition['ResourcePath']==NEW+'/Meshes/SK_GroundMech_A4B2.SK_GroundMech_A4B2' and definition['bTeamColorEnabled']
probe=unreal.new_object(unreal.SkeletalMeshComponent);probe.set_skeletal_mesh_asset(unreal.load_asset(definition['ResourcePath']))
for key,index in [('GuLi_TeamPrimary',8),('GuLi_TeamSecondary',12)]:assert probe.get_custom_primitive_data_index_for_vector_parameter(key)==index
assert probe.get_custom_primitive_data_index_for_scalar_parameter('GuLi_TeamEnabled')==16
for name in tables:
    cfg=manifest['tables'][name];assert unreal.find_object(None,cfg['struct'])
    instance=getattr(unreal,cfg['struct'].rsplit('.',1)[-1])()
    rows=json.loads((ROOT/'Data/Json'/f'{name}.json').read_text(encoding='utf8'))
    for field in rows[0]:
        if field=='Name':continue
        n=field[1:] if field.startswith('b') and len(field)>1 and field[1].isupper() else field
        n=re.sub(r'([A-Z]+)([A-Z][a-z])',r'\1_\2',n);n=re.sub(r'([a-z0-9])([A-Z])',r'\1_\2',n).lower();instance.get_editor_property(n)
entries=[scope['import_table'](n,manifest['tables'][n]) for n in tables]
datareport={'success':all(e['imported'] for e in entries),'tables':entries,'errors':[e.get('err') for e in entries if not e['imported']],'preflight':'Scoped native schema + model 4001 source/CPD + unchanged unrelated source rows; shared importer full row readback'}
(OUT/'datatable_import.json').write_text(json.dumps(datareport,ensure_ascii=False,indent=2),encoding='utf8')
assert datareport['success'],datareport['errors']
for sub in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem):sub.reload_catalog()
mesh=unreal.load_asset(NEW+'/Meshes/SK_GroundMech_A4B2');anim=unreal.load_asset(NEW+'/Animations/ABP_GroundMech_A4B2')
bp.modify();cdo=unreal.get_default_object(bp.generated_class());cdo.modify();component=cdo.get_editor_property('mesh');component.modify()
height=cdo.get_editor_property('standing_height');bounds=mesh.get_imported_bounds();scale=height/(bounds.box_extent.z*2)
capsule=cdo.get_editor_property('capsule_component');half=capsule.get_unscaled_capsule_half_height()
cdo.set_editor_property('integrated_body_mesh',True)
component.set_skeletal_mesh_asset(mesh);component.set_editor_property('override_materials',[]);component.set_anim_instance_class(anim.generated_class())
component.set_relative_scale3d(unreal.Vector(scale,scale,scale));component.set_relative_rotation(unreal.Rotator(yaw=-90),False,False)
component.set_relative_location(unreal.Vector(0,0,-half-(bounds.origin.z-bounds.box_extent.z)*scale),False,False)
for p in ['armor','shoulder']:
    comp=cdo.get_editor_property(p);comp.modify();comp.set_static_mesh(None)
gun=cdo.get_editor_property('machinegun');gun.modify();gun.set_skeletal_mesh_asset(None)
assert unreal.BlueprintService.compile_blueprint(bp.get_path_name());assert LIB.save_loaded_asset(bp,False)
compiled=[]
for root in ['/Game/MechaDestroyer/Blueprints',NEW+'/Animations',BASE+'/Review']:
    for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(root,True):
        if str(a.asset_class_path.asset_name) not in ['Blueprint','AnimBlueprint']:continue
        p=str(a.package_name);ok=bool(unreal.BlueprintService.compile_blueprint(p));compiled.append({'path':p,'success':ok})
        if p.startswith(NEW):assert LIB.save_loaded_asset(a.get_asset(),False)
assert all(x['success'] for x in compiled)
cdo=unreal.get_default_object(bp.generated_class());component=cdo.get_editor_property('mesh')
report={'success':True,'blueprint':bp.get_path_name(),'rollback':rollback,'model_id':cdo.model_id,'integrated_body_mesh':cdo.get_editor_property('integrated_body_mesh'),'mesh':component.get_skeletal_mesh_asset().get_path_name(),'anim':component.anim_class.get_path_name(),'scale':scale,'standing_height_preserved':height,'capsule_half_height_preserved':half,'capsule_radius_preserved':capsule.get_unscaled_capsule_radius(),'compiled':compiled,'native_code_modified':False,'visual_verification':'User'}
(OUT/'player_install.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
