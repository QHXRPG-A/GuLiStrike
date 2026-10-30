"""Migrate exact old mesh references, preserve display bounds, then retire meshes."""
import json
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/Buildings/OutpostSymmetry_20261029/UE_Delivery_v1'
LIB=unreal.EditorAssetLibrary
AR=unreal.AssetRegistryHelpers.get_asset_registry()
OPTIONS=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True)
NEW='/Game/GuLiStrike/Buildings/OutpostSymmetric/Meshes/SM_OutpostSymmetric'
OLD=['/Game/GuLiStrike/Buildings/Meshes/SM_OutpostPlaceholder',
     '/Game/GuLiStrike/Buildings/Legacy/SM_OutpostCube20m_PreMonument',
     '/Game/GuLiStrike/Buildings/Meshes/Source/SM_OutpostMonument_v01']
ALIAS='/Game/GuLiStrike/Review/ModelInterface/ManualOutpost/Meshes/SM_OutpostPlaceholder'

def refs(path):return [str(x) for x in (AR.get_referencers(path,OPTIONS) or [])]
def vec(v):return list(v.to_tuple())
def checkpoint(stage,**kw):
    (OUT/'retirement-progress.json').write_text(json.dumps({'stage':stage,**kw},ensure_ascii=False,indent=2),encoding='utf8')

if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():raise RuntimeError('Retire outside PIE.')
new=unreal.load_asset(NEW)
assert isinstance(new,unreal.StaticMesh)
old={path:unreal.load_asset(path) for path in OLD}
assert all(isinstance(m,unreal.StaticMesh) and m!=new for m in old.values())
report={'success':False,'mesh':new.get_path_name(),'references_before':{p:refs(p) for p in [*OLD,ALIAS]},
    'components':[],'catalogs':[],'tables':[],'removed_editor_displays':[],'deleted':[]}
checkpoint('loading-exact-referencers')
packages=sorted(set(p for paths in report['references_before'].values() for p in paths if p not in OLD and p!=ALIAS))
loaded=[]
for package in packages:
    asset=unreal.load_asset(package)
    if not asset:raise RuntimeError('Cannot load referencer '+package)
    loaded.append(asset)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in actors.get_all_level_actors():
    if actor.get_actor_label()=='ModelRegistry_Mass_ManualOutpost':
        report['removed_editor_displays'].append(actor.get_actor_label())
        assert actors.destroy_actor(actor)

checkpoint('migrating-components')
new_size=new.get_bounds().box_extent*2
for component in unreal.ObjectIterator(unreal.StaticMeshComponent):
    mesh=component.static_mesh
    if mesh not in old.values():continue
    owner=component.get_owner()
    before=component.get_relative_transform()
    bounds=mesh.get_bounds()
    scale=component.get_editor_property('relative_scale3d')*(bounds.box_extent*2)/new_size
    center=before.transform_location(bounds.origin)
    component.modify()
    component.set_static_mesh(new)
    component.set_editor_property('override_materials',[])
    component.set_relative_scale3d(scale)
    component.set_relative_location(center,False,True)
    component.set_overlay_material(unreal.load_asset('/Game/GuLiStrike/Buildings/OutpostSymmetric/Materials/M_OutpostSymmetric_Outline'))
    component.set_default_custom_primitive_data_float(18,0.)
    report['components'].append({'component':component.get_path_name(),'old_mesh':mesh.get_path_name(),
        'new_scale':vec(scale),'new_location':vec(center),'preserved':'old displayed center and three-axis envelope'})

for asset in loaded:
    if isinstance(asset,unreal.GuLiBuildingCatalog):
        changed=0
        for mesh in old.values():
            count=unreal.GuLiModelAuthoringLibrary.remap_building_visual_cache(asset,mesh,new)
            if count<0:raise RuntimeError('Invalid cached building remap '+asset.get_path_name())
            changed+=count
        if changed:report['catalogs'].append(asset.get_path_name())
    elif isinstance(asset,unreal.DataTable):
        rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset));changed=False
        for row in rows:
            path=row.get('ResourcePath')
            if path and path.split('.')[0] in OLD:
                row['ResourcePath']=new.get_path_name();changed=True
        if changed:
            asset.modify()
            assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(asset,json.dumps(rows))
            report['tables'].append(asset.get_path_name())

# Delete the unreferenced historical redirector by object identity, never by a
# path delete which could resolve to and delete its destination mesh.
for data in AR.get_assets_by_package_name(ALIAS):
    redirector=data.get_asset()
    if redirector.get_class().get_name()=='ObjectRedirector':
        assert not refs(ALIAS),'Historical alias still has users'
        assert LIB.delete_loaded_asset(redirector)
        assert LIB.does_asset_exist(NEW)
        report['deleted_redirector']=ALIAS

for asset in loaded:
    if not LIB.save_loaded_asset(asset,False):raise RuntimeError('Referencer save failed '+asset.get_path_name())
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
AR.scan_modified_asset_files([str(ROOT/'Content'/
    (asset.get_path_name().split('.')[0].removeprefix('/Game/')+('.umap' if isinstance(asset,unreal.World) else '.uasset')))
    for asset in loaded])
checkpoint('referencers-saved',component_count=len(report['components']))
report['references_after_migration']={p:refs(p) for p in OLD}
for path,mesh in old.items():
    remaining=refs(path)
    if remaining:raise RuntimeError('Refusing to delete referenced mesh '+path+': '+str(remaining))
    assert LIB.delete_loaded_asset(mesh)
    report['deleted'].append(path)
    checkpoint('deleting-unreferenced-models',deleted=report['deleted'])
report['remaining_old_static_meshes']=[str(x.package_name) for p in OLD for x in AR.get_assets_by_package_name(p)
    if str(x.asset_class_path.asset_name)=='StaticMesh']
assert not report['remaining_old_static_meshes']
report['success']=True
(OUT/'ue-model-retirement.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'deleted':report['deleted'],
    'migrated_components':len(report['components']),'catalogs':report['catalogs'],'tables':report['tables'],
    'removed_editor_displays':report['removed_editor_displays']}))
