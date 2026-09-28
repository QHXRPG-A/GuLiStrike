"""Shared, scoped artwork configuration for the existing card-reveal flow."""
import unreal
from card_reveal_graph import B, Graph, Node, require, variable, compile_save

ROOT='/Game/GuLiStrike/CardSystem/RevealDemo'
CARD=ROOT+'/Blueprints/BP_ParallaxRevealCard'
DIRECTOR=ROOT+'/Blueprints/BP_CardRevealDirector'


def ensure_artwork_variables():
    for name,suffix in [('CardFrontMaterials',''),('CardTextMaterials','_UI')]:
        if not B.variable_exists(DIRECTOR,name):
            refs=[]
            for title in ['Moon','Star','Tower']:
                n='MI_Card_'+title+suffix
                refs.append("MaterialInstanceConstant'"+ROOT+'/Materials/'+n+'.'+n+"'")
            require(B.add_variable(DIRECTOR,name,'UMaterialInterface','('+','.join(refs)+')',True,'Array'),name)
        require(B.modify_variable(DIRECTOR,name,new_category='Card Artwork',set_instance_editable=1),name)
    if not B.variable_exists(DIRECTOR,'SimpleCardFrame'):
        variable(DIRECTOR,'SimpleCardFrame','bool',False,True,'Card Artwork')


def ensure_artwork_input():
    entry=next(n for n in B.get_nodes_in_graph(CARD,'SetArtwork') if 'FunctionEntry' in n.node_type)
    if 'HideDecorativeFrame' not in [p.pin_name for p in B.get_node_pins(CARD,'SetArtwork',entry.node_id)]:
        require(B.add_function_input(CARD,'SetArtwork','HideDecorativeFrame','bool'),'Add frame option')


def write_artwork_function():
    g=Graph(CARD,'SetArtwork',True)
    for comp,param in [('CardFace','FrontMaterial'),('CardText','TextMaterial')]:
        g.method('PrimitiveComponent','SetMaterial',g.get(comp),ElementIndex=0,Material=g.arg(param))
    visible=g.native('Not_PreBool',A=g.arg('HideDecorativeFrame'))
    for comp in ['CardFrame','CardPictograms','CardVignette']:
        g.method('SceneComponent','SetVisibility',g.get(comp),bNewVisibility=visible,bPropagateToChildren=False)
    # The marketplace UI mesh packs text into small strips. A full-card UI image
    # needs the same UV/geometry as the artwork, slightly in front of its face.
    choice=g.branch(g.arg('HideDecorativeFrame'))
    meshroot='/Game/Assets/card/ParallaxCardMaterial/Geometry/'
    g.method('StaticMeshComponent','SetStaticMesh',g.get('CardText'),NewMesh=meshroot+'S_Card_Base.S_Card_Base')
    g.method('SceneComponent','K2_SetRelativeLocation',g.get('CardText'),NewLocation=g.vec(0,.035,0),bSweep=False,bTeleport=True)
    g.call('Actor','PrestreamTextures',Seconds=5.,bEnableStreaming=True,CinematicTextureGroups=0)
    g.tail=choice['else']
    g.method('StaticMeshComponent','SetStaticMesh',g.get('CardText'),NewMesh=meshroot+'S_Card_UI.S_Card_UI')
    g.method('SceneComponent','K2_SetRelativeLocation',g.get('CardText'),NewLocation=g.vec(0,0,0),bSweep=False,bTeleport=True)
    g.call('Actor','PrestreamTextures',Seconds=5.,bEnableStreaming=True,CinematicTextureGroups=0)
    g.layout()


