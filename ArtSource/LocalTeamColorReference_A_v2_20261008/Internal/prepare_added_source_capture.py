"""Capture three existing gameplay buildings for reference A; no source assets saved."""
from pathlib import Path
folder=Path(__file__).resolve().parent
text=(folder/'capture_original_views.py').read_text(encoding='utf8')
text=text.replace("('ShieldGenerator','ManualOutpost')", "('MissileTurret','SentryTurret','ResourceFactory')")
text=text.replace("len(report['models'])==2", "len(report['models'])==3")
text=text.replace("source-capture.json", "source-capture-added-buildings.json")
text=text.replace("        for slot in model['materials']:\n            path='/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial'\n            if path:body.set_material(slot['slot'],unreal.load_asset(path))", "        gray=unreal.load_asset('/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial')\n        for mesh_component in group['components']:\n            for material_index in range(mesh_component.get_num_materials()):\n                mesh_component.set_material(material_index,gray)")
text=text.replace("    unreal.EditorPythonScripting.set_keep_python_script_alive(False)", "    unreal.EditorPythonScripting.set_keep_python_script_alive(False)\n    unreal.SystemLibrary.quit_editor()")
assert "len(report['models'])==3" in text
assert 'save_loaded_asset' not in text and 'save_current_level' not in text
target=folder/'capture_added_original_views.py'
target.write_text(text,encoding='utf8')
print(target)
