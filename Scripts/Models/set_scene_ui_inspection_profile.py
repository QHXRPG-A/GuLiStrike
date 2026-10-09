"""Toggle saved inspection profiles in the running client; editor map is untouched."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')
requested = globals().pop('GULI_SCENE_UI_PROFILE', 'None')
w = next(w for w in unreal.ObjectIterator(unreal.World) if '/UEDPIE_2_' in w.get_path_name())
changed = []
for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
    label = a.get_actor_label()
    if not label.startswith('SceneUI_Profile_'): continue
    active = label == 'SceneUI_Profile_' + requested
    if isinstance(a, unreal.PostProcessVolume): a.enabled = active
    else:
        a.set_actor_hidden_in_game(not active)
        for c in a.get_components_by_class(unreal.SceneComponent): c.set_visibility(active, True)
    changed.append({'label': label, 'active': active})
unreal.MCPythonHelper.submit_result(json.dumps({'profile': requested, 'changed': changed, 'next_frame_capture_required': True}))
