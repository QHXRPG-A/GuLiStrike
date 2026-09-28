"""Import approved clean redraw layers and configure the owned review map."""
import json
import traceback
from pathlib import Path
import unreal

ROOT='/Game/GuLiStrike/Cards/WarMachineTarot'
ART=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Cel_Closeups_v5_Redraw/Production')
MAP=ROOT+'/Maps/LVL_WarMachineTarotReview'
EAL=unreal.EditorAssetLibrary
MEL=unreal.MaterialEditingLibrary
REPORT={'success':False,'textures':[],'materials':[]}


def texture(file,name):
    task=unreal.AssetImportTask()
    for key,value in {'filename':str(file),'destination_path':ROOT+'/Textures','destination_name':name,
                      'automated':True,'replace_existing':True,'save':True}.items():
        task.set_editor_property(key,value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex=unreal.load_asset(ROOT+'/Textures/'+name)
    assert tex,file
    for key,value in {'srgb':True,'compression_settings':unreal.TextureCompressionSettings.TC_BC7,
        'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP,'never_stream':True}.items():
        tex.set_editor_property(key,value)
    assert EAL.save_loaded_asset(tex,False)
    REPORT['textures'].append({'path':tex.get_path_name(),'source':str(file),'size':[tex.blueprint_get_size_x(),tex.blueprint_get_size_y()]})
    return tex


def copied(source,name):
    path=ROOT+'/Materials/'+name
    obj=unreal.load_asset(path) if EAL.does_asset_exist(path) else EAL.duplicate_asset(source,path)
    assert obj,path
    return obj


try:
    assert not unreal.WidgetService.is_pie_running(),'Stop owned preview before editing'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith(MAP+'.'),'Dedicated review map required'
    director=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='WarMachine_CardDirector')
    REPORT['previous_fronts']=[m.get_path_name() for m in director.get_editor_property('CardFrontMaterials')]
    frame=texture(ART/'EmptyCardFrame.png','T_CelCardFrame_Redraw_v5')
    frame_parent=copied(ROOT+'/Materials/M_CelCardUI','M_CelCardUI_Redraw_v5')
    mask=next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer()==frame_parent)
    # Retain the generated frame's silver side lines and chamfered top corners.
    mask.set_editor_property('code','''float edge=max(max(step(UV.x,0.051),step(0.949,UV.x)),step(UV.y,0.063));
float corners=max(step(UV.x+UV.y*0.666667,0.117),step((1.0-UV.x)+UV.y*0.666667,0.117));
return max(max(edge,corners),step(PanelStart,UV.y));''')
    MEL.recompile_material(frame_parent)
    assert EAL.save_loaded_asset(frame_parent,False)
    fronts=[]
    frames=[]
    for name in ['FireRate','MissileDamage','HighSpeed']:
        file=ART/'Layers'/'Corrected'/(name+'_Atlas.png')
        if not file.exists():file=ART/'Layers'/(name+'_Atlas.png')
        atlas=texture(file,'T_'+name+'_Layers_Redraw_v5')
        front=copied(ROOT+'/Materials/MI_'+name,'MI_'+name+'_Redraw_v5')
        ui=copied(ROOT+'/Materials/MI_'+name+'_UI','MI_'+name+'_UI_Redraw_v5')
        MEL.set_material_instance_parent(ui,frame_parent)
        assert unreal.MaterialService.set_instance_texture_parameter(front.get_path_name(),'BaseColor Map',atlas.get_path_name())
        assert unreal.MaterialService.set_instance_texture_parameter(ui.get_path_name(),'UI Map',frame.get_path_name())
        scalars={'Layers global depth':2.,'Ability behind machinery':1. if name=='HighSpeed' else 0.,
                 'Foreground behind machinery':1. if name=='HighSpeed' else 0.}
        # Flash and barrel share depth; the launcher and emerging missile are
        # drawn on one layer. No pressure can detach these mechanical joints.
        depths=[0.,-.16,-.16,-.45,-.75,-1.1]
        if name=='MissileDamage':depths=[0.,-.10,-.25,-.45,-.75,-1.1]
        for i,depth in enumerate(depths,1):
            scalars[f'Layer {i} Depth']=depth
            scalars[f'Layer {i} Opacity']=1.
        for channel in 'RGBA':scalars['Detail emissive exponent ('+channel+' mask)']=.035 if channel in 'RG' else 0.
        for key,value in scalars.items():
            assert unreal.MaterialService.set_instance_scalar_parameter(front.get_path_name(),key,value),key
        # Generated tiles cover the illustration, not the UI panel. Fit all
        # layers into that same aperture before applying individual depth.
        layouts={i:((0.,-.0825),(.92,.715)) for i in range(1,7)}
        layouts[6]=((0.,-.0825),(1.104,.858))
        if name=='FireRate':layouts[2]=((0.,-.065),(.92,.715))
        if name=='MissileDamage':
            layouts[1]=((-.36,-.13),(.92,.715))
            layouts[2]=((-.10,-.10),(.92,.715))
        for i,(offset,scale) in layouts.items():
            assert unreal.MaterialService.set_instance_vector_parameter(front.get_path_name(),f'Layer {i} Offset',*offset,0.,0.)
            assert unreal.MaterialService.set_instance_vector_parameter(front.get_path_name(),f'Layer {i} Scale',*scale,1.,1.)
        for mi in [front,ui]:
            MEL.update_material_instance(mi)
            assert EAL.save_loaded_asset(mi,False)
        fronts.append(front)
        frames.append(ui)
        REPORT['materials'].append({'card':name,'front':front.get_path_name(),'frame':ui.get_path_name(),'depth':2.,'depths':depths,'layouts':layouts})
    director.set_editor_property('CardFrontMaterials',fronts)
    director.set_editor_property('CardTextMaterials',frames)
    director.set_editor_property('CardAreaMultiplier',2.)
    director.set_editor_property('CardThicknessMultiplier',2.)
    assert director.get_editor_property('MaximumTilt')==12.
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    REPORT['map']={'path':MAP,'saved':True,'actor':director.get_actor_label(),
        'fronts':[m.get_path_name() for m in director.get_editor_property('CardFrontMaterials')],
        'frames':[m.get_path_name() for m in director.get_editor_property('CardTextMaterials')],
        'area':director.get_editor_property('CardAreaMultiplier'),'thickness':director.get_editor_property('CardThicknessMultiplier'),
        'tilt':director.get_editor_property('MaximumTilt'),'text_table':director.get_editor_property('CardTextDataTable').get_path_name(),
        'text_rows':[str(x) for x in director.get_editor_property('CardTextRowNames')]}
    REPORT['success']=True
except Exception:
    REPORT['error']=traceback.format_exc()
(ART/'Inspection').mkdir(exist_ok=True)
(ART/'Inspection'/'v5-integration.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(REPORT,ensure_ascii=False))
