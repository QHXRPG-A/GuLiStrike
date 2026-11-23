"""Catalog-driven multi-artwork review using the existing reveal actor and input flow."""
import gc
import json
import math
from pathlib import Path
import unreal
from card_artwork_catalog import GROUND_MASK_LAYERS
from card_reveal_graph import B, Graph, Node, require, variable, function, compile_save

SYSTEM='/Game/GuLiStrike/CardSystem/RevealDemo'
DIRECTOR=SYSTEM+'/Blueprints/BP_CardRevealDirector'
CARD=SYSTEM+'/Blueprints/BP_ParallaxRevealCard'
BASE_PC=SYSTEM+'/Blueprints/BP_CardRevealPlayerController'
PC=SYSTEM+'/Blueprints/BP_CardArtworkCatalogController'
GM=SYSTEM+'/Blueprints/BP_CardArtworkCatalogGameMode'
HUD=SYSTEM+'/UI/WBP_CardRevealHUD'
SELECTOR=SYSTEM+'/UI/WBP_CardArtworkCatalogSelector'
FACE=SYSTEM+'/UI/WBP_CardArtworkGroundReviewFace'
IA=SYSTEM+'/Input/IA_CardRevealClick'
IMC=SYSTEM+'/Input/IMC_CardReveal'
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
W=unreal.WidgetService


