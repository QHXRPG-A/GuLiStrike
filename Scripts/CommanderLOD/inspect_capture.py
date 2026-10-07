import unreal,json
s=getattr(unreal,'_commander_lod_capture_state',{})
rows=[]
for actor in getattr(unreal,'_commander_lod_preview_actors',[]):
 if not unreal.SystemLibrary.is_valid(actor):continue
 for comp in actor.get_components_by_class(unreal.MeshComponent):
  rows.append(dict(actor=actor.get_actor_label(),materials=[m.get_path_name() if m else None for m in comp.get_materials()]))
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,finished=s.get('finished'),start=s.get('start'),frames=len(s.get('frames',[])),error=s.get('error'),components=rows)))
