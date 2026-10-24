"""Read the open editor into an immutable standalone snapshot. Never saves or edits UE packages."""
import datetime
import hashlib
import json
from pathlib import Path
import uuid
import unreal


def main():
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not world or level.is_in_play_in_editor():raise RuntimeError('请在普通编辑器中打开目标地图，退出PIE后再导出。')
    map_package=world.get_path_name().split('.')[0]
    if map_package!='/Game/Maps/LVL_CommanderMassPrototype':raise RuntimeError('当前快照入口仅针对已确认的指挥官地图。')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    lands=[a for a in actors if isinstance(a,unreal.Landscape)]
    if len(lands)!=1:raise RuntimeError('需要唯一Landscape；不导出缺失代理的局部地形。')
    land=lands[0];service=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    snapshot=service.get_snapshot()
    if not snapshot.success:raise RuntimeError('地图布局校验失败: '+str([(i.field,i.message) for i in snapshot.issues]))
    layout=json.loads(snapshot.json);density=service.get_density_snapshot()
    if not density.success:raise RuntimeError('资源密度快照失败。')
    snapshot_id=str(uuid.uuid4());root=Path(unreal.Paths.project_dir()).resolve()/'ArtSource/MapDesigner/Snapshots'/snapshot_id
    root.mkdir(parents=True)
    (root/'layout.json').write_bytes(snapshot.json.encode('utf-8'))
    if density.json:(root/'density_layers.json').write_bytes(density.json.encode('utf-8'))
    warnings=[];catalog={'schema_version':1,'types':layout['types']}
    if hasattr(service,'get_standalone_type_catalog'):
        result=service.get_standalone_type_catalog()
        if not result.success:raise RuntimeError('完整类型目录导出失败。')
        catalog=json.loads(result.json);catalog_text=result.json
    else:
        known={t['type_id'] for t in catalog['types']}
        omitted=[str(t.get_editor_property('type_id')) for t in service.list_types() if str(t.get_editor_property('type_id')) not in known]
        if omitted:warnings.append('尚未加载新原生适配，未使用类型待补齐: '+', '.join(omitted))
        catalog_text=json.dumps(catalog,ensure_ascii=False,sort_keys=True,separators=(',',':'))+'\n'
    (root/'type_catalog.json').write_bytes(catalog_text.encode('utf-8'))
    height=root/'height.png'
    result=unreal.LandscapeService.export_heightmap(land.get_name(),str(height))
    if not result:raise RuntimeError('高度图导出失败。')
    dimensions=unreal.LandscapeService.get_heightmap_dimensions(str(height));resolution=[dimensions.width,dimensions.height]
    weights=[]
    for name in ('FlatGround','Hills','Plateau','Rock','Beach','Water'):
        filename=name+'.png';result=unreal.LandscapeService.export_weight_map(land.get_name(),name,str(root/filename))
        if not result.success:raise RuntimeError('权重导出失败: '+name)
        weights.append({'layer':name,'file':filename})
    # LayerInfo paths can be resolved from the unchanged source Landscape during import.
    geometry=[];follow={};by_key={m['marker_key']:m['marker_id'] for m in layout['markers']}
    for actor in actors:
        if isinstance(actor,unreal.GuLiMapMarker):
            record=actor.get_editor_property('record');key=str(record.get_editor_property('marker_key'))
            try:enabled=actor.get_editor_property('standalone_follow_terrain')
            except Exception:enabled=True
            if key in by_key:follow[by_key[key]]=bool(enabled)
        if any(str(t).startswith('GuLiMapDesignBuffer_') for t in actor.tags):continue
        kind=None
        if isinstance(actor,unreal.NavModifierVolume):
            area=actor.get_editor_property('area_class')
            if area and area.get_path_name().endswith('NavArea_Null'):kind='forbidden'
        elif isinstance(actor,unreal.StaticMeshActor):
            for component in actor.get_components_by_class(unreal.StaticMeshComponent):
                if component.get_editor_property('can_ever_affect_navigation') and component.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION:kind='collision';break
        if kind:
            center,extent=actor.get_actor_bounds(False)
            geometry.append({'actor':actor.get_path_name(),'kind':kind,'bounds':[list(center.to_tuple()),list(extent.to_tuple())]})
    (root/'static_geometry.json').write_bytes((json.dumps(geometry,ensure_ascii=False,sort_keys=True,separators=(',',':'))+'\n').encode('utf-8'))
    (root/'editor.json').write_bytes((json.dumps({'ground_follow':follow},sort_keys=True,separators=(',',':'))+'\n').encode('utf-8'))
    manifest={'schema':'guli-map-design/snapshot-v1','snapshot_id':snapshot_id,'captured_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'map_package':map_package,'landscape':land.get_name(),'resolution':resolution,'height_file':'height.png',
              'location':list(land.get_actor_location().to_tuple()),'scale':list(land.get_actor_scale3d().to_tuple()),
              'material':land.get_editor_property('landscape_material').get_path_name(),'weights':weights,'warnings':warnings,
              'baseline':{'layout_json':hashlib.sha256(snapshot.json.encode('utf-8')).hexdigest(),
                          'density_json':hashlib.sha256(density.json.encode('utf-8')).hexdigest(),
                          'type_catalog_json':hashlib.sha256(catalog_text.encode('utf-8')).hexdigest()}}
    if hasattr(unreal,'GuLiMapDesignAuthoringLibrary'):
        baseline=unreal.GuLiMapDesignAuthoringLibrary.read_source_baseline()
        if not baseline.success:raise RuntimeError(baseline.message)
        manifest['baseline'].update({k:v for k,v in json.loads(baseline.json).items() if isinstance(v,str)})
    (root/'manifest.json').write_bytes((json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
    return {'success':True,'snapshot_id':snapshot_id,'directory':str(root),'resolution':resolution,'markers':len(layout['markers']),'types':len(catalog['types']),'warnings':warnings}


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
