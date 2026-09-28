"""Import only a versioned preview copy of v6; leave production assets untouched."""
import unreal
import json
import sys
import traceback
from pathlib import Path

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/UI/WarMachineTarotCards/ModelComic_v8/ModelPreview'
SOURCE=OUT
BASE='/Game/GuLiStrike/Cards/WarMachineTarot/ModelReferences/v8'
REPORT={'success':False,'source_model_version':'WarMachine_LevelNodes_v6'}
WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
VAR='Interchange.FeatureFlags.Import.Enable'
OLD=unreal.SystemLibrary.get_console_variable_int_value(VAR)
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    original=unreal.load_asset('/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Cel')
    assert original
    REPORT['original_materials']=[s.material_interface.get_path_name() for s in original.static_materials]
    REPORT['world']=WORLD.get_path_name()
    sys.path.insert(0,str(ROOT/'Scripts'))
    import import_cel_model_assets as cel
    cel.OWNER='GuLi.CardModelReference.v8'
    cel.ALLOWED=(BASE+'/',)
    cel.REPORT={'assets':[],'materials':[]}
    # Reuse the same original cel pipeline, including fixed light and shade bands.
    unreal.SystemLibrary.execute_console_command(WORLD,VAR+' 0')
    atlas=cel.imported(SOURCE/'T_WarMachine_BaseColor.png',BASE+'/Textures/T_WarMachine_BaseColor')
    mask=cel.imported(SOURCE/'T_WarMachine_LineMask.png',BASE+'/Textures/T_WarMachine_LineMask')
    atlas.set_editor_property('srgb',True)
    mask.set_editor_property('srgb',False)
    mask.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
    cel.save(atlas);cel.save(mask)
    body=cel.material(BASE+'/Materials/M_WarMachine_Cel',base=atlas,mask=mask)
    contour=cel.material(BASE+'/Materials/M_WarMachine_Contour',contour=True)
    mesh=cel.imported(SOURCE/'SM_WarMachine_LevelNodes_Reference.fbx',BASE+'/Meshes/SM_WarMachine_LevelNodes_Reference',False)
    cel.mesh_settings(mesh,{'Body':body,'Contour':contour})
    REPORT.update(cel.REPORT)
    REPORT['source_export']=json.loads((OUT/'export-manifest.json').read_text(encoding='utf-8'))
    REPORT['success']=True
except Exception:
    REPORT['error']=traceback.format_exc()
    unreal.log_error(REPORT['error'])
finally:
    unreal.SystemLibrary.execute_console_command(WORLD,VAR+' '+str(OLD))
    (OUT/'import-manifest.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
    print('CARD_REFERENCE_V8_IMPORT',json.dumps(REPORT,ensure_ascii=False))
