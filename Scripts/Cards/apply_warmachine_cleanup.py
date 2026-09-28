"""Import the cleaned v4 artwork and select its copies in the review map.

Run through Scripts/ue_exec.py with the review map open and PIE stopped.
Keeps v3 source textures/material instances, parallax logic and editable text.
"""
import json
import traceback
from pathlib import Path
import unreal

ROOT='/Game/GuLiStrike/Cards/WarMachineTarot'
ART=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Cel_Closeups_v4_Clean')
MAP=ROOT+'/Maps/LVL_WarMachineTarotReview'
EAL=unreal.EditorAssetLibrary
REPORT={'success':False,'textures':[],'materials':[]}


def import_texture(file,name):
    assert file.is_file(),str(file)
    task=unreal.AssetImportTask()
    task.filename=str(file)
    task.destination_path=ROOT+'/Textures'
    task.destination_name=name
    task.automated=True
    task.replace_existing=True
    task.save=True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture=unreal.load_asset(ROOT+'/Textures/'+name)
    assert texture,str(file)
    for key,value in {'srgb':True,'compression_settings':unreal.TextureCompressionSettings.TC_BC7,
        'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP,'never_stream':True}.items():
        texture.set_editor_property(key,value)
    assert EAL.save_loaded_asset(texture,False)
    REPORT['textures'].append({'asset':texture.get_path_name(),'source':str(file),
        'size':[texture.blueprint_get_size_x(),texture.blueprint_get_size_y()]})
    return texture


def copy_material(source,name):
    dest=ROOT+'/Materials/'+name
    obj=unreal.load_asset(dest) if EAL.does_asset_exist(dest) else EAL.duplicate_asset(source,dest)
    assert obj,dest
    return obj


try:
    assert not unreal.WidgetService.is_pie_running(),'Stop the review PIE before applying artwork'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith(MAP+'.'), 'Open only the dedicated review map'
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Review pending map edits first'
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    director=next(a for a in actors if a.get_actor_label()=='WarMachine_CardDirector')
    REPORT['previous_fronts']=[m.get_path_name() for m in director.get_editor_property('CardFrontMaterials')]
    REPORT['previous_frames']=[m.get_path_name() for m in director.get_editor_property('CardTextMaterials')]
    frame=import_texture(ART/'EmptyCardFrame.png','T_CelCardFrame_Clean_v4')
    fronts=[]
    frames=[]
    for name in ['FireRate','MissileDamage','HighSpeed']:
        tex=import_texture(ART/'Layers'/(name+'_Atlas.png'),'T_'+name+'_Layers_Clean_v4')
        front=copy_material(ROOT+'/Materials/MI_'+name,'MI_'+name+'_Clean_v4')
        ui=copy_material(ROOT+'/Materials/MI_'+name+'_UI','MI_'+name+'_UI_Clean_v4')
        assert unreal.MaterialService.set_instance_texture_parameter(front.get_path_name(),'BaseColor Map',tex.get_path_name())
        assert unreal.MaterialService.set_instance_texture_parameter(ui.get_path_name(),'UI Map',frame.get_path_name())
        for mi in [front,ui]:
            unreal.MaterialEditingLibrary.update_material_instance(mi)
            assert EAL.save_loaded_asset(mi,False)
        fronts.append(front)
        frames.append(ui)
        REPORT['materials'].append({'card':name,'front':front.get_path_name(),'frame':ui.get_path_name()})
    director.set_editor_property('CardFrontMaterials',fronts)
    director.set_editor_property('CardTextMaterials',frames)
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    REPORT['map']={'saved':True,'path':MAP,'actor':director.get_actor_label(),
        'fronts':[m.get_path_name() for m in director.get_editor_property('CardFrontMaterials')],
        'text_table':director.get_editor_property('CardTextDataTable').get_path_name(),
        'text_rows':[str(n) for n in director.get_editor_property('CardTextRowNames')],
        'maximum_tilt':director.get_editor_property('MaximumTilt')}
    REPORT['success']=True
except Exception:
    REPORT['error']=traceback.format_exc()
(ART/'Inspection').mkdir(exist_ok=True)
(ART/'Inspection'/'applied-cleanup.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(REPORT,ensure_ascii=False))
