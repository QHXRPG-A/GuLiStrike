"""Replace only review artwork with versioned six-layer ModelComic assets."""
import unreal
import json
import sys
import traceback
from pathlib import Path
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Cards')
from comic_card_edge_coverage import configure_artwork_clip

ROOT='/Game/GuLiStrike/Cards/WarMachineTarot'
MAP=ROOT+'/Maps/LVL_WarMachineTarotReview'
OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/ModelComic_v9/Production')
INSPECT=OUT/'Inspection'
INSPECT.mkdir(parents=True,exist_ok=True)
EAL=unreal.EditorAssetLibrary
MEL=unreal.MaterialEditingLibrary
OWNER='GuLiStrike.CardArtwork.ModelComic.v9'
NAMES=['FireRate','MissileDamage','HighSpeed']
R={'success':False,'assets':[],'materials':[],'preserved':{}}

def save(obj):
    assert obj.get_path_name().startswith(ROOT+'/')
    EAL.set_metadata_tag(obj,'GuLi.CardArtwork.Owner',OWNER)
    assert EAL.save_loaded_asset(obj,False)

def duplicate(source,name):
    path=ROOT+'/Materials/'+name
    if EAL.does_asset_exist(path):
        obj=unreal.load_asset(path)
        assert EAL.get_metadata_tag(obj,'GuLi.CardArtwork.Owner')==OWNER,path
    else:obj=EAL.duplicate_asset(source,path)
    assert obj,path
    EAL.set_metadata_tag(obj,'GuLi.CardArtwork.Owner',OWNER)
    return obj

