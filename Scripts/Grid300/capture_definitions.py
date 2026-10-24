"""Capture identity fields and effective collision inputs omitted by Python dir()."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'ArtSource/Navigation/Grid300/Baseline/definitions.json'
if OUT.exists():
    result={'success':True,'already_captured':True,'path':str(OUT)}
else:
    resource=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    clusters=[{'cluster_id':c.get_editor_property('cluster_id'),'first_node_index':c.get_editor_property('first_node_index')}
              for c in resource.get_editor_property('clusters')]
    nodes=[{'node_id':n.get_editor_property('node_id'),'cluster_id':n.get_editor_property('cluster_id')}
           for n in resource.get_editor_property('nodes')]
    tables={}
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    for item in registry.get_assets_by_path('/Game/GuLiStrike',True):
        name=str(item.asset_name)
        if name not in ('DT_GuLiStrikeCommander_Soldiers','DT_GuLiStrikeBuildings_Buildings'):continue
        table=item.get_asset()
        tables[name]={'path':table.get_path_name(),'rows':json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))}
    assert len(tables)==2
    capsules={}
    for row in tables['DT_GuLiStrikeCommander_Soldiers']['rows']:
        path=row.get('ActorClass','')
        if not path or path=='None':continue
        cls=unreal.load_class(None,path)
        assert cls,path
        cdo=unreal.get_default_object(cls)
        capsule=cdo.get_component_by_class(unreal.CapsuleComponent)
        assert capsule,path
        capsules[str(row['Id'])]={'class':path,'radius_cm':capsule.get_scaled_capsule_radius(),
                                 'half_height_cm':capsule.get_scaled_capsule_half_height()}
    result={'success':True,'schema':'guli-grid300/definitions-v1','clusters':clusters,'nodes':nodes,
            'tables':tables,'capsules':capsules}
    with OUT.open('x',encoding='utf-8') as stream:json.dump(result,stream,ensure_ascii=False,indent=2)
    result={'success':True,'path':str(OUT),'clusters':len(clusters),'nodes':len(nodes),'capsules':capsules}
unreal.MCPythonHelper.submit_result(json.dumps(result,ensure_ascii=False))
