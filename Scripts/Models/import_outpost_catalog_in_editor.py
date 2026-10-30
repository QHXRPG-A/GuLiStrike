"""Import models before resolving the building cache/derived construction shape."""
import json
import sys
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/Buildings/OutpostSymmetry_20261029/UE_Delivery_v1'
sys.path.insert(0,str(ROOT/'Scripts'))
mat=unreal.load_asset('/Game/GuLiStrike/Buildings/OutpostSymmetric/Materials/M_OutpostSymmetric_Toon')
old_normals=[n for n in unreal.ObjectIterator(unreal.MaterialExpressionVertexNormalWS) if n.get_outer()==mat]
if old_normals:
    normal=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionPixelNormalWS,-800,150)
    shader=next(n for n in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if n.get_outer()==mat)
    assert unreal.MaterialEditingLibrary.connect_material_expressions(normal,'',shader,'Normal')
    for n in old_normals:unreal.MaterialEditingLibrary.delete_material_expression(mat,n)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mat,False)
results=[]
for phase,tables in [('models',['DT_GuLiStrikeModels_'+s for s in ['Models','MaterialParameters','ColorRegions']]),
                     ('buildings',['DT_GuLiStrikeBuildings_Buildings'])]:
    report_file=OUT/('datatable-import-'+phase+'.json')
    scope={'__name__':'outpost_scoped_import','GULI_TABLE_FILTER':tables,'GULI_IMPORT_REPORT':str(report_file),
        'GULI_MODEL_VALIDATION_IDS':[2003],
        'GULI_IMPORT_PROGRESS':str(OUT/('datatable-import-'+phase+'.log'))}
    exec(compile((ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf8'),'outpost_datatables','exec'),scope)
    report=json.loads(report_file.read_text(encoding='utf8'))
    if report['errors']: raise RuntimeError(str(report['errors']))
    results.append({'phase':phase,'tables':tables,'errors':report['errors'],
        'construction_shapes':report.get('construction_shapes')})
    for registry in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem): registry.reload_catalog()
catalog=unreal.load_asset('/Game/GuLiStrike/Buildings/DA_GuLiBuildingCatalog')
assert unreal.EditorAssetLibrary.save_loaded_asset(catalog,False)
# The source blueprint Shader-to-RGB is rebuilt as a pixel-normal three-band stage.
mat=unreal.load_asset('/Game/GuLiStrike/Buildings/OutpostSymmetric/Materials/M_OutpostSymmetric_Toon')
expressions=list(unreal.MaterialEditingLibrary.get_material_default_vector_parameter_value(mat,'GuLi_TeamPrimary').to_tuple())
result={'success':True,'phases':results,'default_primary':expressions,
    'native_contract_loaded_after_restart':False}
(OUT/'catalog-import.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
