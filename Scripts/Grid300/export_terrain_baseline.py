"""Export the current (including unsaved edits) Landscape height/weights, without saving the map."""
import hashlib
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'ArtSource/Navigation/Grid300/Baseline/Terrain'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name()=='/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
OUT.mkdir(parents=True,exist_ok=True)
manifest=OUT/'manifest.json'
if manifest.exists():
    result={'success':True,'already_captured':True,'path':str(manifest)}
else:
    landscapes=[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.Landscape)]
    assert len(landscapes)==1
    landscape=landscapes[0]
    name=landscape.get_name()
    height=OUT/'height.png'
    assert unreal.LandscapeService.export_heightmap(name,str(height))
    dims=unreal.LandscapeService.get_heightmap_dimensions(str(height))
    weights=[]
    for layer in ('FlatGround','Hills','Plateau','Rock','Beach','Water'):
        path=OUT/(layer+'.png')
        exported=unreal.LandscapeService.export_weight_map(name,layer,str(path))
        assert exported.success,str(exported)
        weights.append({'layer':layer,'file':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    result={'success':True,'schema':'guli-grid300/live-terrain-v1','map':world.get_path_name(),
            'landscape':name,'resolution':[dims.width,dims.height],
            'location':list(landscape.get_actor_location().to_tuple()),'scale':list(landscape.get_actor_scale3d().to_tuple()),
            'height':height.name,'height_sha256':hashlib.sha256(height.read_bytes()).hexdigest(),'weights':weights,
            'material':landscape.get_editor_property('landscape_material').get_path_name()}
    with manifest.open('x',encoding='utf-8') as stream:json.dump(result,stream,ensure_ascii=False,indent=2)
unreal.MCPythonHelper.submit_result(json.dumps(result,ensure_ascii=False))
