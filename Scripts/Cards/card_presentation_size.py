"""Independent face-area, body-thickness and stable-pointer sizing controls.

Executed inside the editor; defaults retain the original tarot presentation.
"""
import math
import unreal
from card_reveal_graph import B, Graph, require, variable, function, compile_save
from card_artwork_config import CARD, DIRECTOR, write_start_function

HUD='/Game/GuLiStrike/CardSystem/RevealDemo/UI/WBP_CardRevealHUD'
EDGE='/Game/GuLiStrike/CardSystem/WarMachineTarot/Materials/M_CelCardEdge'
FACE_Y=0.1885548383
BASE_THICKNESS=2.0*FACE_Y+0.55


def ensure_size_variables():
    for name in ['CardAreaMultiplier','CardThicknessMultiplier']:
        if not B.variable_exists(DIRECTOR,name):
            variable(DIRECTOR,name,'float',1.0,True,'Presentation Tuning')


def write_pointer_function():
    ensure_size_variables()
    g=Graph(DIRECTOR,'UpdatePointer',True)
    size=g.method('PlayerController','GetViewportSize',g.get('Controller'))
    sx=g.native('Conv_IntToDouble',InInt=size['SizeX'])
    sy=g.native('Conv_IntToDouble',InInt=size['SizeY'])
    mx=g.method('PlayerController','GetMousePosition',g.get('Controller'))
    g.set('MouseU',mx['LocationX']/g.native('FMax',A=sx,B=1))
    g.set('MouseV',mx['LocationY']/g.native('FMax',A=sy,B=1))
    focused=g.call('GuLiCardRevealViewportLibrary','IsCardRevealViewportFocused',PlayerController=g.get('Controller'))['ReturnValue']
    permitted=[mx['ReturnValue'],focused,g.get('WindowActive'),g.compare('Greater',sx,0),g.compare('Greater',sy,0)]
    if B.variable_exists(DIRECTOR,'AwaitingConfirmation'):
        permitted.append(g.native('Not_PreBool',A=g.get('AwaitingConfirmation')))
    g.set('MouseValid',g.both(*permitted))
    half=g.get('CameraDistance')*g.native('Tan',A=g.get('HorizontalFOV')*(math.pi/360))
    g.set('ViewWidth',half*2)
    g.set('ViewHeight',g.get('ViewWidth')*g.native('FMax',A=sy,B=1)/g.native('FMax',A=sx,B=1))
    base=g.native('FMin',A=g.get('ViewHeight')*(.5/48.617),B=g.get('ViewWidth')*(.24/32.175))
    width_scale=g.native('Sqrt',A=g.native('FMax',A=g.get('CardAreaMultiplier'),B=.01))
    g.set('CardScale',base*width_scale)
    g.set('HoveredIndex',-1)
    g.layout()


def ensure_edge_component():
    eal=unreal.EditorAssetLibrary
    material=unreal.load_asset(EDGE)
    if not material:
        material=unreal.AssetToolsHelpers.get_asset_tools().create_asset(EDGE.rsplit('/',1)[1],EDGE.rsplit('/',1)[0],unreal.Material,unreal.MaterialFactoryNew())
        material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
        color=unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionConstant3Vector)
        color.constant=unreal.LinearColor(.016,.032,.055,1)
        require(unreal.MaterialEditingLibrary.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR),'Card edge color')
        unreal.MaterialEditingLibrary.recompile_material(material)
        require(eal.save_loaded_asset(material,False),'Save edge material')
    if not B.component_exists(CARD,'CardSolidEdge'):
        bp=unreal.load_asset(CARD)
        sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        lib=unreal.SubobjectDataBlueprintFunctionLibrary
        parent=next(h for h in sub.k2_gather_subobject_data_for_blueprint(bp) if str(lib.get_variable_name(lib.get_data(h)))=='ArtworkCenter')
        handle,reason=sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=parent,new_class=unreal.StaticMeshComponent,blueprint_context=bp))
        require(lib.is_handle_valid(handle),str(reason))
        require(sub.rename_subobject(handle,unreal.Text('CardSolidEdge')),'Name edge component')
    for name,value in {'StaticMesh':'/Engine/BasicShapes/Cube.Cube','OverrideMaterials':'('+EDGE+'.M_CelCardEdge)',
        'RelativeLocation':'(X=0,Y=-0.275,Z=0)','RelativeScale3D':'(X=0.3,Y=0.00925,Z=0.45)',
        'bVisible':'false','CastShadow':'false'}.items():
        require(B.set_component_property(CARD,'CardSolidEdge',name,value),name)
    require(B.set_collision_settings(CARD,'CardSolidEdge','NoCollision','WorldDynamic','NoCollision',{}),'Edge collision')


def write_size_function():
    if not B.function_exists(CARD,'SetPresentationSize'):
        function(CARD,'SetPresentationSize',[('AreaMultiplier','float'),('ThicknessMultiplier','float'),('ShowSolidEdge','bool')])
    g=Graph(CARD,'SetPresentationSize',True)
    # The actor remains uniformly scaled so its rotations and widget text stay
    # rigid. Change only the back offset and the solid body's normal dimension.
    # Divide by sqrt(area) because the actor's uniform scale already adds depth.
    ratio=g.native('FMax',A=g.arg('ThicknessMultiplier'),B=.01)/g.native('Sqrt',A=g.native('FMax',A=g.arg('AreaMultiplier'),B=.01))
    thickness=ratio*BASE_THICKNESS
    back_y=g.math('Subtract',2*FACE_Y,thickness)
    g.method('SceneComponent','K2_SetRelativeLocation',g.get('CardBack'),NewLocation=g.vec(0,back_y,0),bSweep=False,bTeleport=True)
    center_y=g.math('Subtract',FACE_Y,thickness*.5)
    g.method('SceneComponent','K2_SetRelativeLocation',g.get('CardSolidEdge'),NewLocation=g.vec(0,center_y,0),bSweep=False,bTeleport=True)
    g.method('SceneComponent','SetRelativeScale3D',g.get('CardSolidEdge'),NewScale3D=g.vec(.3,g.math('Subtract',thickness,.04)/100,.45))
    g.method('SceneComponent','SetVisibility',g.get('CardSolidEdge'),bNewVisibility=g.arg('ShowSolidEdge'),bPropagateToChildren=False)
    g.layout()


def write_hud_layout():
    if not B.function_exists(HUD,'SetCardArea'):
        function(HUD,'SetCardArea',[('AreaMultiplier','float')])
    g=Graph(HUD,'SetCardArea',True)
    slot=g.call('WidgetLayoutLibrary','SlotAsCanvasSlot',Widget=g.get('Hint'))['ReturnValue']
    choice=g.branch(g.compare('Greater',g.arg('AreaMultiplier'),1.25))
    for tail,y in [(choice['then'],.94),(choice['else'],.84)]:
        g.tail=tail
        g.method('CanvasPanelSlot','SetAnchors',slot,InAnchors=f'(Minimum=(X=0.5,Y={y}),Maximum=(X=0.5,Y={y}))')
    g.layout()


def configure():
    require(not unreal.WidgetService.is_pie_running(),'Stop owned preview before authoring')
    ensure_size_variables()
    ensure_edge_component()
    write_size_function()
    card=compile_save(CARD)
    write_hud_layout()
    hud=compile_save(HUD)
    write_pointer_function()
    write_start_function()
    director=compile_save(DIRECTOR)
    return {'card':card,'hud':hud,'director':director,'baseline_face_separation':BASE_THICKNESS}