def theme_rows():
    return {r['Name']:r for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeGroundHUD_Elements.json').read_text(encoding='utf-8'))}


def theme_color(name):
    from card_rich_text import linear_color
    channels=linear_color(theme_rows()[name]['ColorSRGB'])
    return '('+','.join(f'{k}={v}' for k,v in channels.items())+')'


def cls(path):return path+'.'+path.rsplit('/',1)[1]+'_C'


def widget(path,kind,name,parent=''):
    if not W.widget_exists(path,name):
        result=W.add_component(path,kind,name,parent,True)
        require(result.success,str(result))


def property_(path,name,key,value):require(W.set_property(path,name,key,str(value)),f'{name}.{key}')


def slot(path,name,x,y,w,h,ax=0,ay=0,alignx=0,aligny=0):
    for key,value in {'Anchor Min X':ax,'Anchor Max X':ax,'Anchor Min Y':ay,'Anchor Max Y':ay,
        'Alignment X':alignx,'Alignment Y':aligny,'Position X':x,'Position Y':y,'Size X':w,'Size Y':h}.items():
        property_(path,name,key,value)


def ensure_widget(path):
    if not B.blueprint_exists(path):require(B.create_blueprint(path.rsplit('/',1)[1],'UserWidget',path.rsplit('/',1)[0]),path)


def bind(g,owner,dispatcher,target,callback):
    n=Node(g,B.add_delegate_bind_node(g.bp,g.name,owner,dispatcher,*g.pos()))
    n.put('self',target);n.put('execute',g.tail)
    d=Node(g,B.add_create_delegate_node(g.bp,g.name,callback,*g.pos()))
    n.put('Delegate',d['OutputDelegate']);g.tail=n['then']


def preview_copy(artwork):
    """Derived first-acquisition copy; values and strings both come from exported Excel."""
    cards=json.loads((ROOT/'data/Json/DT_GuLiStrikeRogueCards_Cards.json').read_text(encoding='utf-8'))
    card=next((r for r in cards if r['FrontMaterial']==artwork['Material']),None)
    if not card:return artwork['Title'],''
    texts={r['TextId']:r['Content'] for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeGameTexts_Texts.json').read_text(encoding='utf-8'))}
    title,description=(texts[key] for key in card['TextIds'][:2])
    effects={str(r['Id']):r for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeRogueCards_Effects.json').read_text(encoding='utf-8'))}
    configs={r['Id']:r for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeRogueCards_GroundWeaponEffects.json').read_text(encoding='utf-8'))}
    effect=effects.get(str(card['EffectIds'][0])) if card.get('EffectIds') else None
    if effect and effect['Handler']=='ground/fire_rate':
        first=f"{configs[effect['ConfigId']]['FireRateBonusPercent']*100:g}"
        description=description.format('1',first,first)
    elif effect and effect['Handler']=='ground/explosive_package':
        config=configs[effect['ConfigId']]
        description=description.format('1',config['ExplosiveIntervals'][0],f"{config['ExplosiveDamageMultiplier']:g}",f"{config['ExplosiveRadiusCentimeters']/100:g}")
    elif effect and effect['Handler']=='ground/explosive_enhancement':
        config=configs[effect['ConfigId']]
        description=description.format('1',f"{float(config['ExplosiveRadiusBonuses'][0])*100:g}",f"{float(config['ExplosiveDamageBonuses'][0])*100:g}")
    elif effect and effect['Handler']=='ground/damage_bonus':
        first=f"{float(configs[effect['ConfigId']]['DamageBonusIncrements'][0])*100:g}"
        description=description.format('1',first,first)
    elif effect and effect['Handler']=='ricochet':
        ranks=json.loads((ROOT/'data/Json/DT_GuLiStrikeRogueCards_GroundRicochet.json').read_text(encoding='utf-8'))
        config=next(row for row in ranks if row['Id']==effect['ConfigId'])
        description=description.format('1',config['TargetCounts'][0],f"{float(config['DamageFractions'][0])*100:g}")
    elif '{' in description: description=''  # No invented numbers for other catalog effects.
    return title,description


def ensure_review_pointer_input():
    # Upgrade older reveal directors once when preparing this review from source.
    from card_artwork_config import write_start_function, write_update_card_function, write_tick_function
    from card_presentation_size import write_pointer_function
    writers={'UpdatePointer':('TextureTarget',write_pointer_function),'UpdateCard':('ReviewFrameHalfExtent',write_update_card_function),
             'TickPresentation':('ActiveCardCount',write_tick_function)}
    missing=[write for fn,(pin,write) in writers.items() if not any(pin in n.pin_names for n in B.get_nodes_in_graph(DIRECTOR,fn))]
    if not B.function_exists(DIRECTOR,'SetPointerFrameFromCard') or not any(
            'Card' in n.pin_names and 'CallFunction' in n.node_type
            and B.get_node_details(DIRECTOR,'StartPresentation',n.node_id).function_name=='SetPointerFrameFromCard'
            for n in B.get_nodes_in_graph(DIRECTOR,'StartPresentation')):
        missing.append(write_start_function)
    if not missing:
        return
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert DIRECTOR not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    for write in missing:
        write()
    compile_save(DIRECTOR)


def make_face():
    ensure_widget(FACE)
    widget(FACE,'CanvasPanel','Canvas')
    widget(FACE,'GuLiGroundCardFace','GroundFrame','Canvas')
    slot(FACE,'GroundFrame',0,0,1280,1556)
    property_(FACE,'GroundFrame','Visibility','HitTestInvisible')
    for kind,name,parent in [('ScaleBox','TitleFit','Canvas'),('TextBlock','CardTitle','TitleFit'),
        ('ScaleBox','DescriptionFit','Canvas'),('ScaleBox','DescriptionDensity','DescriptionFit'),
        ('RichTextBlock','CardDescription','DescriptionDensity')]:widget(FACE,kind,name,parent)
    slot(FACE,'TitleFit',82,43,1116,154)
    slot(FACE,'DescriptionFit',74,1376,1132,113)
    for name in ['TitleFit','DescriptionFit']:
        property_(FACE,name,'Stretch','ScaleToFit');property_(FACE,name,'StretchDirection','DownOnly')
    property_(FACE,'DescriptionDensity','Stretch','UserSpecified')
    property_(FACE,'DescriptionDensity','UserSpecifiedScale',2)
    theme=theme_rows()
    require(W.set_font(FACE,'CardTitle',unreal.WidgetFontInfo(size=int(theme['CardTitleFont']['Value']),
        font_family=theme['UIFont']['Texture'],typeface='Bold',color=theme_color('Text'))),'Review title font')
    property_(FACE,'CardDescription','TextStyleSet','/Game/GuLiStrike/GroundMech/UI/DT_GroundCardTextStyles.DT_GroundCardTextStyles')
    for name in ['CardTitle','CardDescription']:
        property_(FACE,name,'Justification','Center');property_(FACE,name,'Visibility','HitTestInvisible')
    property_(FACE,'CardDescription','AutoWrapText','false');property_(FACE,'CardDescription','WrapTextAt',0)
    property_(FACE,'Canvas','Visibility','HitTestInvisible')
    function(FACE,'SetCopy',[('Title','FText'),('Description','FText')])
    compile_save(FACE)
    g=Graph(FACE,'SetCopy',True)
    g.method('TextBlock','SetText',g.get('CardTitle'),InText=g.arg('Title'))
    g.method('RichTextBlock','SetText',g.get('CardDescription'),InText=g.arg('Description'))
    g.layout();return compile_save(FACE)


def make_selector(ids,catalog,frame):
    ensure_review_pointer_input()
    ensure_widget(SELECTOR)
    widget(SELECTOR,'CanvasPanel','Canvas')
    property_(SELECTOR,'Canvas','Visibility','SelfHitTestInvisible')
    # Gameplay cards own their IconId. Art-only entries carry the same pairing in the icon catalog.
    data_dir=ROOT/'data/Json'
    icons={r['Id']:r['ResourcePath'] for r in json.loads((data_dir/'DT_GuLiStrikeIcons_Icons.json').read_text(encoding='utf-8'))}
    card_icons={r['FrontMaterial']:r['IconId'] for r in json.loads((data_dir/'DT_GuLiStrikeRogueCards_Cards.json').read_text(encoding='utf-8')) if r['IconId']}
    icon_catalog=json.loads((ROOT/'ArtSource/UI/GroundMech/Icons/catalog.json').read_text(encoding='utf-8'))
    artwork_icons={r['artwork_id']:r['id'] for r in icon_catalog['icons'] if 'artwork_id' in r}
    theme=theme_rows()
    blueprint=unreal.load_asset(SELECTOR)
    previous=int(unreal.EditorAssetLibrary.get_metadata_tag(blueprint,'GuLi.CardArtwork.ButtonCount') or '0')
    for index in range(len(ids),previous):
        property_(SELECTOR,f'Artwork{index}','Visibility','Collapsed')
        if W.widget_exists(SELECTOR,f'ArtworkIcon{index}'):
            property_(SELECTOR,f'ArtworkIcon{index}','Visibility','Collapsed')
    for index,ident in enumerate(ids):
        button,label,image=f'Artwork{index}',f'ArtworkLabel{index}',f'ArtworkIcon{index}'
        artwork=catalog['artworks'][ident]
        icon_id=card_icons.get(artwork['Material']) or artwork_icons.get(ident)
        line,col=divmod(index,5)
        count=min(5,len(ids)-line*5)
        center_x=(col-(count-1)/2)*200
        top=18+line*48
        widget(SELECTOR,'Button',button,'Canvas');widget(SELECTOR,'TextBlock',label,button)
        property_(SELECTOR,button,'Visibility','Visible')
        slot(SELECTOR,button,center_x,top,194,42,ax=.5,alignx=.5)
        property_(SELECTOR,button,'IsFocusable','false')
        property_(SELECTOR,label,'Text',artwork['Title'])
        property_(SELECTOR,label,'Justification','Center')
        property_(SELECTOR,label,'Visibility','HitTestInvisible')
        property_(SELECTOR,label,'Padding Left',42 if icon_id else 4)
        require(W.set_font(SELECTOR,label,unreal.WidgetFontInfo(font_family=theme['UIFont']['Texture'],
            typeface='Medium',size=int(theme['SmallFont']['Value']),color=theme_color('Text'))),'Selector font')
        if icon_id:
            widget(SELECTOR,'Image',image,'Canvas')
            slot(SELECTOR,image,center_x-89,top+5,32,32,ax=.5)
            property_(SELECTOR,image,'ZOrder',1)
            property_(SELECTOR,image,'Visibility','HitTestInvisible')
            brush=unreal.WidgetBrushInfo(resource_path=icons[icon_id],image_size='(X=32,Y=32)')
            require(W.set_brush(SELECTOR,image,'Brush',brush),f'Artwork {ident} icon {icon_id}')
        elif W.widget_exists(SELECTOR,image):
            property_(SELECTOR,image,'Visibility','Collapsed')
        property_(SELECTOR,button,'BackgroundColor',theme_color('Panel'))
    variable(SELECTOR,'Director',DIRECTOR)
    variable(SELECTOR,'SelectedMaterial','UMaterialInterface',catalog['artworks'][ids[0]]['Material'])
    initial=catalog['artworks'][ids[0]]
    first_title,first_description=preview_copy(initial)
    initial_ground_frame=catalog['templates'][initial['TemplateId']]['CompositePreset'] in GROUND_MASK_LAYERS
    variable(SELECTOR,'SelectedTitle','FText',first_title)
    variable(SELECTOR,'SelectedDescription','FText',first_description)
    variable(SELECTOR,'GroundFrame','bool',initial_ground_frame)
    function(SELECTOR,'InitializeReview',[('ReviewDirector',DIRECTOR)])
    function(SELECTOR,'SelectArtwork',[('Material','UMaterialInterface'),('Title','FText'),('Description','FText'),('UseGroundFrame','bool'),('TiltDegrees','float')])
    function(SELECTOR,'ApplyReviewFrame')
    for index in range(len(ids)):function(SELECTOR,f'Select{index}')
    compile_save(SELECTOR)
    g=Graph(SELECTOR,'InitializeReview',True)
    g.set('Director',g.arg('ReviewDirector'))
    g.set('SelectedMaterial',initial['Material'])
    g.set('SelectedTitle',g.call('KismetTextLibrary','Conv_StringToText',InString=first_title)['ReturnValue'])
    g.set('SelectedDescription',g.call('KismetTextLibrary','Conv_StringToText',InString=first_description)['ReturnValue'])
    g.set('GroundFrame',initial_ground_frame)
    # WidgetService.bind_event is a logging stub in this project's plugin.
    # Real multicast binding nodes are required; initialization runs once per widget.
    for index in range(len(ids)):bind(g,'Button','OnClicked',g.get(f'Artwork{index}'),f'Select{index}')
    g.invoke('ApplyReviewFrame');g.layout()
    g=Graph(SELECTOR,'SelectArtwork',True)
    for key,arg in [('SelectedMaterial','Material'),('SelectedTitle','Title'),('SelectedDescription','Description'),('GroundFrame','UseGroundFrame')]:g.set(key,g.arg(arg))
    g.call('KismetSystemLibrary','SetDoublePropertyByName',Object=g.get('Director'),PropertyName='MaximumTilt',Value=g.arg('TiltDegrees'))
    g.method(DIRECTOR,'CleanupCards',g.get('Director'))
    g.method(DIRECTOR,'SetPhase',g.get('Director'),NewPhase=6)
    g.method(DIRECTOR,'StartPresentation',g.get('Director'))
    g.invoke('ApplyReviewFrame');g.layout()
    write_apply_review_frame(frame)
    for index,ident in enumerate(ids):
        row=catalog['artworks'][ident];title,description=preview_copy(row)
        g=Graph(SELECTOR,f'Select{index}',True)
        title_pin=g.call('KismetTextLibrary','Conv_StringToText',InString=title)['ReturnValue']
        description_pin=g.call('KismetTextLibrary','Conv_StringToText',InString=description)['ReturnValue']
        g.invoke('SelectArtwork',Material=row['Material'],Title=title_pin,Description=description_pin,
            UseGroundFrame=catalog['templates'][row['TemplateId']]['CompositePreset'] in GROUND_MASK_LAYERS,
            TiltDegrees=review_tilt(catalog,ident))
        g.layout()
    unreal.EditorAssetLibrary.set_metadata_tag(blueprint,'GuLi.CardArtwork.ButtonCount',str(len(ids)))
    result=compile_save(SELECTOR)
    # Compilation can retain a prior generated-class default when a text field becomes empty.
    defaults=unreal.get_default_object(blueprint.generated_class())
    if str(defaults.get_editor_property('SelectedDescription'))!=first_description:
        defaults.set_editor_property('SelectedDescription',first_description)
        require(unreal.EditorAssetLibrary.save_loaded_asset(blueprint,False),'Save selector description')
    return result


def write_apply_review_frame(frame):
    g=Graph(SELECTOR,'ApplyReviewFrame',True)
    card=g.member(DIRECTOR,'Card0',g.get('Director'))
    g.branch(g.call('KismetSystemLibrary','IsValid',Object=card)['ReturnValue'])
    g.method(CARD,'SetArtwork',card,FrontMaterial=g.get('SelectedMaterial'),TextMaterial=frame,HideDecorativeFrame=True)
    ground=g.branch(g.get('GroundFrame'))
    g.tail=ground['else']
    g.method(DIRECTOR,'SetPointerFrameFromCard',g.get('Director'))
    g.tail=ground['then']
    pc=g.call('GameplayStatics','GetPlayerController',PlayerIndex=0)['ReturnValue']
    made=g.call('WidgetBlueprintLibrary','Create',WidgetType=cls(FACE),OwningPlayer=pc)['ReturnValue']
    face=g.cast(FACE,made)
    g.method(FACE,'SetCopy',face,Title=g.get('SelectedTitle'),Description=g.get('SelectedDescription'))
    text=g.member(CARD,'EditableText',card)
    g.method('WidgetComponent','SetDrawSize',text,Size='(X=1280,Y=1556)')
    g.method('SceneComponent','K2_SetRelativeLocation',text,NewLocation=g.vec(0,.8,0),bSweep=False,bTeleport=True)
    g.method('SceneComponent','SetRelativeScale3D',text,NewScale3D=g.vec(48.617/1556,48.617/1556,48.617/1556))
    g.method(DIRECTOR,'SetPointerFrameFromCard',g.get('Director'),Card=card)
    # SetBlendMode is native-only. This reviewed component is resized above and
    # made visible below, rebuilding its render state with the reflected enum.
    g.call('KismetSystemLibrary','SetBytePropertyByName',Object=text,PropertyName='BlendMode',Value=2)
    g.method('WidgetComponent','SetWidget',text,Widget=face)
    g.method('SceneComponent','SetVisibility',text,bNewVisibility=True,bPropagateToChildren=False)
    g.method('WidgetComponent','RequestRenderUpdate',text)
    mesh=g.member(CARD,'CardFace',card)
    g.method('SceneComponent','K2_SetRelativeLocation',mesh,NewLocation=g.vec(0,0,-5),bSweep=False,bTeleport=True)
    g.method('SceneComponent','SetRelativeScale3D',mesh,NewScale3D=g.vec(1.35,1.35,1.35))
    g.method('SceneComponent','SetVisibility',g.member(CARD,'CardText',card),bNewVisibility=False,bPropagateToChildren=False)
    edge=g.member(CARD,'CardSolidEdge',card)
    # Keep the review thickness within the same Ground cut-corner frame.
    g.method('SceneComponent','SetRelativeScale3D',edge,NewScale3D=g.vec(.374,.012,.46))
    g.layout()


def review_tilt(catalog,ident):
    authored=catalog['parameters'][ident].get('Parallax Tilt Tangent')
    return math.degrees(math.atan(authored['ScalarValue'])) if authored else 16.


def review_instructions(ids):
    return ('点击顶部图标选择牌面。鼠标在卡框内移动可查看视差，标题、插画和底栏均可响应。'
            '单击翻面，再次单击退场，点击重播。已绑定卡的说明来自首次获取的源表文案，未绑定的牌面仅展示标题，不发奖励。\nArtworkIds='+','.join(map(str,ids)))


def make_controller():
    lib=unreal.EditorAssetLibrary
    if not lib.does_asset_exist(PC):require(lib.duplicate_asset(BASE_PC,PC),'Review controller')
    if not lib.does_asset_exist(GM):require(lib.duplicate_asset(SYSTEM+'/Blueprints/BP_CardRevealGameMode',GM),'Review game mode')
    variable(PC,'CatalogSelector',SELECTOR)
    compile_save(PC)
    g=Graph(PC,'HandleReplay',True)
    g.method(DIRECTOR,'StartPresentation',g.get('Director'))
    g.method(SELECTOR,'ApplyReviewFrame',g.get('CatalogSelector'));g.layout()
    g=Graph(PC,'EventGraph')
    g.event('ReceiveBeginPlay')
    actor=g.call('GameplayStatics','GetActorOfClass',ActorClass=cls(DIRECTOR))['ReturnValue']
    director=g.cast(DIRECTOR,actor);g.set('Director',director)
    g.call('PlayerController','SetViewTargetWithBlend',NewViewTarget=director,BlendTime=0)
    created=g.call('WidgetBlueprintLibrary','Create',WidgetType=cls(HUD),OwningPlayer=g.get('self'))['ReturnValue']
    hud=g.cast(HUD,created);g.set('PresentationHUD',hud)
    g.method('UserWidget','AddToViewport',hud,ZOrder=0)
    title=g.call('KismetTextLibrary','Conv_StringToText',InString='游骑兵牌面评审')['ReturnValue']
    g.method('TextBlock','SetText',g.member(HUD,'Title',hud),InText=title)
    g.call('WidgetBlueprintLibrary','SetInputMode_GameAndUIEx',PlayerController=g.get('self'),InMouseLockMode='DoNotLock',bHideCursorDuringCapture=False,bFlushInput=True)
    g.call('WidgetBlueprintLibrary','SetFocusToGameViewport')
    sub=g.call('SubsystemBlueprintLibrary','GetLocalPlayerSubSystemFromPlayerController',PlayerController=g.get('self'),Class='/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem')['ReturnValue']
    enhanced=g.cast('EnhancedInputLocalPlayerSubsystem',sub)
    g.method('EnhancedInputLocalPlayerSubsystem','AddMappingContext',enhanced,MappingContext=IMC,Priority=0)
    bind(g,'Button','OnClicked',g.member(HUD,'Replay',hud),'HandleReplay')
    bind(g,'ApplicationLifecycleComponent','ApplicationWillDeactivateDelegate',g.get('ApplicationLifecycle'),'WindowDeactivated')
    bind(g,'ApplicationLifecycleComponent','ApplicationHasReactivatedDelegate',g.get('ApplicationLifecycle'),'WindowReactivated')
    g.method(DIRECTOR,'Initialize',director,PlayerController=g.get('self'),HUD=hud)
    created=g.call('WidgetBlueprintLibrary','Create',WidgetType=cls(SELECTOR),OwningPlayer=g.get('self'))['ReturnValue']
    selector=g.cast(SELECTOR,created);g.set('CatalogSelector',selector)
    g.method('UserWidget','AddToViewport',selector,ZOrder=10)
    g.method(SELECTOR,'InitializeReview',selector,ReviewDirector=director)
    action=Node(g,B.add_input_action_node(PC,'EventGraph',IA,*g.pos()))
    g.tail=action['Started'];g.method(DIRECTOR,'ProcessClick',g.get('Director'))
    g.event('ReceiveEndPlay')
    sub=g.call('SubsystemBlueprintLibrary','GetLocalPlayerSubSystemFromPlayerController',PlayerController=g.get('self'),Class='/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem')['ReturnValue']
    enhanced=g.cast('EnhancedInputLocalPlayerSubsystem',sub)
    g.method('EnhancedInputLocalPlayerSubsystem','RemoveMappingContext',enhanced,MappingContext=IMC)
    g.method('Widget','RemoveFromParent',g.get('CatalogSelector'))
    g.method('Widget','RemoveFromParent',g.get('PresentationHUD'));g.layout()
    result=compile_save(PC)
    require(B.set_property(GM,'PlayerControllerClass',cls(PC)),'Review game mode controller')
    return {'controller':result,'game_mode_compile':compile_save(GM)}


def prepare(ids):
    import card_artwork_pipeline as pipeline
    _,catalog,_=pipeline.load_catalog();ids=pipeline.selected_ids(catalog,ids)
    dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not dirty.intersection({PC,GM,SELECTOR,FACE}), 'Preserve unsaved review assets'
    report=pipeline.prepare_review(ids[0],reload_saved=False)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    director=next(a for a in actors.get_all_level_actors() if a.get_actor_label()==pipeline.REVIEW_ACTOR)
    frame=director.get_editor_property('CardTextMaterials')[0].get_path_name()
    report['face']=make_face();report['selector']=make_selector(ids,catalog,frame);report.update(make_controller())
    director.set_editor_property('MaximumTilt',review_tilt(catalog,ids[0]))
    director.set_editor_property('DirectSelection',False)
    world.get_world_settings().set_editor_property('default_game_mode',unreal.EditorAssetLibrary.load_blueprint_class(GM))
    note=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='CardArtwork_CatalogInstructions')
    note.set_editor_property('text',review_instructions(ids))
    unreal.EditorAssetLibrary.set_metadata_tag(world,'GuLi.CardArtwork.ReviewIds',json.dumps(ids))
    require(unreal.EditorLoadingAndSavingUtils.save_map(world,pipeline.REVIEW_MAP),'Save catalog review')
    del world,director,note
    gc.collect()
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);require(level.load_level(pipeline.REVIEW_MAP),'Reload catalog review')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_world_settings().get_editor_property('default_game_mode').get_path_name()==cls(GM)
    assert json.loads(unreal.EditorAssetLibrary.get_metadata_tag(world,'GuLi.CardArtwork.ReviewIds'))==ids
    report.update(artwork_ids=ids,selector_asset=SELECTOR,game_mode=GM,saved_readback=True,pie_started=False,user_visual_review='pending')
    return report
