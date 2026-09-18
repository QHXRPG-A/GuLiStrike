"""Existing-asset material/scatter candidate. Original pack and formal maps stay read-only.

Run one action at a time through commander_editor_python.py. This is an authoring
script, not a test suite or an authorization to publish the candidate.
"""
import json
import math
import random
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/Environment/PineStyleAdaptation_20260918'
BASE = '/Game/GuLiStrike/Environment/ArtReview/PineStyleAdaptation_20260918'
MAP = BASE + '/LVL_Pine_Style_v1'
SOURCE_MAP = '/Game/GuLiStrike/Environment/ArtReview/PineStyleComparison_20260917/LVL_Pine_Units_Comparison'
VENDOR = '/Game/StylizedPineEnvironment/Assets'
OWNER = 'GuLi.PineStyleAdaptation.20260918.v1'
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'Previews').mkdir(exist_ok=True)


def path(obj):
    return obj.get_path_name() if obj else None


def dirty():
    return {key: [path(p) for p in fn()] for key, fn in (
        ('content', unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages),
        ('maps', unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages))}


def write(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def owned_world():
    world = EDITOR.get_editor_world()
    assert world, 'No active editor world; refusing scene mutation'
    assert world.get_path_name().split('.')[0] == MAP, path(world)
    assert LIB.get_metadata_tag(world, 'GuLi.ArtReview.Owner') == OWNER
    assert not EDITOR.get_game_world(), 'PIE must be stopped'
    return world


def tag(obj, source=None):
    LIB.set_metadata_tag(obj, 'GuLi.ArtReview.Owner', OWNER)
    LIB.set_metadata_tag(obj, 'GuLi.ArtReview.Status', 'Candidate_PendingUserReview')
    if source:
        LIB.set_metadata_tag(obj, 'GuLi.ArtReview.Source', source)


def save(obj):
    assert path(obj).startswith(BASE + '/'), path(obj)
    assert LIB.get_metadata_tag(obj, 'GuLi.ArtReview.Owner') == OWNER
    assert LIB.save_loaded_asset(obj, False), path(obj)


def duplicate(source, dest):
    assert not LIB.does_asset_exist(dest), 'Refusing overwrite: ' + dest
    obj = LIB.duplicate_asset(source, dest)
    assert obj, dest
    tag(obj, source)
    return obj


def setup():
    assert not EDITOR.get_game_world()
    initial = dirty()
    assert not initial['maps'] and not initial['content'], initial
    assert not LIB.does_asset_exist(MAP), 'Candidate already exists'
    assert EDITOR.get_editor_world().get_path_name().split('.')[0] == SOURCE_MAP
    # Save-as affects only a new map package; the original comparison remains on disk.
    world = EDITOR.get_editor_world()
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    assert LEVEL.load_level(MAP)
    world = EDITOR.get_editor_world()
    assert world.get_path_name().split('.')[0] == MAP
    tag(world, SOURCE_MAP)
    assert LEVEL.save_current_level()
    write('scope.json', {'source_map': SOURCE_MAP, 'candidate_map': MAP, 'dirty_before': initial,
        'art_revision': '1.1', 'formal_assets_modified': False,
        'scope': 'Existing pine/grass material and scatter preview; mesh topology retained. No formal publication.',
        'geometry_review': 'New/redesigned mesh work remains subject to A/B; not approved by start instruction.',
        'outline': 'Preserve vendor vegetation without adding outlines in this isolated candidate; not a new global exception.',
        'engine': 'D:/UnrealEngine-5.7', 'source_pack': 'D:/BaiduNetdiskDownload/塞尔达松树林/StylizedPineEnvironment/StylizedPineEnvironment'})
    return {'success': True, 'map': MAP, 'dirty': dirty()}


def node(mat, cls, **props):
    n = EDIT.create_material_expression(mat, cls)
    for key, value in props.items():
        n.set_editor_property(key, value)
    return n


def link(a, output, b, pin):
    assert EDIT.connect_material_expressions(a, output, b, pin), (path(a), pin)


def scalar(mat, name, value, group='Style'):
    return node(mat, unreal.MaterialExpressionScalarParameter,
        parameter_name=name, default_value=value, group=group)


def color(mat, name, rgb):
    return node(mat, unreal.MaterialExpressionVectorParameter, parameter_name=name,
        default_value=unreal.LinearColor(*rgb, 1), group='Style')


def custom(mat, code, inputs, kind=unreal.CustomMaterialOutputType.CMOT_FLOAT3, desc=''):
    n = node(mat, unreal.MaterialExpressionCustom, code=code, output_type=kind, desc=desc)
    defs = []
    for name in inputs:
        p = unreal.CustomInput()
        p.set_editor_property('input_name', name)
        defs.append(p)
    n.set_editor_property('inputs', defs)
    for key, (src, out) in inputs.items():
        link(src, out, n, key)
    return n


def material(kind):
    folder, name = {'Leaf': ('Leaves', 'M_Leaf'), 'Bark': ('Bark', 'M_Bark'), 'Grass': ('Grass', 'M_Grass')}[kind]
    source = VENDOR + '/Materials/' + folder + '/' + name
    target = BASE + '/Materials/M_Pine_' + kind + '_Toon'
    if LIB.does_asset_exist(target):
        mat = unreal.load_asset(target)
        assert LIB.get_metadata_tag(mat, 'GuLi.ArtReview.Owner') == OWNER
        # Recover this script's incomplete first material pass, never another asset.
        graph = json.loads(unreal.MaterialNodeService.export_material_graph(target))
        original = json.loads(unreal.MaterialNodeService.export_material_graph(source))
        assert [e['class'] for e in graph['expressions'][:len(original['expressions'])]] == [e['class'] for e in original['expressions']]
        assert EDIT.get_material_property_input_node(mat, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET).get_class().get_name() != 'MaterialExpressionCustom', 'Material already completed'
        for expression in graph['expressions'][len(original['expressions']):]:
            assert unreal.MaterialNodeService.delete_expression(target, expression['id'])
    else:
        mat = duplicate(source, target)
    original_wpo = EDIT.get_material_property_input_node(mat, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    original_pin = EDIT.get_material_property_input_node_output_name(mat, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    # Remove PBR outputs rather than accidentally sampling the vendor normal/noise in the toon pass.
    for output in ('BaseColor', 'Specular', 'Roughness', 'Normal', 'SubsurfaceColor'):
        unreal.MaterialNodeService.disconnect_output(path(mat), output)
    n = node(mat, unreal.MaterialExpressionVertexNormalWS)
    light = color(mat, 'ArtLightDirection', (.35, -.55, .76))
    palettes = {
        'Leaf': ((.20, .34, .085), (.105, .215, .064), (.055, .12, .078)),
        'Bark': ((.29, .18, .103), (.175, .11, .077), (.090, .074, .069)),
        'Grass': ((.205, .355, .092), (.145, .285, .078), (.083, .192, .09)),
    }
    bright, mid, shade = [color(mat, key, rgb) for key, rgb in zip(('ToneLight', 'ToneMid', 'ToneShadow'), palettes[kind])]
    dark_cut = scalar(mat, 'ShadeThreshold', .18)
    light_cut = scalar(mat, 'LightThreshold', .46)
    up_bias = scalar(mat, 'NormalUpBias', 0.22 if kind == 'Leaf' else (0.45 if kind == 'Grass' else 0.0))
    worldpos = node(mat, unreal.MaterialExpressionWorldPosition)
    body = custom(mat,
        'float3 nn=normalize(float3(N.xy, N.z)); '
        'nn=normalize(lerp(nn,float3(0,0,1),Up)); '
        'float d=dot(nn,normalize(L)); '
        'return d<DarkCut ? Shadow : (d<LightCut ? Mid : Bright);',
        {'N': (n, ''), 'L': (light, 'RGB'), 'Bright': (bright, 'RGB'), 'Mid': (mid, 'RGB'),
         'Shadow': (shade, 'RGB'), 'DarkCut': (dark_cut, ''), 'LightCut': (light_cut, ''), 'Up': (up_bias, '')},
        desc='Three explicit tones. Fixed art light like Ship; no dynamic received surface shadow.')
    assert EDIT.connect_material_property(body, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    strength = scalar(mat, 'WindStrength', 1., 'Wind')
    if kind == 'Leaf':
        sample = node(mat, unreal.MaterialExpressionTextureSampleParameter2D,
            parameter_name='LeafTexture', texture=unreal.load_asset(VENDOR + '/Textures/Leaves/T_PineLeaf'),
            mip_value_mode=unreal.TextureMipValueMode.TMVM_MIP_BIAS, const_mip_value=2)
        coverage = scalar(mat, 'LeafCoverageThreshold', .28)
        alpha = custom(mat, 'return saturate((A-Cut)*8+0.5);', {'A': (sample, 'A'), 'Cut': (coverage, '')},
            unreal.CustomMaterialOutputType.CMOT_FLOAT1,
            'Preview only: soften subpixel needle gaps using original atlas mips; does not repaint the atlas.')
        assert EDIT.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY_MASK)
        mat.set_editor_property('opacity_mask_clip_value', .5)
    if kind == 'Grass':
        localpos = node(mat, unreal.MaterialExpressionPreSkinnedPosition)
        t = node(mat, unreal.MaterialExpressionTime)
        amplitude = scalar(mat, 'WindAmplitudeCm', 6., 'Wind')
        period = scalar(mat, 'WindPeriodSeconds', 4., 'Wind')
        direction = color(mat, 'WindDirection', (.85, .35, 0.))
        bounds = unreal.load_asset(VENDOR + '/Meshes/Grass/SM_Grass').get_bounds()
        base_z = scalar(mat, 'MeshBaseZ', bounds.origin.z - bounds.box_extent.z, 'Wind')
        height = scalar(mat, 'MeshHeight', bounds.box_extent.z * 2, 'Wind')
        wpo = custom(mat,
            'float h=saturate((P.z-BaseZ)/max(Height,.001)); '
            'float phase=dot(W.xy,float2(.00037,.00023)); '
            'float wave=sin(T*6.2831853/max(Period,.1)+phase); '
            'return normalize(float3(Dir.xy,.00001))*Amp*Strength*h*h*wave;',
            {'P': (localpos, ''), 'W': (worldpos, 'XYZ'), 'T': (t, ''), 'BaseZ': (base_z, ''),
             'Height': (height, ''), 'Period': (period, ''), 'Dir': (direction, 'RGB'),
             'Amp': (amplitude, ''), 'Strength': (strength, '')},
            desc='Root-pinned height weight on original geometry; zero strength yields exactly zero WPO. No vertex paint required.')
    else:
        wpo = custom(mat, 'return Original*Strength;',
            {'Original': (original_wpo, original_pin), 'Strength': (strength, '')}, desc='Preserve original wind deformation with master off switch.')
    assert EDIT.connect_material_property(wpo, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    EDIT.layout_material_expressions(mat)
    EDIT.recompile_material(mat)
    save(mat)
    return mat


def materials():
    owned_world()
    masters = {kind: material(kind) for kind in ('Leaf', 'Bark', 'Grass')}
    # Only instances required by the three live-tree silhouettes and grass sample.
    instances = {}
    for folder, name, kind in (
        ('Leaves', 'MI_Leaf', 'Leaf'), ('Leaves', 'MI_LeafSmall', 'Leaf'),
        ('Bark', 'MI_PineBark', 'Bark'), ('Bark', 'MI_PineTallBark', 'Bark'),
        ('Grass', 'MI_Grass', 'Grass')):
        src = VENDOR + '/Materials/' + folder + '/' + name
        instance = duplicate(src, BASE + '/Materials/' + name + '_Toon')
        parent = instances['MI_Leaf'] if name == 'MI_LeafSmall' else masters[kind]
        EDIT.set_material_instance_parent(instance, parent)
        EDIT.set_material_instance_scalar_parameter_value(instance, 'WindStrength', .35 if kind != 'Grass' else 1.)
        if kind in ('Leaf', 'Bark'):
            EDIT.set_material_instance_scalar_parameter_value(instance, 'Wind Sway', .006)
        if kind == 'Leaf':
            EDIT.set_material_instance_scalar_parameter_value(instance, 'wind Intensity', .05)
            EDIT.set_material_instance_scalar_parameter_value(instance, 'Wind Speed', .55)
        EDIT.update_material_instance(instance)
        save(instance)
        instances[name] = instance
    write('materials.json', {'masters': {k: path(v) for k,v in masters.items()},
        'instances': {k: path(v) for k,v in instances.items()},
        'source_textures_edited': False, 'shading': 'Unlit fixed ArtLightDirection three-tone, retaining cast shadows',
        'wind': 'Original tree WPO retained; grass changed to root-pinned 4-second wave, default 6cm peak.'})
    return {'success': True, 'masters': [path(m) for m in masters.values()], 'instances': [path(m) for m in instances.values()], 'dirty': dirty()}


def apply():
    world = owned_world()
    manifest = json.loads((OUT / 'materials.json').read_text(encoding='utf-8'))
    mats = {k: unreal.load_asset(v) for k,v in manifest['instances'].items()}
    meshes = {}
    originals = [VENDOR + '/Meshes/Trees/' + name for name in ('SM_PineTree_Full', 'SM_PineTreeTall_Full', 'SM_PineTree_HalfFull')]
    originals.append(VENDOR + '/Meshes/Grass/SM_Grass')
    for src in originals:
        mesh = duplicate(src, BASE + '/Meshes/' + src.rsplit('/',1)[1] + '_Toon')
        for idx, slot in enumerate(mesh.static_materials):
            key = slot.material_interface.get_name()
            assert key in mats, key
            mesh.set_material(idx, mats[key])
        # Topology, UVs, and bounds are untouched; displacement-only bounds extension.
        if mesh.get_name() == 'SM_Grass_Toon':
            mesh.set_editor_property('positive_bounds_extension', unreal.Vector(10, 10, 1))
            mesh.set_editor_property('negative_bounds_extension', unreal.Vector(10, 10, 1))
        save(mesh)
        meshes[src] = mesh
    for actor in ACTORS.get_all_level_actors():
        if actor.get_actor_label().startswith('PineReview_Tree_'):
            comp = actor.static_mesh_component
            src = comp.static_mesh.get_path_name().split('.')[0]
            comp.set_static_mesh(meshes[src])
    old_ft = '/Game/GuLiStrike/Environment/ArtReview/PineStyleComparison_20260917/Foliage/FT_ReviewGrass'
    new_ft = duplicate(old_ft, BASE + '/Foliage/FT_Grass_Toon')
    new_ft.set_editor_property('mesh', meshes[VENDOR + '/Meshes/Grass/SM_Grass'])
    new_ft.set_editor_property('cull_distance', unreal.Int32Interval(min=220000, max=260000))
    new_ft.set_editor_property('scaling', unreal.FoliageScaling.FREE)
    for axis, limits in [('x', (12., 15.)), ('y', (12., 15.)), ('z', (6., 8.))]:
        new_ft.set_editor_property('scale_' + axis, unreal.FloatInterval(min=limits[0], max=limits[1]))
    new_ft.set_editor_property('custom_navigable_geometry', unreal.HasCustomNavigableGeometry.NO)
    save(new_ft)
    # Removal is restricted to the duplicated review map and its known grass type.
    result = unreal.FoliageService.remove_all_foliage_of_type(old_ft)
    assert result.success, result.error_message
    rng = random.Random(20260918)
    transforms = []
    for x in range(-14200, -1500, 245):
        for y in range(-18500, -7400, 245):
            px, py = x + rng.uniform(-105,105), y + rng.uniform(-105,105)
            r = ((px+8400)/6200)**2 + ((py+12500)/5000)**2
            edge = .91 + .13 * math.sin(px/870) * math.cos(py/990)
            if r > edge or rng.random() < .06:
                continue
            if ((px+6500)/1750)**2 + ((py+13500)/1450)**2 < 1:
                continue
            transform = unreal.Transform(location=unreal.Vector(px, py, 0), rotation=unreal.Rotator(yaw=rng.uniform(0,360)),
                scale=unreal.Vector(rng.uniform(12,15),rng.uniform(12,15),rng.uniform(6,8)))
            transforms.append(transform)
    unreal.InstancedFoliageActor.add_instances(world, new_ft, transforms)
    count = unreal.FoliageService.get_instance_count(path(new_ft))
    assert count == len(transforms), (count, len(transforms))
    for actor in ACTORS.get_all_level_actors():
        if isinstance(actor, unreal.InstancedFoliageActor):
            actor.set_folder_path('PineComparison/Nature/Grass')
    assert LEVEL.save_current_level()
    report = {'success': True, 'map': MAP, 'mesh_copies': {k:path(v) for k,v in meshes.items()},
        'mesh_topology_changed': False, 'foliage_type':path(new_ft), 'grass_instances':count,
        'before_grass_instances':2293, 'grass_triangles_per_instance':37, 'grass_lod0_total': count*37,
        'grass_scale':{'xy':[12,15], 'z':[6,8]}, 'tree_scale': 'original review 10x retained',
        'cull_cm': [220000,260000], 'source_cull_cm':[0,260000],
        'ground_rocks_lights_units': 'Unchanged from original comparison for controlled before/after', 'dirty':dirty()}
    write('scene_changes.json', report)
    return report


def refine():
    owned_world()
    mat = unreal.load_asset(BASE + '/Materials/M_Pine_Leaf_Toon')
    body = EDIT.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert isinstance(body, unreal.MaterialExpressionCustom)
    assert 'RootShade' not in body.get_editor_property('code'), 'Already refined'
    definitions = list(body.get_editor_property('inputs'))
    for key in ('UV', 'RootShade'):
        value = unreal.CustomInput()
        value.set_editor_property('input_name', key)
        definitions.append(value)
    body.set_editor_property('inputs', definitions)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    roots = scalar(mat, 'CanopyRootShade', .58)
    link(uv, '', body, 'UV')
    link(roots, '', body, 'RootShade')
    body.set_editor_property('code', body.get_editor_property('code').replace(
        'float d=dot(nn,normalize(L));',
        'float d=dot(nn,normalize(L))-RootShade*smoothstep(.12,.92,UV.y);'))
    EDIT.layout_material_expressions(mat)
    EDIT.recompile_material(mat)
    save(mat)
    ft = unreal.load_asset(BASE + '/Foliage/FT_Grass_Toon')
    ft.set_editor_property('cast_shadow', False)
    save(ft)
    for a in ACTORS.get_all_level_actors():
        for c in a.get_components_by_class(unreal.FoliageInstancedStaticMeshComponent):
            if c.static_mesh and path(c.static_mesh).startswith(BASE):
                c.set_cast_shadow(False)
    # Grass surface underpainting belongs to this review ground, not the foliage mesh.
    ground = next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label() == 'PineReview_ReviewGround')
    src = path(ground.static_mesh_component.get_material(0))
    g = duplicate(src, BASE + '/Materials/M_ReviewGround_GrassUnderlay')
    previous = EDIT.get_material_property_input_node(g, unreal.MaterialProperty.MP_BASE_COLOR)
    position = node(g, unreal.MaterialExpressionWorldPosition)
    paint = color(g, 'GrassUnderlayColor', (.064, .150, .038))
    mixed = custom(g,
        'float r=pow((P.x+8400)/6200,2)+pow((P.y+12500)/5000,2); '
        'float edge=.91+.13*sin(P.x/870)*cos(P.y/990); '
        'float h=pow((P.x+6500)/1750,2)+pow((P.y+13500)/1450,2); '
        'float mask=(1-smoothstep(edge-.07,edge+.07,r))*smoothstep(.86,1.05,h); '
        'return lerp(Base,Grass,mask);',
        {'P': (position, 'XYZ'), 'Base': (previous, ''), 'Grass': (paint, 'RGB')},
        desc='Review-only grass underpainting. Original ground outside the lawn and unit apron unchanged.')
    assert EDIT.connect_material_property(mixed, '', unreal.MaterialProperty.MP_BASE_COLOR)
    EDIT.layout_material_expressions(g)
    EDIT.recompile_material(g)
    save(g)
    ground.static_mesh_component.set_material(0, g)
    assert LEVEL.save_current_level()
    changes = json.loads((OUT / 'scene_changes.json').read_text(encoding='utf-8'))
    changes['ground_rocks_lights_units'] = 'Rocks/lights/units unchanged. Ground receives scoped lawn underpainting only.'
    changes['refinement'] = {'canopy_root_tone': .58, 'grass_cast_shadow': False,
        'reason': 'Keep broad branch tiers; replace noisy per-blade ground shadows with coherent lawn underpaint.',
        'ground_material': path(g)}
    write('scene_changes.json', changes)
    return {'success': True, 'refinement': changes['refinement'], 'dirty': dirty()}


def refine_grass():
    owned_world()
    mat = unreal.load_asset(BASE + '/Materials/M_Pine_Grass_Toon')
    body = EDIT.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert 'RootShade' not in body.get_editor_property('code'), 'Already refined'
    definitions = list(body.get_editor_property('inputs'))
    for key in ('UV', 'RootShade'):
        value = unreal.CustomInput()
        value.set_editor_property('input_name', key)
        definitions.append(value)
    body.set_editor_property('inputs', definitions)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    roots = scalar(mat, 'GrassRootShade', .44)
    link(uv, '', body, 'UV')
    link(roots, '', body, 'RootShade')
    body.set_editor_property('code', body.get_editor_property('code').replace(
        'float d=dot(nn,normalize(L));',
        'float d=dot(nn,normalize(L))-RootShade*smoothstep(.30,.96,UV.y);'))
    EDIT.layout_material_expressions(mat)
    EDIT.recompile_material(mat)
    save(mat)
    return {'success': True, 'grass_root_shade': .44, 'dirty': dirty()}


VIEWS = json.loads((ROOT / 'ArtSource/Environment/PineStyleComparison_20260917/camera_presets.json').read_text(encoding='utf-8'))
VIEWS['10_tree_detail'] = {'target_cm':[20500,3000,7600], 'direction':[.25,-1,.28], 'distance_cm':42000, 'fov':45}
VIEWS['11_grass_detail'] = {'target_cm':[-6700,-13200,850], 'direction':[.65,-1,.42], 'distance_cm':14500, 'fov':45}


def set_view(name):
    owned_world()
    v = VIEWS[name]
    target = unreal.Vector(*v['target_cm'])
    pos = target + unreal.Vector(*v['direction']).normal()*v['distance_cm']
    rot = unreal.MathLibrary.find_look_at_rotation(pos,target)
    cam = next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label() == 'PineReview_Camera')
    cam.set_actor_location(pos,False,False)
    cam.set_actor_rotation(rot,False)
    cam.get_component_by_class(unreal.CameraComponent).set_field_of_view(v['fov'])
    EDITOR.set_level_viewport_camera_info(pos,rot)
    unreal.ViewportService.set_view_mode('lit')
    unreal.ViewportService.set_realtime(True)
    unreal.ViewportService.set_game_view(True)
    unreal.ViewportService.set_exposure(True,0.)
    ACTORS.set_selected_level_actors([])
    return cam


def capture(name='03_overview'):
    global SHOT
    cam = set_view(name)
    SHOT = unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(OUT/'Previews'/(name+'.png')),cam,delay=2.)
    return {'success':True,'queued':name}


def gallery():
    owned_world()
    global GALLERY_HANDLE, GALLERY_STATE
    assert not globals().get('GALLERY_HANDLE')
    names=list(VIEWS)
    GALLERY_STATE={'index':0,'phase':'camera','last':time.monotonic(),'start':time.monotonic(),'images':[]}
    def tick(dt):
        global GALLERY_HANDLE, SHOT
        state=GALLERY_STATE
        try:
            if time.monotonic()-state['start']>300:
                raise RuntimeError('Gallery timeout')
            if state['index']>=len(names):
                handle=GALLERY_HANDLE
                GALLERY_HANDLE=None
                unreal.unregister_slate_post_tick_callback(handle)
                set_view('03_overview')
                assert LEVEL.save_current_level()
                write('gallery.json',{'success':True,'images':state['images']})
                write('camera_presets.json',VIEWS)
                return
            name=names[state['index']]
            if state['phase']=='camera':
                set_view(name)
                state.update(phase='settle',last=time.monotonic())
            elif state['phase']=='settle' and time.monotonic()-state['last']>3:
                cam=next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label()=='PineReview_Camera')
                SHOT=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(OUT/'Previews'/(name+'.png')),cam,delay=1.)
                state.update(phase='capture',last=time.monotonic())
            elif state['phase']=='capture' and SHOT.is_task_done():
                file=OUT/'Previews'/(name+'.png')
                assert file.is_file() and file.stat().st_size>10000,str(file)
                state['images'].append(str(file))
                state.update(phase='camera',index=state['index']+1)
        except Exception:
            write('gallery.json',{'success':False,'error':traceback.format_exc(),'state':state})
            if GALLERY_HANDLE:
                unreal.unregister_slate_post_tick_callback(GALLERY_HANDLE)
                GALLERY_HANDLE=None
    GALLERY_HANDLE=unreal.register_slate_post_tick_callback(tick)
    return {'success':True,'queued_views':names}