def write_start_function():
    g=Graph(DIRECTOR,'StartPresentation',True)
    def item(name,index):
        n=Node(g,B.create_node_by_key(DIRECTOR,g.name,'NODE K2Node_GetArrayItem',*g.pos()))
        n.put('Array',g.get(name))
        n.put('Dimension 1',index)
        return n['Output']
    g.branch(g.compare('Equal',g.get('Phase'),6,'Int'))
    g.invoke('CleanupCards')
    g.set('SelectedIndex',-1)
    g.set('HoveredIndex',-1)
    if B.variable_exists(DIRECTOR,'AwaitingConfirmation'):
        g.set('AwaitingConfirmation',False)
    hud=ROOT+'/UI/WBP_CardRevealHUD'
    if B.variable_exists(DIRECTOR,'CardAreaMultiplier') and B.function_exists(hud,'SetCardArea'):
        g.method(hud,'SetCardArea',g.get('PresentationHUD'),AreaMultiplier=g.get('CardAreaMultiplier'))
    for i in range(3):
        transform=g.native('MakeTransform',Location=g.vec(0,-1100,0),Rotation=g.rot(),Scale=g.vec(.001,.001,.001))
        spawn=g.call('GameplayStatics','BeginDeferredActorSpawnFromClass',ActorClass=CARD+'.BP_ParallaxRevealCard_C',
                     SpawnTransform=transform,CollisionHandlingOverride='AlwaysSpawn')
        end=g.call('GameplayStatics','FinishSpawningActor',Actor=spawn['ReturnValue'],SpawnTransform=transform)
        actor=g.cast(CARD,end['ReturnValue'])
        g.set('Card'+str(i),actor)
        g.method('Actor','K2_AttachToActor',actor,ParentActor=g.get('self'),SocketName='None',
                 LocationRule='KeepRelative',RotationRule='KeepRelative',ScaleRule='KeepRelative',bWeldSimulatedBodies=False)
        front=item('CardFrontMaterials',i)
        ui=item('CardTextMaterials',i)
        g.method(CARD,'SetArtwork',actor,FrontMaterial=front,TextMaterial=ui,HideDecorativeFrame=g.get('SimpleCardFrame'))
        if B.variable_exists(DIRECTOR,'CardAreaMultiplier') and B.function_exists(CARD,'SetPresentationSize'):
            g.method(CARD,'SetPresentationSize',actor,AreaMultiplier=g.get('CardAreaMultiplier'),
                     ThicknessMultiplier=g.get('CardThicknessMultiplier'),ShowSolidEdge=g.get('SimpleCardFrame'))
        if B.variable_exists(DIRECTOR,'CardTextDataTable') and B.function_exists(CARD,'SetEditableText'):
            if B.variable_exists(DIRECTOR,'UseLiveCardData'):
                seq=Node(g,B.create_node_by_key(DIRECTOR,g.name,'NODE K2Node_ExecutionSequence',*g.pos()))
                seq.put('execute',g.tail);g.tail=seq['then_0']
                choice=g.branch(g.get('UseLiveCardData'))
                g.call('GuLiRogueCardPresentationLibrary','SetLiveCardText',Card=actor,CardId=item('LiveCardIds',i),Controller=g.get('Controller'))
                g.tail=choice['else']
                g.method(CARD,'SetEditableText',actor,TextTable=g.get('CardTextDataTable'),CardId=item('CardTextRowNames',i))
                g.tail=seq['then_1']
            else:
                g.method(CARD,'SetEditableText',actor,TextTable=g.get('CardTextDataTable'),CardId=item('CardTextRowNames',i))
    g.invoke('SetPhase',NewPhase=0)
    g.layout()


def configure():
    require(not unreal.WidgetService.is_pie_running(),'PIE must be stopped before editing these Blueprints')
    ensure_artwork_variables()
    ensure_artwork_input()
    write_artwork_function()
    card_result=compile_save(CARD)
    write_start_function()
    director_result=compile_save(DIRECTOR)
    cdo=unreal.get_default_object(unreal.load_asset(DIRECTOR).generated_class())
    defaults={k:[m.get_path_name() for m in cdo.get_editor_property(k)] for k in ['CardFrontMaterials','CardTextMaterials']}
    require(all(len(v)==3 for v in defaults.values()),'Original sample defaults must contain three materials')
    return {'card':card_result,'director':director_result,'defaults':defaults,'simple_frame':cdo.get_editor_property('SimpleCardFrame')}