def import_atlas(name):
    path=ROOT+'/Textures/T_'+name+'_Layers_ModelComic_v9'
    if EAL.does_asset_exist(path):
        assert EAL.get_metadata_tag(unreal.load_asset(path),'GuLi.CardArtwork.Owner')==OWNER
    task=unreal.AssetImportTask()
    for key,value in {'filename':str(OUT/'Layers'/f'{name}_Atlas.png'),
        'destination_path':ROOT+'/Textures','destination_name':path.rsplit('/',1)[1],
        'automated':True,'replace_existing':True,'save':False}.items():task.set_editor_property(key,value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex=unreal.load_asset(path)
    assert tex,name
    for key,value in {'srgb':True,'compression_settings':unreal.TextureCompressionSettings.TC_BC7,
        'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP,
        'never_stream':True}.items():tex.set_editor_property(key,value)
    assert tex.blueprint_get_size_x()==1254 and tex.blueprint_get_size_y()==1254
    save(tex)
    R['assets'].append({'path':tex.get_path_name(),'size':[1254,1254],'source':str(OUT/'Layers'/f'{name}_Atlas.png')})
    return tex

def scalar(mi,key,value):
    assert unreal.MaterialService.set_instance_scalar_parameter(mi.get_path_name(),key,value),key

def vector(mi,key,values):
    assert unreal.MaterialService.set_instance_vector_parameter(mi.get_path_name(),key,*values),key

def diagnostic(mat):
    d=unreal.MaterialNodeService.get_material_diagnostics(mat.get_path_name())
    return {'success':d.success,'compiled_ok':d.is_compiled_ok,'errors':list(d.compile_errors),
            'samples':d.texture_sample_count,'referenced_textures':list(d.referenced_texture_paths)}

try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith(MAP+'.')
    director=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
                  if a.get_actor_label()=='WarMachine_CardDirector')
    old_fronts=list(director.get_editor_property('CardFrontMaterials'))
    old_frames=list(director.get_editor_property('CardTextMaterials'))
    keep=['MaximumTilt','CardAreaMultiplier','CardThicknessMultiplier','EntryDuration','FlipDuration',
          'FlashDuration','ExitDuration','SimpleCardFrame']
    R['preserved']={key:director.get_editor_property(key) for key in keep}
    R['preserved']['frames']=[x.get_path_name() for x in old_frames]
    R['preserved']['text_table']=director.get_editor_property('CardTextDataTable').get_path_name()
    R['preserved']['text_rows']=[str(x) for x in director.get_editor_property('CardTextRowNames')]
    before={'fronts':[x.get_path_name() for x in old_fronts],**R['preserved']}
    backup=INSPECT/'map-artwork-before.json'
    if not backup.exists():backup.write_text(json.dumps(before,ensure_ascii=False,indent=2),encoding='utf-8')
    textures={name:import_atlas(name) for name in NAMES}
    parent=duplicate(ROOT+'/Materials/M_CelCardParallax_Comic_v6','M_CelCardParallax_ModelComic_v9')
    nodes=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==parent]
    for n in nodes:
        if isinstance(n,unreal.MaterialExpressionTextureObjectParameter) and str(n.get_editor_property('parameter_name')) in ['BaseColor Map','Ability Layer Map']:
            n.set_editor_property('texture',textures['FireRate'])
    # Body pixels are fully solid while their extraction boundary remains antialiased.
    body_alpha=next(n for n in nodes if str(n.get_editor_property('desc'))=='WM_LAYER_3_Alpha')
    body_alpha.set_editor_property('code','return smoothstep(0.12,0.72,A)*Inside*Opacity;')
    configure_artwork_clip(parent)
    save(parent)
    R['parent_diagnostics']=diagnostic(parent)
    assert R['parent_diagnostics']['compiled_ok'],R['parent_diagnostics']
    fronts=[]
    for index,name in enumerate(NAMES):
        mi=duplicate(old_fronts[index].get_path_name(),'MI_'+name+'_ModelComic_v9')
        MEL.set_material_instance_parent(mi,parent)
        for param in ['BaseColor Map','Ability Layer Map']:
            assert unreal.MaterialService.set_instance_texture_parameter(mi.get_path_name(),param,textures[name].get_path_name()),param
        # Keep the intact body at the card's reference plane. Only independent
        # foreground/background move across its silhouette, so the body cannot
        # uncover the atlas edge when camera off-axis and hover tilt combine.
        depths=[.10,.04,0.,-.40,-.75,-1.10]
        values={'Layers global depth':4.,'Ability behind machinery':1. if name=='HighSpeed' else 0.,
                'Foreground behind machinery':0.}
        for i,depth in enumerate(depths,1):
            values[f'Layer {i} Depth']=depth
            values[f'Layer {i} Opacity']=1.
        for channel in 'RGBA':values[f'Detail emissive exponent ({channel} mask)']=.012 if channel in 'RG' else 0.
        for key,value in values.items():scalar(mi,key,value)
        # Common coordinates for connected body and ability artwork. Environmental
        # planes overscan the fixed aperture; the opaque base always fills it.
        layouts={i:((0.,-.0875),(.92,.745)) for i in range(1,7)}
        layouts[5]=((0.,-.0875),(1.36,1.10))
        layouts[6]=((0.,-.0875),(1.65,1.34))
        if name=='HighSpeed':
            layouts[2]=((0.,-.0875),(1.30,1.05))
            layouts[4]=((0.,-.0875),(1.34,1.08))
        for i,(offset,scale) in layouts.items():
            vector(mi,f'Layer {i} Offset',(*offset,0.,0.))
            vector(mi,f'Layer {i} Scale',(*scale,1.,1.))
        MEL.update_material_instance(mi);save(mi)
        assert MEL.get_material_instance_texture_parameter_value(mi,'BaseColor Map')==textures[name]
        assert MEL.get_material_instance_scalar_parameter_value(mi,'Layers global depth')==4.
        fronts.append(mi)
        R['materials'].append({'card':name,'front':mi.get_path_name(),'atlas':textures[name].get_path_name(),
            'depth':4.,'depths':depths,'layouts':layouts,'frame':old_frames[index].get_path_name()})
    director.set_editor_property('CardFrontMaterials',fronts)
    for key in keep:assert director.get_editor_property(key)==R['preserved'][key],key
    assert director.get_editor_property('MaximumTilt')==16.
    assert director.get_editor_property('CardAreaMultiplier')==2.
    assert director.get_editor_property('CardThicknessMultiplier')==2.
    assert list(director.get_editor_property('CardTextMaterials'))==old_frames
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    R['map']={'path':MAP,'saved':True,'actor':director.get_actor_label(),
        'fronts':[x.get_path_name() for x in director.get_editor_property('CardFrontMaterials')]}
    table=director.get_editor_property('CardTextDataTable')
    R['text_csv']=unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(table)
    R['success']=True
except Exception:
    R['error']=traceback.format_exc()
    unreal.log_error(R['error'])
(INSPECT/'integration.json').write_text(json.dumps(R,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'success':R['success'],'error':R.get('error'),'assets':R.get('map')},ensure_ascii=False))