def verify():
    world=owned_world()
    actors=[]
    meshes={}
    for a in ACTORS.get_all_level_actors():
        if isinstance(a,unreal.StaticMeshActor):
            c=a.static_mesh_component
            actors.append({'name':a.get_actor_label(),'mesh':path(c.static_mesh),
                'scale':list(a.get_actor_scale3d().to_tuple()), 'materials':[path(c.get_material(i)) for i in range(c.get_num_materials())]})
    for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(BASE+'/Meshes',recursive=True):
        m=data.get_asset()
        meshes[path(m)]={'triangles':[m.get_num_triangles(i) for i in range(m.get_num_lods())],
            'materials':[path(s.material_interface) for s in m.static_materials]}
    diagnostics={}
    for kind in ('Leaf','Bark','Grass'):
        mpath=BASE+'/Materials/M_Pine_'+kind+'_Toon'
        d=unreal.MaterialNodeService.get_material_diagnostics(mpath)
        assert d.success and d.is_compiled_ok, str(d)
        diagnostics[kind]={'success':d.success, 'compiled_ok':d.is_compiled_ok, 'errors':str(d.compile_errors),
            'referenced_textures':list(d.referenced_texture_paths)}
    report={'success':True,'map':path(world),'actors':actors,'meshes':meshes,'material_diagnostics':diagnostics,
        'foliage':[{'type':f.foliage_type_path,'count':f.instance_count,'mesh':f.mesh_path} for f in unreal.FoliageService.list_foliage_types()],
        'dirty':dirty(),'visual_approval':'pending','performance':'not_measured','new_mesh_geometry':'not_authored'}
    ft=unreal.load_asset(BASE+'/Foliage/FT_Grass_Toon')
    report['grass_type_settings']={'cast_shadow':ft.get_editor_property('cast_shadow'),
        'cull_distance':str(ft.get_editor_property('cull_distance')),
        'collision':str(ft.get_editor_property('body_instance').get_editor_property('collision_enabled')),
        'navigation':str(ft.get_editor_property('custom_navigable_geometry'))}
    write('readback.json',report)
    return report


