"""Read authored data through Unreal, never through binary package files."""
from pathlib import Path
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
result={}
for cls,prop in (("GuLiUnitDataSettings","soldier_data_table"),
                 ("GuLiCommanderDataSettings","skill_data_table"),
                 ("GuLiCommanderDataSettings","unit_skill_data_table"),
                 ("GuLiCommanderDataSettings","weapon_mount_data_table"),
                 ("GuLiRogueCardSettings","cards"),
                 ("GuLiModelRegistrySettings","models_table")):
    settings=unreal.get_default_object(unreal.load_class(None,"/Script/GuLiStrike."+cls))
    ref=settings.get_editor_property("".join(part.capitalize() for part in prop.split("_")))
    table=unreal.load_asset(str(ref)) if not isinstance(ref,unreal.DataTable) else ref
    if not table: raise RuntimeError("Missing configured table "+prop+": "+str(ref))
    result[prop]={"path":table.get_path_name(),"rows":json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))}
table=unreal.load_asset("/Game/GuLiStrike/Data/DT_GuLiStrikeBuildings_Buildings")
result["buildings"]={"path":table.get_path_name(),"rows":json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))}
catalog=unreal.get_default_object(unreal.load_class(None,"/Script/GuLiStrike.GuLiCommanderSkillSettings")).get_editor_property("Catalog")
if not isinstance(catalog,unreal.Object): catalog=unreal.load_asset(str(catalog))
result["active_catalog"]={"path":catalog.get_path_name(),"global_skills":[str(x) for x in catalog.get_editor_property("GlobalSkills")],
                          "skills":[{"id":str(s.get_editor_property("SkillId")),"class":str(s.get_editor_property("ExecutorClass")),
                                     "scope":str(s.get_editor_property("Scope")),"configuration":str(s.get_editor_property("Configuration"))}
                                    for s in catalog.get_editor_property("Skills")]}
output=root/"TestResults/HybridFullAcceptance/Implementation/integrated-authored-inputs.json"
output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
unreal.MCPythonHelper.submit_result(json.dumps({"success":True,"output":str(output),"tables":list(result)}))
