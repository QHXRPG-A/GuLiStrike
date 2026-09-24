"""Author only /Game/GuLiStrike/Cards/RevealDemo. Run through Scripts/ue_exec.py.

CARD_REVEAL_PHASE may be foundation, card, hud, director, controller, validate.
No phase stops PIE, changes project defaults, or saves unrelated packages.
"""
import json
import math
import sys
import traceback
from pathlib import Path
import unreal

SCRIPT_DIR = Path('D:/UE5.7/test1/Scripts/Cards')
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))
from card_reveal_graph import B, Graph, Node, require, variable, function, compile_save

ROOT = '/Game/GuLiStrike/Cards/RevealDemo'
CARD = ROOT + '/Blueprints/BP_ParallaxRevealCard'
DIRECTOR = ROOT + '/Blueprints/BP_CardRevealDirector'
PC = ROOT + '/Blueprints/BP_CardRevealPlayerController'
GM = ROOT + '/Blueprints/BP_CardRevealGameMode'
HUD = ROOT + '/UI/WBP_CardRevealHUD'
IA = ROOT + '/Input/IA_CardRevealClick'
IMC = ROOT + '/Input/IMC_CardReveal'
SOURCE = '/Game/Assets/card/ParallaxCardMaterial'
EVIDENCE = Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo')
ASSETS = [CARD, HUD, DIRECTOR, PC, GM]
REPORT = {}


def asset_class(path):
    return path + '.' + path.rsplit('/', 1)[-1] + '_C'


def blueprint(path, parent):
    if not B.blueprint_exists(path):
        directory, name = path.rsplit('/', 1)
        require(B.create_blueprint(name, parent, directory), f'Create {path}')
    return unreal.load_asset(path)


def component(bp, kind, name, parent=''):
    if not B.component_exists(bp, name):
        # Native subobject authoring preserves same-blueprint parent references
        # correctly across save/reload (VibeUE AddComponent 4.0 writes inherited metadata).
        asset = unreal.load_asset(bp)
        sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        lib = unreal.SubobjectDataBlueprintFunctionLibrary
        handles = sub.k2_gather_subobject_data_for_blueprint(asset)
        owner = next(h for h in handles if str(lib.get_variable_name(lib.get_data(h))) == parent) if parent else handles[0]
        handle, reason = sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=owner,
            new_class=getattr(unreal, kind), blueprint_context=asset))
        require(lib.is_handle_valid(handle), f'{name}: {reason}')
        require(sub.rename_subobject(handle, unreal.Text(name)), f'Rename {name}')


def prop(bp, component_name, name, value):
    require(B.set_component_property(bp, component_name, name, str(value)), f'{bp}.{component_name}.{name}')


def owned_material(name, blend=unreal.BlendMode.BLEND_OPAQUE, unlit=True):
    path = ROOT + '/Materials/' + name
    m = unreal.load_asset(path)
    if not m:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ROOT + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    require(m, name)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
    m.set_editor_property('blend_mode', blend)
    m.set_editor_property('two_sided', False)
    if unlit:
        m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    return m


def expr(m, cls, x=0, y=0, **values):
    n = unreal.MaterialEditingLibrary.create_material_expression(m, cls, x, y)
    for k, v in values.items():
        n.set_editor_property(k, v)
    return n


def material_finish(m):
    unreal.MaterialEditingLibrary.recompile_material(m)
    require(unreal.EditorAssetLibrary.save_loaded_asset(m, False), 'Save material')


