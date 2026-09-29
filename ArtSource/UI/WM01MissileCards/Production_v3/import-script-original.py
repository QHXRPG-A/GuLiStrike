"""Import approved-card production layers as new owned assets; existing cards stay intact."""
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/UI/WM01MissileCards/Production_v3'
DEST='/Game/GuLiStrike/Cards/Commander/WM01'
SOURCE=DEST+'/FireRate/Materials/MI_FireRate_ModelComic_v9'
LIB=unreal.EditorAssetLibrary
EDIT=unreal.MaterialEditingLibrary
OWNER='GuLi.WM01MissileCards.20260929'
report={'success':False,'cards':[],'user_art_review':{'MissilePod':'approved Review_v2','RainSalvo':'approved Review_v3'},'layer_visual_review':'pending UE composition'}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    for card in ('MissilePod','RainSalvo'):
        path=DEST+'/'+card
        texture_path=path+'/Textures/T_'+card+'_Layers_ModelComic_v1'
        if LIB.does_asset_exist(texture_path):
            assert LIB.get_metadata_tag(unreal.load_asset(texture_path),'GuLi.Owner')==OWNER
        task=unreal.AssetImportTask()
        for key,value in {'filename':str(OUT/(card+'_Layers.png')),'destination_path':path+'/Textures',
                          'destination_name':texture_path.rsplit('/',1)[-1],'automated':True,'replace_existing':True,'save':False}.items():
            task.set_editor_property(key,value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture=unreal.load_asset(texture_path)
        assert texture and texture.blueprint_get_size_x()==1254 and texture.blueprint_get_size_y()==1254
        for key,value in {'srgb':True,'compression_settings':unreal.TextureCompressionSettings.TC_BC7,
                          'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP,
                          'never_stream':False}.items():
            texture.set_editor_property(key,value)
        LIB.set_metadata_tag(texture,'GuLi.Owner',OWNER)
        assert LIB.save_loaded_asset(texture,False)
        material_path=path+'/Materials/MI_'+card+'_ModelComic_v1'
        if LIB.does_asset_exist(material_path):
            mi=unreal.load_asset(material_path)
            assert LIB.get_metadata_tag(mi,'GuLi.Owner')==OWNER
        else:
            mi=LIB.duplicate_asset(SOURCE,material_path)
            LIB.set_metadata_tag(mi,'GuLi.Owner',OWNER)
        for param in ('BaseColor Map','Ability Layer Map'):
            assert unreal.MaterialService.set_instance_texture_parameter(mi.get_path_name(),param,texture.get_path_name())
        for index,depth in enumerate((.10,.04,0.,-.40,-.75,-1.10),1):
            assert unreal.MaterialService.set_instance_scalar_parameter(mi.get_path_name(),f'Layer {index} Depth',depth)
            assert unreal.MaterialService.set_instance_scalar_parameter(mi.get_path_name(),f'Layer {index} Opacity',1.)
            assert unreal.MaterialService.set_instance_vector_parameter(mi.get_path_name(),f'Layer {index} Scale',.92,.745,1,1)
            assert unreal.MaterialService.set_instance_vector_parameter(mi.get_path_name(),f'Layer {index} Offset',0,-.0875,0,0)
        for key,value in {'Layers global depth':4.,'Ability behind machinery':0.,'Foreground behind machinery':0.}.items():
            assert unreal.MaterialService.set_instance_scalar_parameter(mi.get_path_name(),key,value)
        # Register the extracted foreground to the pod attachment points. The raw
        # generator output is retained; these explicit transforms remain editable.
        offset=(-.06,-.1525) if card=='MissilePod' else (.0731,-.1979)
        assert unreal.MaterialService.set_instance_vector_parameter(mi.get_path_name(),'Layer 2 Offset',*offset,0,0)
        if card=='RainSalvo':
            assert unreal.MaterialService.set_instance_vector_parameter(mi.get_path_name(),'Layer 2 Scale',.736,.596,1,1)
            assert unreal.MaterialService.set_instance_scalar_parameter(mi.get_path_name(),'Foreground behind machinery',1.)
        for index,scale in ((4,(1.08,.87)),(5,(1.2,.97)),(6,(1.30,1.055))):
            assert unreal.MaterialService.set_instance_vector_parameter(mi.get_path_name(),f'Layer {index} Scale',*scale,1,1)
        LIB.set_metadata_tag(mi,'GuLi.Owner',OWNER)
        LIB.set_metadata_tag(mi,'GuLi.Layer2','Three assembly arms' if card=='MissilePod' else 'Blue translucent bonus missile and opaque green +1 only')
        LIB.set_metadata_tag(mi,'GuLi.VisualReview','Pending production alignment/tilt acceptance')
        EDIT.update_material_instance(mi)
        assert LIB.save_loaded_asset(mi,False)
        loaded=unreal.load_asset(material_path)
        assert EDIT.get_material_instance_texture_parameter_value(loaded,'BaseColor Map')==texture
        report['cards'].append({'card':card,'material':loaded.get_path_name(),'texture':texture.get_path_name(),
                                'layer2':LIB.get_metadata_tag(loaded,'GuLi.Layer2'),'layer2_offset':offset,'saved_readback':True})
    report['success']=True
except Exception:
    report['error']=traceback.format_exc()
(OUT/'ue-import.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report,ensure_ascii=False))
