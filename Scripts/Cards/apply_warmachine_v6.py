"""Import authorized comic layers and configure the owned review map."""
import json
import traceback
import sys
from pathlib import Path
import unreal
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Cards')
from comic_card_edge_coverage import configure_artwork_clip,SPEED_COVERAGE_LAYOUTS

ROOT='/Game/GuLiStrike/Cards/WarMachineTarot'
ART=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Comic_Closeups_v6/Production')
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
    parallax_parent=copied(ROOT+'/Materials/M_CelCardParallax','M_CelCardParallax_Comic_v6')
    expressions=[e for e in unreal.ObjectIterator(unreal.MaterialExpression) if e.get_outer()==parallax_parent]
    action=next(e for e in expressions if isinstance(e,unreal.MaterialExpressionTextureSample)
                and e.material_expression_editor_x==-1920 and e.material_expression_editor_y==352)
    ability_parameter=next((e for e in expressions if isinstance(e,unreal.MaterialExpressionTextureObjectParameter)
                            and str(e.get_editor_property('parameter_name'))=='Ability Layer Map'),None)
    if not ability_parameter:
        ability_parameter=MEL.create_material_expression(parallax_parent,unreal.MaterialExpressionTextureObjectParameter,-7500,2200)
        ability_parameter.set_editor_property('parameter_name','Ability Layer Map')
        original=next(e for e in expressions if isinstance(e,unreal.MaterialExpressionTextureObjectParameter)
                      and str(e.get_editor_property('parameter_name'))=='BaseColor Map')
        ability_parameter.set_editor_property('texture',original.get_editor_property('texture'))
    assert MEL.connect_material_expressions(ability_parameter,'',action,'Tex')
    configure_artwork_clip(parallax_parent)
    assert EAL.save_loaded_asset(parallax_parent,False)
    # The final disc correction intentionally contains no detached ribbons;
    # keep the approved ribbons from the earlier v6 atlas as the ability source.
    speed_ability=texture(ART/'Layers'/'HighSpeed_Atlas.png','T_HighSpeed_Ability_Comic_v6')
    frame=texture(ART/'EmptyCardFrame.png','T_CelCardFrame_Comic_v6')
    frame_parent=copied(ROOT+'/Materials/M_CelCardUI','M_CelCardUI_Comic_v6')
    mask=next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer()==frame_parent)
    # Retain the generated frame's silver side lines and chamfered top corners.
    mask.set_editor_property('code','''float edge=max(max(step(UV.x,0.052),step(0.948,UV.x)),step(UV.y,0.050));
float corners=max(step(UV.x+UV.y*0.666667,0.124),step((1.0-UV.x)+UV.y*0.666667,0.124));
return max(max(edge,corners),step(PanelStart,UV.y));''')
    MEL.recompile_material(frame_parent)
    assert EAL.save_loaded_asset(frame_parent,False)
    fronts=[]
    frames=[]
    for name in ['FireRate','MissileDamage','HighSpeed']:
        file=ART/'Layers'/'Corrected'/(name+'_Atlas.png')
        if not file.exists():file=ART/'Layers'/(name+'_Atlas.png')
        atlas=texture(file,'T_'+name+'_Layers_Comic_v6')
        front=copied(ROOT+'/Materials/MI_'+name,'MI_'+name+'_Comic_v6')
        ui=copied(ROOT+'/Materials/MI_'+name+'_UI','MI_'+name+'_UI_Comic_v6')
        MEL.set_material_instance_parent(ui,frame_parent)
        MEL.set_material_instance_parent(front,parallax_parent)
        assert unreal.MaterialService.set_instance_texture_parameter(front.get_path_name(),'BaseColor Map',atlas.get_path_name())
        ability=speed_ability if name=='HighSpeed' else atlas
        assert unreal.MaterialService.set_instance_texture_parameter(front.get_path_name(),'Ability Layer Map',ability.get_path_name())
        assert unreal.MaterialService.set_instance_texture_parameter(ui.get_path_name(),'UI Map',frame.get_path_name())
        assert unreal.MaterialService.set_instance_scalar_parameter(ui.get_path_name(),'Text panel start',.765)
        scalars={'Layers global depth':4.,'Ability behind machinery':1. if name=='HighSpeed' else 0.,
                 'Foreground behind machinery':1. if name=='HighSpeed' else 0.}
        # Flash and barrel share depth; the launcher and emerging missile are
        # drawn on one layer. No pressure can detach these mechanical joints.
        depths=[0.,-.16,-.16,-.45,-.75,-1.1]
        if name=='MissileDamage':depths=[0.,-.10,-.25,-.45,-.75,-1.1]
        for i,depth in enumerate(depths,1):
            scalars[f'Layer {i} Depth']=depth
            scalars[f'Layer {i} Opacity']=1.
        for channel in 'RGBA':scalars['Detail emissive exponent ('+channel+' mask)']=.020 if channel in 'RG' else 0.
        for key,value in scalars.items():
            assert unreal.MaterialService.set_instance_scalar_parameter(front.get_path_name(),key,value),key
        # Generated tiles cover the illustration, not the UI panel. Fit all
        # layers into that same aperture before applying individual depth.
        layouts={i:((0.,-.0875),(.92,.745)) for i in range(1,7)}
        # Four-times depth and 16-degree corners need more opaque sky/ground
        # beyond the frame. The larger base cannot expose an empty atlas edge.
        layouts[6]=((0.,-.0875),(1.35,1.10))
        if name=='FireRate':
            # Attached, equally sized muzzle bursts are now part of the intact
            # machinery. Layer 2 contains only short free firing streaks.
            layouts[3]=((-.05,-.0875),(.92,.745))
            layouts[2]=((.20,-.08),(.55,.34))
        if name=='MissileDamage':
            # Restore the source's spread between near, middle and distant
            # missiles. The atlas generator centered the free sprites too far
            # right, which stacked them over the emerging missile in UE.
            layouts[1]=((-.20,-.1075),(.92,.745))
            layouts[2]=((-.18,-.1375),(.92,.745))
            layouts[4]=((0.,-.1475),(.92,.745))
        if name=='HighSpeed':
            # A completed disc has genuine transparent margins; preserve those
            # margins at the 16-degree limits instead of clipping its right rim.
            layouts[3]=((-.02,-.12),(.92,.745))
            layouts[2]=((0.,.025),(.92,.745))
            # Environment margins must cover the aperture at the right slot's
            # off-axis view plus the full 16-degree hover, not only head-on.
            layouts.update(SPEED_COVERAGE_LAYOUTS)
        for i,(offset,scale) in layouts.items():
            assert unreal.MaterialService.set_instance_vector_parameter(front.get_path_name(),f'Layer {i} Offset',*offset,0.,0.)
            assert unreal.MaterialService.set_instance_vector_parameter(front.get_path_name(),f'Layer {i} Scale',*scale,1.,1.)
        for mi in [front,ui]:
            MEL.update_material_instance(mi)
            assert EAL.save_loaded_asset(mi,False)
        fronts.append(front)
        frames.append(ui)
        REPORT['materials'].append({'card':name,'front':front.get_path_name(),'frame':ui.get_path_name(),'depth':4.,'depths':depths,'layouts':layouts,'ability_texture':ability.get_path_name()})
    director.set_editor_property('CardFrontMaterials',fronts)
    director.set_editor_property('CardTextMaterials',frames)
    director.set_editor_property('CardAreaMultiplier',2.)
    director.set_editor_property('CardThicknessMultiplier',2.)
    director.set_editor_property('MaximumTilt',16.)
    assert director.get_editor_property('MaximumTilt')==16.
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
(ART/'Inspection').mkdir(parents=True,exist_ok=True)
(ART/'Inspection'/'v6-integration.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(REPORT,ensure_ascii=False))
result=json.dumps(REPORT,ensure_ascii=False)