def materials():
    mel = unreal.MaterialEditingLibrary
    m = owned_material('M_CardBack')
    t = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, parameter_name='CardBackTexture',
             texture=unreal.load_asset('/Game/Assets/card/RewardCards/Textures/T_CardBG_00'))
    mel.connect_material_property(t, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    material_finish(m)

    m = owned_material('M_DemoBackdrop')
    c = expr(m, unreal.MaterialExpressionVectorParameter, parameter_name='BackgroundColor',
             default_value=unreal.LinearColor(0.012, 0.014, 0.019, 1))
    mel.connect_material_property(c, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    material_finish(m)

    m = owned_material('M_CardConfirmFlash', unreal.BlendMode.BLEND_ADDITIVE)
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -800, 0)
    progress = expr(m, unreal.MaterialExpressionScalarParameter, -800, 150, parameter_name='Progress', default_value=0.0)
    strength = expr(m, unreal.MaterialExpressionScalarParameter, -800, 300, parameter_name='Intensity', default_value=8.0)
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -300, 360, parameter_name='FlashColor',
                default_value=unreal.LinearColor(1, 0.84, 0.55, 1))
    custom = expr(m, unreal.MaterialExpressionCustom, -400, 0,
                  output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                  description='Local confirmation: fast rise, small expansion, soft decay',
                  code='''float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float t = saturate(Progress);
float envelope = smoothstep(0.0, 0.12, t) * (1.0 - smoothstep(0.22, 1.0, t));
float radius = lerp(0.18, 0.70, t);
float core = pow(saturate(1.0 - r / radius), 3.0);
float glow = exp(-r*r / max(0.006, radius*radius*0.16)) * 0.4;
float rays = pow(saturate(1.0 - abs(p.x*p.y)*90.0), 6.0) * pow(saturate(1.0-r),4.0) * 0.2;
return (core + glow + rays) * envelope * Intensity;''')
    custom_inputs = []
    for name in ['UV', 'Progress', 'Intensity']:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        custom_inputs.append(item)
    custom.set_editor_property('inputs', custom_inputs)
    for a, pin, output in [(uv, 'UV', ''), (progress, 'Progress', ''), (strength, 'Intensity', '')]:
        require(mel.connect_material_expressions(a, output, custom, pin), f'Flash input {pin}')
    mul = expr(m, unreal.MaterialExpressionMultiply, 0, 0)
    mel.connect_material_expressions(custom, '', mul, 'A')
    mel.connect_material_expressions(tint, 'RGB', mul, 'B')
    mel.connect_material_property(mul, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = expr(m, unreal.MaterialExpressionConstant, 0, 200, r=1.0)
    mel.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    material_finish(m)

    # Copy editable instances, preserving the marketplace parent graph and animation parameters.
    for title in ['Moon', 'Star', 'Tower']:
        for suffix in ['', '_UI']:
            name = f'MI_Card_{title}{suffix}'
            src = SOURCE + '/Materials/' + name
            dst = ROOT + '/Materials/' + name
            if not unreal.EditorAssetLibrary.does_asset_exist(dst):
                require(unreal.EditorAssetLibrary.duplicate_asset(src, dst), f'Copy {src}')
            require(unreal.EditorAssetLibrary.save_asset(dst, False), f'Save {dst}')


def foundation():
    for p, parent in [(CARD, 'Actor'), (HUD, 'UserWidget'), (DIRECTOR, 'Actor'), (PC, 'PlayerController'), (GM, 'GameModeBase')]:
        blueprint(p, parent)
    # Reuse existing component templates. Removing and immediately recreating them can
    # collide with a live generated class's still-referenced *_GEN_VARIABLE templates.
    materials()
    component(CARD, 'SceneComponent', 'PositionRoot')
    require(B.set_root_component(CARD, 'PositionRoot'), 'Card position root')
    component(CARD, 'SceneComponent', 'HoverPivot', 'PositionRoot')
    component(CARD, 'SceneComponent', 'FlipPivot', 'HoverPivot')
    component(CARD, 'SceneComponent', 'ArtworkCenter', 'FlipPivot')
    prop(CARD, 'ArtworkCenter', 'RelativeLocation', '(X=0,Y=-0.189,Z=-0.8745)')
    for name, mesh in [('CardFace', 'S_Card_Base'), ('CardText', 'S_Card_UI'), ('CardFrame', 'S_Deck_Base'),
                       ('CardPictograms', 'S_Deck_Pictograms'), ('CardVignette', 'S_Deck_Vignette'), ('CardBack', 'S_Card_Base')]:
        component(CARD, 'StaticMeshComponent', name, 'ArtworkCenter')
        prop(CARD, name, 'StaticMesh', SOURCE + '/Geometry/' + mesh + '.' + mesh)
        prop(CARD, name, 'CastShadow', 'false')
        require(B.set_collision_settings(CARD, name, 'NoCollision', 'WorldDynamic', 'NoCollision', {}), f'Collision {name}')
    prop(CARD, 'CardFace', 'OverrideMaterials', f'({ROOT}/Materials/MI_Card_Moon.MI_Card_Moon)')
    prop(CARD, 'CardText', 'OverrideMaterials', f'({ROOT}/Materials/MI_Card_Moon_UI.MI_Card_Moon_UI)')
    prop(CARD, 'CardBack', 'OverrideMaterials', f'({ROOT}/Materials/M_CardBack.M_CardBack)')
    prop(CARD, 'CardBack', 'RelativeRotation', '(Pitch=0,Yaw=180,Roll=0)')
    prop(CARD, 'CardBack', 'RelativeLocation', '(X=0,Y=-0.55,Z=0)')
    component(CARD, 'StaticMeshComponent', 'ConfirmationFlash', 'PositionRoot')
    prop(CARD, 'ConfirmationFlash', 'StaticMesh', '/Engine/BasicShapes/Plane.Plane')
    prop(CARD, 'ConfirmationFlash', 'RelativeRotation', '(Pitch=0,Yaw=0,Roll=90)')
    prop(CARD, 'ConfirmationFlash', 'RelativeLocation', '(X=0,Y=5,Z=0)')
    prop(CARD, 'ConfirmationFlash', 'RelativeScale3D', '(X=0.65,Y=0.65,Z=0.65)')
    prop(CARD, 'ConfirmationFlash', 'OverrideMaterials', f'({ROOT}/Materials/M_CardConfirmFlash.M_CardConfirmFlash)')
    prop(CARD, 'ConfirmationFlash', 'CastShadow', 'false')
    require(B.set_collision_settings(CARD, 'ConfirmationFlash', 'NoCollision', 'WorldDynamic', 'NoCollision', {}), 'Flash collision')
    for name, kind, default in [('CurrentYaw', 'float', 0), ('CurrentRoll', 'float', 0),
                                ('FlashMID', 'UMaterialInstanceDynamic', '')]:
        variable(CARD, name, kind, default)
    function(CARD, 'SetArtwork', [('FrontMaterial', 'UMaterialInterface'), ('TextMaterial', 'UMaterialInterface')])
    function(CARD, 'ApplyFrame', [(n, 'float') for n in ['DisplayX', 'DisplayScale', 'FlightAlpha', 'FarDistance', 'FlipAngle',
                                                     'HoverX', 'HoverY', 'MaxTilt', 'HoverSpeed', 'DeltaSeconds', 'FlashProgress', 'FlashIntensity']] + [('Visible', 'bool')])

    component(DIRECTOR, 'SceneComponent', 'PresentationRoot')
    require(B.set_root_component(DIRECTOR, 'PresentationRoot'), 'Director root')
    component(DIRECTOR, 'CameraComponent', 'PresentationCamera', 'PresentationRoot')
    prop(DIRECTOR, 'PresentationCamera', 'RelativeLocation', '(X=0,Y=140,Z=0)')
    prop(DIRECTOR, 'PresentationCamera', 'RelativeRotation', '(Pitch=0,Yaw=-90,Roll=0)')
    prop(DIRECTOR, 'PresentationCamera', 'FieldOfView', '50')
    prop(DIRECTOR, 'PresentationCamera', 'bConstrainAspectRatio', 'false')
    prop(DIRECTOR, 'PresentationCamera', 'bOverrideAspectRatioAxisConstraint', 'true')
    prop(DIRECTOR, 'PresentationCamera', 'AspectRatioAxisConstraint', 'AspectRatio_MaintainXFOV')
    prop(DIRECTOR, 'PresentationCamera', 'PostProcessSettings',
         '(bOverride_AutoExposureMethod=True,AutoExposureMethod=AEM_Manual,'
         'bOverride_AutoExposureApplyPhysicalCameraExposure=True,AutoExposureApplyPhysicalCameraExposure=False,'
         'bOverride_AutoExposureBias=True,AutoExposureBias=0,'
         'bOverride_MotionBlurAmount=True,MotionBlurAmount=0,'
         'bOverride_BloomIntensity=True,BloomIntensity=0.25,'
         'bOverride_LensFlareIntensity=True,LensFlareIntensity=0,'
         'bOverride_LocalExposureHighlightContrastScale=True,LocalExposureHighlightContrastScale=1,'
         'bOverride_LocalExposureShadowContrastScale=True,LocalExposureShadowContrastScale=1,'
         'bOverride_VignetteIntensity=True,VignetteIntensity=0.15)')
    component(DIRECTOR, 'StaticMeshComponent', 'Backdrop', 'PresentationRoot')
    prop(DIRECTOR, 'Backdrop', 'StaticMesh', '/Engine/BasicShapes/Plane.Plane')
    prop(DIRECTOR, 'Backdrop', 'RelativeLocation', '(X=0,Y=-2500,Z=0)')
    prop(DIRECTOR, 'Backdrop', 'RelativeRotation', '(Pitch=0,Yaw=0,Roll=90)')
    prop(DIRECTOR, 'Backdrop', 'RelativeScale3D', '(X=60,Y=60,Z=60)')
    prop(DIRECTOR, 'Backdrop', 'OverrideMaterials', f'({ROOT}/Materials/M_DemoBackdrop.M_DemoBackdrop)')
    prop(DIRECTOR, 'Backdrop', 'CastShadow', 'false')
    require(B.set_collision_settings(DIRECTOR, 'Backdrop', 'NoCollision', 'WorldStatic', 'NoCollision', {}), 'Backdrop collision')
    for name, default in [('EntryDuration', .5), ('FlipDuration', .45), ('FlashDuration', .25), ('ExitDuration', .5),
                          ('MaximumTilt', 12), ('HoverInterpSpeed', 12), ('FlashIntensity', 8), ('CameraDistance', 140),
                          ('HorizontalFOV', 50), ('FarDistance', 1100)]:
        variable(DIRECTOR, name, 'float', default, True, 'Presentation Tuning')
    for name, kind, default in [('Phase', 'int', 6), ('PhaseTime', 'float', 0), ('SelectedIndex', 'int', -1),
                                ('HoveredIndex', 'int', -1), ('MouseU', 'float', 0), ('MouseV', 'float', 0),
                                ('MouseValid', 'bool', False), ('WindowActive', 'bool', True),
                                ('ViewWidth', 'float', 1), ('ViewHeight', 'float', 1), ('CardScale', 'float', 1),
                                ('Controller', 'APlayerController', ''), ('PresentationHUD', HUD, '')]:
        variable(DIRECTOR, name, kind, default)
    for i in range(3):
        variable(DIRECTOR, f'Card{i}', CARD, '')
    signatures = {
        'Initialize': [('PlayerController', 'APlayerController'), ('HUD', HUD)],
        'StartPresentation': [], 'CleanupCards': [], 'UpdatePointer': [], 'ProcessClick': [], 'ActivateHit': [('HitIndex', 'int')],
        'SetPhase': [('NewPhase', 'int')], 'TickPresentation': [('DeltaSeconds', 'float')],
        'UpdateCard': [('Card', CARD), ('Index', 'int'), ('DeltaSeconds', 'float')],
        'WindowDeactivated': [], 'WindowReactivated': []}
    for name, inputs in signatures.items():
        function(DIRECTOR, name, inputs)
    if not B.variable_exists(DIRECTOR, 'OnPresentationFinished'):
        require(B.add_event_dispatcher(DIRECTOR, 'OnPresentationFinished'), 'Finish dispatcher')
        require(B.add_event_dispatcher_parameter(DIRECTOR, 'OnPresentationFinished', 'SelectedIndex', 'int'), 'Finish index')

    function(HUD, 'SetStage', [('Stage', 'int')])
    function(HUD, 'RequestReplay')
    if not B.variable_exists(HUD, 'OnReplayRequested'):
        require(B.add_event_dispatcher(HUD, 'OnReplayRequested'), 'Replay dispatcher')
    for name in ['HandleReplay', 'WindowDeactivated', 'WindowReactivated']:
        function(PC, name)
    variable(PC, 'Director', DIRECTOR)
    variable(PC, 'PresentationHUD', HUD)
    component(PC, 'ApplicationLifecycleComponent', 'ApplicationLifecycle')
    if not unreal.EditorAssetLibrary.does_asset_exist(IA):
        unreal.InputService.create_action('IA_CardRevealClick', ROOT + '/Input', 'Boolean')
    if not unreal.EditorAssetLibrary.does_asset_exist(IMC):
        unreal.InputService.create_mapping_context('IMC_CardReveal', ROOT + '/Input', 0)
        require(unreal.InputService.add_key_mapping(IMC, IA, 'LeftMouseButton'), 'Left mouse mapping')
    for p in [IA, IMC]:
        require(unreal.EditorAssetLibrary.save_asset(p, False), p)
    for p in ASSETS:
        REPORT[p] = compile_save(p)


def card_graphs():
    g = Graph(CARD, 'EventGraph')
    g.event('ReceiveBeginPlay')
    mid = g.method('PrimitiveComponent', 'CreateDynamicMaterialInstance', g.get('ConfirmationFlash'), ElementIndex=0)
    g.set('FlashMID', mid['ReturnValue'])
    g.layout()
    g = Graph(CARD, 'SetArtwork', True)
    for comp, param in [('CardFace', 'FrontMaterial'), ('CardText', 'TextMaterial')]:
        g.method('PrimitiveComponent', 'SetMaterial', g.get(comp), ElementIndex=0, Material=g.arg(param))
    g.call('Actor', 'PrestreamTextures', Seconds=5.0, bEnableStreaming=True, CinematicTextureGroups=0)
    g.layout()
    g = Graph(CARD, 'ApplyFrame', True)
    a = g.arg('FlightAlpha')
    loc = g.vec(g.arg('DisplayX') * a, g.math('Subtract', a, 1) * g.arg('FarDistance'), 0)
    g.call('Actor', 'K2_SetActorLocation', NewLocation=loc, bSweep=False, bTeleport=True)
    size = g.arg('DisplayScale') * g.native('Lerp', A=.015, B=1, Alpha=a)
    g.call('Actor', 'SetActorScale3D', NewScale3D=g.vec(size, size, size))
    g.call('Actor', 'SetActorHiddenInGame', bNewHidden=g.native('Not_PreBool', A=g.arg('Visible')))
    for current, target in [('CurrentYaw', g.arg('HoverX') * g.arg('MaxTilt') * -1),
                            ('CurrentRoll', g.arg('HoverY') * g.arg('MaxTilt'))]:
        g.set(current, g.native('FInterpTo', Current=g.get(current), Target=target,
                               DeltaTime=g.arg('DeltaSeconds'), InterpSpeed=g.arg('HoverSpeed')))
    g.method('SceneComponent', 'K2_SetRelativeRotation', g.get('HoverPivot'),
             NewRotation=g.rot(roll=g.get('CurrentRoll'), yaw=g.get('CurrentYaw')), bSweep=False, bTeleport=True)
    g.method('SceneComponent', 'K2_SetRelativeRotation', g.get('FlipPivot'),
             NewRotation=g.rot(yaw=g.arg('FlipAngle')), bSweep=False, bTeleport=True)
    for name, value in [('Progress', g.arg('FlashProgress')), ('Intensity', g.arg('FlashIntensity'))]:
        g.method('MaterialInstanceDynamic', 'SetScalarParameterValue', g.get('FlashMID'), ParameterName=name, Value=value)
    g.layout()
    REPORT[CARD] = compile_save(CARD)


def hud_graphs():
    W = unreal.WidgetService
    for kind, name, parent in [('CanvasPanel', 'Canvas', ''), ('TextBlock', 'Title', 'Canvas'),
                              ('TextBlock', 'Hint', 'Canvas'), ('Button', 'Replay', 'Canvas'), ('TextBlock', 'ReplayText', 'Replay')]:
        if not W.widget_exists(HUD, name):
            require(W.add_component(HUD, kind, name, parent, True).success, f'Widget {name}')
    for name, y, width, height in [('Title', .12, 740, 48), ('Hint', .84, 900, 44), ('Replay', .56, 200, 52)]:
        for key, value in {'Anchor Min X': .5, 'Anchor Max X': .5, 'Anchor Min Y': y, 'Anchor Max Y': y,
                           'Alignment X': .5, 'Alignment Y': .5, 'Position X': 0, 'Position Y': 0,
                           'Size X': width, 'Size Y': height}.items():
            require(W.set_property(HUD, name, key, str(value)), f'{name} {key}')
    for name, text, size in [('Title', '命运之选', 26), ('Hint', '卡牌正在入场…', 18), ('ReplayText', '重新播放', 20)]:
        require(W.set_property(HUD, name, 'Text', text), name)
        require(W.set_property(HUD, name, 'Justification', 'Center'), name)
        require(W.set_property(HUD, name, 'Visibility', 'HitTestInvisible'), name)
        font = unreal.WidgetFontInfo(size=size, color='(R=0.82,G=0.79,B=0.69,A=1)')
        require(W.set_font(HUD, name, font), name)
    require(W.set_property(HUD, 'Canvas', 'Visibility', 'SelfHitTestInvisible'), 'Canvas hit test')
    require(W.set_property(HUD, 'Replay', 'Visibility', 'Collapsed'), 'Replay initial')
    require(W.set_property(HUD, 'Replay', 'IsFocusable', 'false'), 'Replay focus')
    compile_save(HUD)  # Generate the widget member properties before placing getters.
    g = Graph(HUD, 'SetStage', True)
    hints = ['卡牌正在入场…', '移动鼠标轻压牌面 · 单击选择', '正在翻开卡背…', '已锁定 · 再次单击所选卡牌确认', '命运已定', '卡牌正在离场…', '本次展示结束']
    for i, hint in enumerate(hints):
        b = g.branch(g.compare('Equal', g.arg('Stage'), i, 'Int'))
        text_value = g.call('KismetTextLibrary', 'Conv_StringToText', InString=hint)['ReturnValue']
        g.method('TextBlock', 'SetText', g.get('Hint'), InText=text_value)
        g.method('Widget', 'SetVisibility', g.get('Replay'), InVisibility='Visible' if i == 6 else 'Collapsed')
        g.method('Widget', 'SetIsEnabled', g.get('Replay'), bInIsEnabled=(i == 6))
        g.tail = b['else']
    g.layout()
    g = Graph(HUD, 'RequestReplay', True)
    n = Node(g, B.add_call_delegate_node(HUD, g.name, 'OnReplayRequested', *g.pos()))
    n.put('execute', g.tail)
    g.layout()
    require(W.bind_event(HUD, 'Replay', 'OnClicked', 'RequestReplay'), 'Replay button binding')
    REPORT[HUD] = compile_save(HUD)


def director_graphs():
    if not B.function_exists(DIRECTOR, 'ActivateHit'):
        function(DIRECTOR, 'ActivateHit', [('HitIndex', 'int')])
        compile_save(DIRECTOR)
    g = Graph(DIRECTOR, 'Initialize', True)
    g.set('Controller', g.arg('PlayerController'))
    g.set('PresentationHUD', g.arg('HUD'))
    g.method('CameraComponent', 'SetFieldOfView', g.get('PresentationCamera'), InFieldOfView=g.get('HorizontalFOV'))
    g.method('SceneComponent', 'K2_SetRelativeLocation', g.get('PresentationCamera'),
             NewLocation=g.vec(0, g.get('CameraDistance'), 0), bSweep=False, bTeleport=True)
    g.invoke('StartPresentation')
    g.layout()
    g = Graph(DIRECTOR, 'SetPhase', True)
    g.set('Phase', g.arg('NewPhase'))
    g.set('PhaseTime', 0)
    g.method(HUD, 'SetStage', g.get('PresentationHUD'), Stage=g.arg('NewPhase'))
    g.layout()
    # Sequence keeps all three cleanup branches independent when one reference is already invalid.
    g = Graph(DIRECTOR, 'CleanupCards', True)
    sequence = Node(g, B.create_node_by_key(DIRECTOR, g.name, 'NODE K2Node_ExecutionSequence', *g.pos()))
    sequence.put('execute', g.tail)
    # Default Sequence has two outputs: first card, then a second Sequence for cards 1 and 2.
    second = Node(g, B.create_node_by_key(DIRECTOR, g.name, 'NODE K2Node_ExecutionSequence', *g.pos()))
    second.put('execute', sequence['then_1'])
    for i, start in enumerate([sequence['then_0'], second['then_0'], second['then_1']]):
        g.tail = start
        valid = g.call('KismetSystemLibrary', 'IsValid', Object=g.get(f'Card{i}'))['ReturnValue']
        g.branch(valid)
        g.method('Actor', 'K2_DestroyActor', g.get(f'Card{i}'))
    g.layout()
    g = Graph(DIRECTOR, 'StartPresentation', True)
    g.branch(g.compare('Equal', g.get('Phase'), 6, 'Int'))
    g.invoke('CleanupCards')
    g.set('SelectedIndex', -1)
    g.set('HoveredIndex', -1)
    for i, title in enumerate(['Moon', 'Star', 'Tower']):
        transform = g.native('MakeTransform', Location=g.vec(0, -1100, 0), Rotation=g.rot(), Scale=g.vec(.001, .001, .001))
        spawn = g.call('GameplayStatics', 'BeginDeferredActorSpawnFromClass', ActorClass=asset_class(CARD), SpawnTransform=transform,
                       CollisionHandlingOverride='AlwaysSpawn')
        end = g.call('GameplayStatics', 'FinishSpawningActor', Actor=spawn['ReturnValue'], SpawnTransform=transform)
        actor = g.cast(CARD, end['ReturnValue'])
        g.set(f'Card{i}', actor)
        g.method(CARD, 'SetArtwork', actor, FrontMaterial=ROOT + f'/Materials/MI_Card_{title}',
                 TextMaterial=ROOT + f'/Materials/MI_Card_{title}_UI')
    g.invoke('SetPhase', NewPhase=0)
    g.layout()
    g = Graph(DIRECTOR, 'UpdatePointer', True)
    size = g.method('PlayerController', 'GetViewportSize', g.get('Controller'))
    sx = g.native('Conv_IntToDouble', InInt=size['SizeX'])
    sy = g.native('Conv_IntToDouble', InInt=size['SizeY'])
    mx = g.method('PlayerController', 'GetMousePosition', g.get('Controller'))
    g.set('MouseU', mx['LocationX'] / g.native('FMax', A=sx, B=1))
    g.set('MouseV', mx['LocationY'] / g.native('FMax', A=sy, B=1))
    focused = g.call('GuLiCardRevealViewportLibrary', 'IsCardRevealViewportFocused', PlayerController=g.get('Controller'))['ReturnValue']
    g.set('MouseValid', g.both(mx['ReturnValue'], focused, g.get('WindowActive'), g.compare('Greater', sx, 0), g.compare('Greater', sy, 0)))
    half = g.get('CameraDistance') * g.native('Tan', A=g.get('HorizontalFOV') * (math.pi / 360))
    g.set('ViewWidth', half * 2)
    g.set('ViewHeight', g.get('ViewWidth') * sy / g.native('FMax', A=sx, B=1))
    g.set('CardScale', g.native('FMin', A=g.get('ViewHeight') * (.5 / 48.617), B=g.get('ViewWidth') * (.24 / 32.175)))
    g.set('HoveredIndex', -1)
    g.layout()
    g = Graph(DIRECTOR, 'UpdateCard', True)
    phase = g.get('Phase')
    t = g.get('PhaseTime')
    index = g.native('Conv_IntToDouble', InInt=g.arg('Index'))
    selected = g.compare('Equal', g.get('SelectedIndex'), g.arg('Index'), 'Int')
    entering = g.compare('Equal', phase, 0, 'Int')
    exiting = g.compare('Equal', phase, 5, 'Int')
    local = t - index * g.get('EntryDuration')
    entry_alpha = g.ease(local / g.native('FMax', A=g.get('EntryDuration'), B=.01))
    exit_alpha = g.math('Subtract', 1, g.ease(t / g.native('FMax', A=g.get('ExitDuration'), B=.01), 'EaseIn'))
    flight = g.select(entry_alpha, g.select(exit_alpha, 1, exiting), entering)
    center = index * .3 + .2
    x = (g.get('MouseU') - center) / (g.get('CardScale') * 16.0875 / g.get('ViewWidth'))
    y = (g.get('MouseV') - .5) / (g.get('CardScale') * 24.3085 / g.get('ViewHeight'))
    permitted = g.either(g.compare('Equal', phase, 1, 'Int'), g.both(g.compare('Equal', phase, 3, 'Int'), selected))
    hit = g.both(permitted, g.get('MouseValid'),
                 g.compare('LessEqual', g.native('Abs', A=x), 1), g.compare('LessEqual', g.native('Abs', A=y), 1))
    # Record the stable rectangle hit; ApplyFrame always runs, even when this card is not hovered.
    old_tail = g.tail
    branch = g.branch(hit)
    g.set('HoveredIndex', g.arg('Index'))
    hit_tail = g.tail
    # Common updates are dispatched through a Sequence, independent of the hit branch.
    B.disconnect_pin(DIRECTOR, g.name, branch.guid, 'execute')
    seq = Node(g, B.create_node_by_key(DIRECTOR, g.name, 'NODE K2Node_ExecutionSequence', *g.pos()))
    seq.put('execute', old_tail)
    branch.put('execute', seq['then_0'])
    g.tail = seq['then_1']
    flipalpha = g.select(g.ease(t / g.native('FMax', A=g.get('FlipDuration'), B=.01), 'EaseInOut'),
                         g.select(1, 0, g.compare('GreaterEqual', phase, 3, 'Int')),
                         g.compare('Equal', phase, 2, 'Int'))
    flash = g.select(g.clamp(t / g.native('FMax', A=g.get('FlashDuration'), B=.01)), 0,
                     g.both(selected, g.compare('Equal', phase, 4, 'Int')))
    visible = g.both(g.compare('Less', phase, 6, 'Int'), g.either(g.native('Not_PreBool', A=entering), g.compare('GreaterEqual', local, 0)))
    g.method(CARD, 'ApplyFrame', g.arg('Card'), DisplayX=(center - .5) * g.get('ViewWidth'),
             DisplayScale=g.get('CardScale'), FlightAlpha=flight, FarDistance=g.get('FarDistance'),
             FlipAngle=g.select(flipalpha * 180, 0, selected), HoverX=g.select(g.clamp(x, -1, 1), 0, hit),
             HoverY=g.select(g.clamp(y, -1, 1), 0, hit), MaxTilt=g.get('MaximumTilt'),
             HoverSpeed=g.get('HoverInterpSpeed'), DeltaSeconds=g.arg('DeltaSeconds'),
             FlashProgress=flash, FlashIntensity=g.get('FlashIntensity'), Visible=visible)
    g.layout()
    g = Graph(DIRECTOR, 'ProcessClick', True)
    g.branch(g.either(g.compare('Equal', g.get('Phase'), 1, 'Int'), g.compare('Equal', g.get('Phase'), 3, 'Int')))
    g.invoke('UpdatePointer')
    for i in range(3):
        g.invoke('UpdateCard', Card=g.get(f'Card{i}'), Index=i, DeltaSeconds=0)
    g.invoke('ActivateHit', HitIndex=g.get('HoveredIndex'))
    g.layout()
    g = Graph(DIRECTOR, 'ActivateHit', True)
    g.branch(g.both(g.compare('GreaterEqual', g.arg('HitIndex'), 0, 'Int'), g.compare('Less', g.arg('HitIndex'), 3, 'Int')))
    selection = g.branch(g.compare('Equal', g.get('Phase'), 1, 'Int'))
    g.set('SelectedIndex', g.arg('HitIndex'))
    g.invoke('SetPhase', NewPhase=2)
    g.tail = selection['else']
    g.branch(g.both(g.compare('Equal', g.get('Phase'), 3, 'Int'), g.compare('Equal', g.arg('HitIndex'), g.get('SelectedIndex'), 'Int')))
    g.invoke('SetPhase', NewPhase=4)
    g.layout()
    g = Graph(DIRECTOR, 'TickPresentation', True)
    g.branch(g.compare('Less', g.get('Phase'), 6, 'Int'))
    g.set('PhaseTime', g.get('PhaseTime') + g.arg('DeltaSeconds'))
    g.invoke('UpdatePointer')
    for i in range(3):
        g.invoke('UpdateCard', Card=g.get(f'Card{i}'), Index=i, DeltaSeconds=g.arg('DeltaSeconds'))
    for phase, duration, following in [(0, g.get('EntryDuration') * 3, 1), (2, g.get('FlipDuration'), 3),
                                        (4, g.get('FlashDuration'), 5), (5, g.get('ExitDuration'), 6)]:
        b = g.branch(g.both(g.compare('Equal', g.get('Phase'), phase, 'Int'), g.compare('GreaterEqual', g.get('PhaseTime'), duration)))
        if following == 6:
            g.invoke('CleanupCards')
        g.invoke('SetPhase', NewPhase=following)
        if following == 6:
            n = Node(g, B.add_call_delegate_node(DIRECTOR, g.name, 'OnPresentationFinished', *g.pos()))
            n.put('execute', g.tail)
            n.put('SelectedIndex', g.get('SelectedIndex'))
        g.tail = b['else']
    g.layout()
    g = Graph(DIRECTOR, 'EventGraph')
    ev = g.event('ReceiveTick')
    g.invoke('TickPresentation', DeltaSeconds=ev['DeltaSeconds'])
    g.layout()
    for name, active in [('WindowDeactivated', False), ('WindowReactivated', True)]:
        g = Graph(DIRECTOR, name, True)
        g.set('WindowActive', active)
        g.layout()
    REPORT[DIRECTOR] = compile_save(DIRECTOR)


def bind(g, owner, dispatcher, target, callback):
    node = Node(g, B.add_delegate_bind_node(g.bp, g.name, owner, dispatcher, *g.pos()))
    node.put('self', target)
    node.put('execute', g.tail)
    create = Node(g, B.add_create_delegate_node(g.bp, g.name, callback, *g.pos()))
    node.put('Delegate', create['OutputDelegate'])
    g.tail = node['then']


def controller_graphs():
    for name in ['HandleReplay', 'WindowDeactivated', 'WindowReactivated']:
        g = Graph(PC, name, True)
        g.method(DIRECTOR, 'StartPresentation' if name == 'HandleReplay' else name, g.get('Director'))
        g.layout()
    g = Graph(PC, 'EventGraph')
    g.event('ReceiveBeginPlay')
    actor = g.call('GameplayStatics', 'GetActorOfClass', ActorClass=asset_class(DIRECTOR))['ReturnValue']
    director = g.cast(DIRECTOR, actor)
    g.set('Director', director)
    g.call('PlayerController', 'SetViewTargetWithBlend', NewViewTarget=director, BlendTime=0)
    # Native Create expands to the same UserWidget construction as the familiar Create Widget node.
    widget = g.call('WidgetBlueprintLibrary', 'Create', WidgetType=asset_class(HUD), OwningPlayer=g.get('self'))['ReturnValue']
    hud = g.cast(HUD, widget)
    g.set('PresentationHUD', hud)
    g.method('UserWidget', 'AddToViewport', hud, ZOrder=0)
    g.call('WidgetBlueprintLibrary', 'SetInputMode_GameAndUIEx', PlayerController=g.get('self'),
           InMouseLockMode='DoNotLock', bHideCursorDuringCapture=False, bFlushInput=True)
    g.call('WidgetBlueprintLibrary', 'SetFocusToGameViewport')
    subsystem = g.call('SubsystemBlueprintLibrary', 'GetLocalPlayerSubSystemFromPlayerController',
                       PlayerController=g.get('self'), Class='/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem')['ReturnValue']
    enhanced = g.cast('EnhancedInputLocalPlayerSubsystem', subsystem)
    g.method('EnhancedInputLocalPlayerSubsystem', 'AddMappingContext', enhanced, MappingContext=IMC, Priority=0)
    bind(g, HUD, 'OnReplayRequested', hud, 'HandleReplay')
    bind(g, 'ApplicationLifecycleComponent', 'ApplicationWillDeactivateDelegate', g.get('ApplicationLifecycle'), 'WindowDeactivated')
    bind(g, 'ApplicationLifecycleComponent', 'ApplicationHasReactivatedDelegate', g.get('ApplicationLifecycle'), 'WindowReactivated')
    g.method(DIRECTOR, 'Initialize', director, PlayerController=g.get('self'), HUD=hud)
    action = Node(g, B.add_input_action_node(PC, 'EventGraph', IA, *g.pos()))
    g.tail = action['Started']
    g.method(DIRECTOR, 'ProcessClick', g.get('Director'))
    g.event('ReceiveEndPlay')
    end_subsystem = g.call('SubsystemBlueprintLibrary', 'GetLocalPlayerSubSystemFromPlayerController',
                           PlayerController=g.get('self'), Class='/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem')['ReturnValue']
    end_enhanced = g.cast('EnhancedInputLocalPlayerSubsystem', end_subsystem)
    g.method('EnhancedInputLocalPlayerSubsystem', 'RemoveMappingContext', end_enhanced, MappingContext=IMC)
    g.method('Widget', 'RemoveFromParent', g.get('PresentationHUD'))
    g.layout()
    require(B.set_property(PC, 'bShowMouseCursor', 'true'), 'Show cursor')
    require(B.set_property(PC, 'bAutoManageActiveCameraTarget', 'false'), 'Camera ownership')
    REPORT[PC] = compile_save(PC)
    require(B.set_property(GM, 'PlayerControllerClass', asset_class(PC)), 'Dedicated PC')
    require(B.set_property(GM, 'DefaultPawnClass', '/Script/Engine.Pawn'), 'Minimal pawn')
    require(B.set_property(GM, 'HUDClass', 'None'), 'No inherited commander HUD')
    REPORT[GM] = compile_save(GM)


def validate():
    for bp in ASSETS:
        REPORT[bp] = compile_save(bp)
    REPORT['assets'] = list(unreal.EditorAssetLibrary.list_assets(ROOT, True, False))
    REPORT['card_hierarchy'] = str(B.get_component_hierarchy(CARD))
    REPORT['director_hierarchy'] = str(B.get_component_hierarchy(DIRECTOR))
    REPORT['hud'] = str(unreal.WidgetService.validate(HUD))
    REPORT['input'] = str(unreal.InputService.get_mappings(IMC))
    for bp in ASSETS:
        for graph in B.list_graphs(bp):
            name = graph if isinstance(graph, str) else graph.graph_name
            definition = B.get_graph_definition(bp, name)
            (EVIDENCE / (bp.rsplit('/', 1)[-1] + '_' + name + '.txt')).write_text(str(definition), encoding='utf-8')


try:
    if '-run=pythonscript' not in unreal.SystemLibrary.get_command_line().lower():
        require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),
                'PIE is active. End the existing session only with its owner\'s permission, then rerun.')
    phase = globals().get('CARD_REVEAL_PHASE', 'foundation')
    {'foundation': foundation, 'card': card_graphs, 'hud': hud_graphs, 'director': director_graphs,
     'controller': controller_graphs, 'validate': validate}[phase]()
    REPORT['success'] = True
except Exception:
    REPORT['success'] = False
    REPORT['error'] = traceback.format_exc()
(EVIDENCE / ('author_' + globals().get('CARD_REVEAL_PHASE', 'foundation') + '.json')).write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(REPORT, ensure_ascii=False))
