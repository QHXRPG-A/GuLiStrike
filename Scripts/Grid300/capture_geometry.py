"""Read static collision/navigation metadata; never samples images or saves the level."""
import json
from pathlib import Path
import unreal

def main():
    root=Path(unreal.Paths.project_dir()).resolve()
    out=root/'ArtSource/Navigation/Grid300/Baseline/geometry.json'
    if out.exists(): return {'success':True,'already_captured':True,'path':str(out)}
    levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not levels.is_in_play_in_editor()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name()=='/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
    rows=[]
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        components=[]
        for c in actor.get_components_by_class(unreal.PrimitiveComponent):
            values={}
            for k in ('can_ever_affect_navigation','collision_enabled','mobility','area_class','box_extent','sphere_radius'):
                try:
                    v=c.get_editor_property(k)
                    values[k]=list(v.to_tuple()) if isinstance(v,unreal.Vector) else str(v)
                except Exception: pass
            try: values['collision_enabled']=str(c.get_collision_enabled())
            except Exception: pass
            try:
                b=c.get_local_bounds();values['local_bounds']=[list(x.to_tuple()) for x in b]
            except Exception: pass
            if values: components.append({'name':c.get_name(),'class':c.get_class().get_name(),'values':values})
        cls=actor.get_class().get_name()
        if components or 'NavModifier' in cls:
            row={'name':actor.get_name(),'label':actor.get_actor_label(),'class':cls,'components':components}
            if 'NavModifier' in cls:
                for k in ('area_class','area_class_to_replace'):
                    try:
                        v=actor.get_editor_property(k);row[k]=v.get_path_name() if v else None
                    except Exception: pass
            rows.append(row)
    out.write_text(json.dumps({'schema':'guli-grid300/static-geometry-v1','actors':rows},ensure_ascii=False,indent=2),encoding='utf-8')
    return {'success':True,'actors':len(rows),'path':str(out)}

unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
