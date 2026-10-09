"""After native compilation + table import, migrate existing CDO/config identity caches.

No paint/geometry/transform/animation is changed; new B candidates are not selected.
Run in the open editor, never launches a game or native compiler.
"""
import json
import sys
from pathlib import Path
import unreal

ROOT=Path(r'D:/UE5.7/test1')
sys.path.insert(0,str(ROOT/'Scripts'))
from Models import model_catalog
if not hasattr(unreal,'GuLiModelRegistrySubsystem'):
    raise RuntimeError('Compile and reload the authorized native model interface first')
table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeModels_Models')
if not table:raise RuntimeError('Import all model-ID tables before seeding compatibility caches')
expected={r['Id']:r for r in model_catalog.rows('Models')}
actual={r['Id']:r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))}
if set(expected)!=set(actual) or any(expected[i]['ResourcePath']!=actual[i]['ResourcePath'] for i in expected):
    raise RuntimeError('Native model table differs from the generated Excel source; no assets changed')
baseline=json.loads((ROOT/'Data/Models/migration-baseline-20261008.json').read_text(encoding='utf8'))
report={'migrated':[],'errors':[],'new_paint_selected':False,'gameplay':'not_run'}
pending=[]
def plan(asset,cdo,property_name,value,entry):
    if not asset or not cdo:raise RuntimeError('Missing original asset before migration')
    cdo.get_editor_property(property_name) # Verify every native field before the first CDO write.
    pending.append((asset,cdo,property_name,value,entry))
parts=json.loads((ROOT/'Data/Json/DT_GuLiStrikeShip_Parts.json').read_text(encoding='utf8'))
by_part={r['PartId']:r for r in parts}
seen=set()
for ship in baseline['ships']:
    for original in ship['parts']:
        if original['class'] in seen:continue
        seen.add(original['class'])
        cdo=unreal.get_default_object(unreal.load_class(None,original['class']))
        row=by_part[original['part_id']]
        resource=model_catalog.resource(row['ModelId'])
        if resource!=original['mesh']['path']:raise RuntimeError('Pre-migration part mesh differs: '+original['part_id'])
        bp=unreal.load_asset(original['class'].removesuffix('_C').split('.')[0])
        plan(bp,cdo,'model_id',row['ModelId'],dict(asset=bp.get_path_name(),model_id=row['ModelId'],kind='ShipPart',geometry='unchanged'))
    bp=unreal.load_asset(ship['class'].removesuffix('_C').split('.')[0])
    cdo=unreal.get_default_object(bp.generated_class())
    preset=str(cdo.get_editor_property('tuning_preset'))
    tuning=json.loads((ROOT/'Data/Json/DT_GuLiStrikeShip_Tuning.json').read_text(encoding='utf8'))
    row=next(r for r in tuning if r['Name']==preset)
    assert model_catalog.resource(row['HullModelId'])==ship['hull']['mesh']['path']
    plan(bp,cdo,'hull_model_id',row['HullModelId'],dict(asset=bp.get_path_name(),model_id=row['HullModelId'],kind='ShipHull',geometry='unchanged'))
economy=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
if not economy:raise RuntimeError('Existing resource economy config is missing')
visuals=list(economy.get_editor_property('ore_visuals'))
for visual in visuals:
    mesh=visual.get_editor_property('mesh') # Serialized migration reference only, never written back.
    mid=int(visual.get_editor_property('model_id'))
    if mesh:
        mid=next(m['Id'] for m in expected.values() if m['ResourcePath']==mesh.get_path_name())
    if mid<=0 or mid not in expected:raise RuntimeError('Unmapped ore visual; config left unchanged')
    visual.set_editor_property('model_id',mid)
plan(economy,economy,'ore_visuals',visuals,dict(asset=economy.get_path_name(),kind='ResourceConfig',ore_model_ids=[int(v.get_editor_property('model_id')) for v in visuals]))
plan(economy,economy,'factory_model_id',model_catalog.model_id('ResourceFactory'),dict(asset=economy.get_path_name(),model_id=model_catalog.model_id('ResourceFactory'),kind='ResourceFactoryConfig'))
ground=unreal.load_asset('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light')
plan(ground,unreal.get_default_object(ground.generated_class()),'model_id',model_catalog.model_id('GroundMech'),dict(asset=ground.get_path_name(),model_id=model_catalog.model_id('GroundMech'),kind='GroundMech',assembly='unchanged'))
for asset,cdo,property_name,value,entry in pending:
    asset.modify();cdo.modify()
    if property_name in ('hull_model_id','model_id') and isinstance(cdo,(unreal.GuLiStrikeShip,unreal.GuLiGroundMechCharacter)):
        native=json.loads(unreal.GuLiModelAuthoringLibrary.set_compatibility_model_id(cdo,'HullModelId' if property_name=='hull_model_id' else 'ModelId',value))
        if not native['success']:raise RuntimeError(native['error'])
    else:cdo.set_editor_property(property_name,value)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset,False):raise RuntimeError('Could not save model identity: '+asset.get_path_name())
    actual_value=cdo.get_editor_property(property_name)
    if property_name!='ore_visuals' and int(actual_value)!=int(value):raise RuntimeError('Model identity readback differs: '+asset.get_path_name())
    report['migrated'].append(entry)
# Existing visual child actors receive one runtime ModelPresentation component automatically.
report['assembly_registration']='native InstallForPresentationActor resolves existing class -> ModelId -> Parts'
(ROOT/'Data/Models/native-binding-migration.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(dict(migrated=len(report['migrated']),new_paint_selected=False,gameplay='not_run')))