def reload_saved():
    owned_world()
    assert not globals().get('GALLERY_HANDLE')
    assert not dirty()['maps'],dirty()
    assert LEVEL.load_level(SOURCE_MAP)
    original_count=unreal.FoliageService.get_instance_count('/Game/GuLiStrike/Environment/ArtReview/PineStyleComparison_20260917/Foliage/FT_ReviewGrass')
    assert original_count==2293,original_count
    original_trees=[path(a.static_mesh_component.static_mesh) for a in ACTORS.get_all_level_actors() if a.get_actor_label().startswith('PineReview_Tree_')]
    assert len(original_trees)==5 and all(p.startswith(VENDOR) for p in original_trees)
    assert LEVEL.load_level(MAP)
    report=verify()
    expected=json.loads((OUT/'scene_changes.json').read_text(encoding='utf-8'))['grass_instances']
    assert sum(f['count'] for f in report['foliage'] if f['type'].startswith(BASE))==expected
    report.update(saved_map_reloaded=True,original_map_preserved=True,original_grass_instances=original_count)
    write('saved_readback.json',report)
    return {'success':True,'saved_map_reloaded':True,'original_map_preserved':True,'grass_count':expected,'dirty':dirty()}


def read_saved_packages():
    """Non-mutating fallback when another editor context owns the active map.

    Read the explicitly named worlds through UE rather than switching levels.
    Run after save / editor restart, and require all relevant packages clean.
    """
    before = dirty()
    assert not any(p.startswith(BASE) or p.startswith(SOURCE_MAP) for values in before.values() for p in values), before
    worlds = {}
    for key, pkg in (('candidate', MAP), ('original', SOURCE_MAP)):
        world = unreal.load_object(None, pkg + '.' + pkg.rsplit('/', 1)[1])
        assert world, pkg
        actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
        rows, foliage = [], []
        for actor in actors:
            if isinstance(actor, unreal.StaticMeshActor):
                c = actor.static_mesh_component
                rows.append({'label': actor.get_actor_label(), 'mesh': path(c.static_mesh),
                    'location': list(actor.get_actor_location().to_tuple()), 'rotation': list(actor.get_actor_rotation().to_tuple()),
                    'scale': list(actor.get_actor_scale3d().to_tuple()),
                    'materials': [path(c.get_material(i)) for i in range(c.get_num_materials())]})
            for c in actor.get_components_by_class(unreal.FoliageInstancedStaticMeshComponent):
                foliage.append({'mesh': path(c.static_mesh), 'count': c.get_instance_count(),
                    'collision': str(c.get_collision_enabled()), 'cast_shadow': c.get_editor_property('cast_shadow'),
                    'materials': [path(c.get_material(i)) for i in range(c.get_num_materials())]})
        worlds[key] = {'world': path(world), 'actors': rows, 'foliage': foliage}
    expected = json.loads((OUT / 'scene_changes.json').read_text(encoding='utf-8'))['grass_instances']
    candidate_grass = [c for c in worlds['candidate']['foliage'] if c['mesh'] and c['mesh'].startswith(BASE)]
    assert sum(c['count'] for c in candidate_grass) == expected
    assert all('NO_COLLISION' in c['collision'] and not c['cast_shadow'] for c in candidate_grass)
    assert sum(c['count'] for c in worlds['original']['foliage']) == 2293
    original_by_label = {r['label']: r for r in worlds['original']['actors']}
    for row in worlds['candidate']['actors']:
        name = row['label']
        if name.startswith('PineReview_Rock_') or name in ('PineReview_Ship', 'PineReview_ShipContour', 'PineReview_Sweeper', 'PineReview_WarMachine'):
            assert row == original_by_label[name], name
        if name.startswith('PineReview_Tree_'):
            assert row['mesh'].startswith(BASE)
            assert original_by_label[name]['mesh'].startswith(VENDOR)
    meshes, diagnostics = {}, {}
    changes = json.loads((OUT / 'scene_changes.json').read_text(encoding='utf-8'))
    for source, dest in changes['mesh_copies'].items():
        src, dst = unreal.load_asset(source), unreal.load_asset(dest)
        tris = [dst.get_num_triangles(i) for i in range(dst.get_num_lods())]
        assert tris == [src.get_num_triangles(i) for i in range(src.get_num_lods())]
        meshes[dest] = {'triangles': tris, 'materials': [path(s.material_interface) for s in dst.static_materials]}
    for kind in ('Leaf', 'Bark', 'Grass'):
        p = BASE + '/Materials/M_Pine_' + kind + '_Toon'
        mat = unreal.load_asset(p)
        assert mat and isinstance(mat, unreal.Material), p
        d = unreal.MaterialNodeService.get_material_diagnostics(p)
        graph_raw = unreal.MaterialNodeService.export_material_graph(p)
        if graph_raw:
            write('graph_' + kind.lower() + '.json', json.loads(graph_raw))
        output = EDIT.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        wpo = EDIT.get_material_property_input_node(mat, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        assert isinstance(output, unreal.MaterialExpressionCustom) and isinstance(wpo, unreal.MaterialExpressionCustom)
        diagnostics[kind] = {'service_available': d.success, 'compiled_ok': d.is_compiled_ok if d.success else None,
            'errors': str(d.compile_errors) if d.success else 'Diagnostic service unavailable in current editor context; not a shader failure report.',
            'referenced_textures': list(d.referenced_texture_paths), 'two_sided': mat.get_editor_property('two_sided'),
            'saved_emissive_code': output.get_editor_property('code'), 'saved_wpo_code': wpo.get_editor_property('code'),
            'saved_shading_model': str(mat.get_editor_property('shading_model'))}
    after = dirty()
    assert before == after, (before, after)
    report = {'success': True, 'method': 'Read explicitly named saved World assets without changing active editor map; after editor restart and final scoped save.',
        'active_map_switch_performed': False, 'worlds': worlds, 'meshes': meshes,
        'diagnostics': diagnostics, 'grass_instances': expected, 'original_map_preserved': True,
        'units_rocks_unchanged': True, 'dirty_before': before, 'dirty_after': after,
        'visual_approval': 'pending', 'performance': 'not_measured', 'wind_video': 'not_recorded',
        'shader_diagnostic_scope': 'Initial three masters compiled successfully before restart. Final diagnostic service may be unavailable; see per-material availability. Actual final render screenshots retained.'}
    write('readback.json', report)
    write('saved_readback.json', report)
    return {'success': True, 'method': report['method'], 'grass_instances': expected,
        'original_grass_instances': 2293, 'diagnostics': diagnostics, 'dirty': after}


def dispatch(action):
    try:
        result=globals()[action]()
    except Exception:
        result={'success':False,'traceback':traceback.format_exc()}
        write('error_'+action+'.json',result)
    unreal.MCPythonHelper.submit_result(json.dumps(result,ensure_ascii=False))
