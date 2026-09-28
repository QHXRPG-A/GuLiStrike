"""Read only the live card review context before material replacement."""
from pathlib import Path
import json, traceback
import unreal
out=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Comic_Closeups_v6/Production/Inspection')
out.mkdir(parents=True,exist_ok=True)
r={'success':False}
try:
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    r['world']=w.get_path_name()
    r['pie']=unreal.WidgetService.is_pie_running()
    r['directors']=[]
    for d in actors:
        if d.get_class().get_name()!='BP_CardRevealDirector_C':continue
        item={'name':d.get_name(),'label':d.get_actor_label()}
        for key in ['MaximumTilt','CardAreaMultiplier','CardThicknessMultiplier','CardScale']:
            item[key]=d.get_editor_property(key)
        item['fronts']=[m.get_path_name() for m in d.get_editor_property('CardFrontMaterials')]
        r['directors'].append(item)
    r['success']=True
except Exception:r['error']=traceback.format_exc()
(out/'live-before.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
result=json.dumps(r,ensure_ascii=False)
print(result)

