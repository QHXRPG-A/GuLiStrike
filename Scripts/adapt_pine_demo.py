"""Author the supplied Demo_Map in place after v1 visual approval.

One explicit action per live-editor call. Never save unrelated dirty packages.
Original source and byte-for-byte project baseline live outside Content.
"""
import hashlib
import json
import math
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/Environment/PineDemoAdaptation_20260918'
PACK = '/Game/StylizedPineEnvironment'
BASE = PACK + '/Assets'
MAP = PACK + '/Maps/Demo_Map'
APPROVED = '/Game/GuLiStrike/Environment/ArtReview/PineStyleAdaptation_20260918'
OWNER = 'GuLi.PineDemoAdaptation.20260918'
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
OUT.mkdir(parents=True, exist_ok=True)
(OUT/'Previews').mkdir(exist_ok=True)


def path(obj):
    return obj.get_path_name() if obj else None


def write(name, value):
    (OUT/name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def dirty():
    return {k:[path(p) for p in f()] for k,f in (
        ('maps',unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages),
        ('content',unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages))}


def guard():
    assert (OUT/'copy_and_backup.json').is_file(), 'Requires verified backup'
    assert not EDITOR.get_game_world(), 'PIE must be stopped'
    world=EDITOR.get_editor_world()
    assert world and path(world).split('.')[0] == MAP, path(world)
    return world


def inventory():
    assert not (OUT/'original_assets.json').exists(),'Original asset snapshot is immutable'
    rows=[]
    for data in REG.get_assets_by_path(BASE, recursive=True):
        obj=data.get_asset()
        row={'path':path(obj),'class':obj.get_class().get_name()}
        if isinstance(obj,unreal.StaticMesh):
            b=obj.get_bounds()
            row.update(triangles=[obj.get_num_triangles(i) for i in range(obj.get_num_lods())],
                       size_cm=list((b.box_extent*2).to_tuple()),
                       materials=[path(s.material_interface) for s in obj.static_materials])
        elif isinstance(obj,unreal.MaterialInstanceConstant):
            row['parent']=path(obj.parent)
            row['texture_overrides']={str(v.parameter_info.name):path(v.parameter_value) for v in obj.texture_parameter_values}
            row['scalar_overrides']={str(v.parameter_info.name):v.parameter_value for v in obj.scalar_parameter_values}
            row['vector_overrides']={str(v.parameter_info.name):list(v.parameter_value.to_tuple()) for v in obj.vector_parameter_values}
        elif isinstance(obj,unreal.Material):
            row.update(shading=str(obj.get_editor_property('shading_model')),blend=str(obj.get_editor_property('blend_mode')))
            graph=unreal.MaterialNodeService.export_material_graph(path(obj))
            if graph:
                write('original_graph_'+obj.get_name()+'.json',json.loads(graph))
        elif isinstance(obj,unreal.LandscapeGrassType):
            row['grass_varieties']=[str(v) for v in obj.get_editor_property('grass_varieties')]
        rows.append(row)
    write('original_assets.json',rows)
    return {'success':True,'classes':{c:sum(r['class']==c for r in rows) for c in sorted(set(r['class'] for r in rows))},
            'materials':[r for r in rows if r['class']=='Material'],
            'meshes':[r['path'] for r in rows if r['class']=='StaticMesh'],
            'grass':[r for r in rows if r['class']=='LandscapeGrassType']}


def scene_snapshot(filename='original_scene.json'):
    if filename=='original_scene.json':
        assert not (OUT/filename).exists(),'Original scene snapshot is immutable'
    world=guard()
    rows=[]
    for a in ACTORS.get_all_level_actors():
        row={'name':a.get_name(),'label':a.get_actor_label(),'class':a.get_class().get_name(),
             'location':list(a.get_actor_location().to_tuple()),'rotation':list(a.get_actor_rotation().to_tuple()),
             'scale':list(a.get_actor_scale3d().to_tuple()),'components':[]}
        for c in a.get_components_by_class(unreal.MeshComponent):
            v={'name':c.get_name(),'class':c.get_class().get_name(),
               'materials':[path(c.get_material(i)) for i in range(c.get_num_materials())]}
            if isinstance(c,unreal.StaticMeshComponent):
                v['mesh']=path(c.static_mesh)
            if isinstance(c,unreal.InstancedStaticMeshComponent):
                v['instances']=c.get_instance_count()
                transforms=[list(c.get_instance_transform(i,False).translation.to_tuple())+
                            list(c.get_instance_transform(i,False).rotation.to_tuple())+
                            list(c.get_instance_transform(i,False).scale3d.to_tuple()) for i in range(c.get_instance_count())]
                v['instance_transform_sha256']=hashlib.sha256(json.dumps(transforms).encode()).hexdigest()
            row['components'].append(v)
        if isinstance(a,unreal.LandscapeProxy):
            row['landscape_material']=path(a.get_editor_property('landscape_material'))
        if isinstance(a,unreal.DirectionalLight):
            row['light']={'direction':list((-a.get_actor_forward_vector()).to_tuple()),'intensity':a.light_component.intensity}
        if isinstance(a,unreal.PostProcessVolume):
            row['postprocess']=str(a.get_editor_property('settings'))
        rows.append(row)
    options=unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True)
    pending=[MAP];seen=set();deps={}
    while pending:
        p=pending.pop()
        if p in seen: continue
        seen.add(p)
        children=[str(d) for d in REG.get_dependencies(p,options)]
        deps[p]=children
        pending += [c for c in children if c.startswith(PACK) and c not in seen]
    report={'world':path(world),'actors':rows,'dependencies':deps,'camera':str(EDITOR.get_level_viewport_camera_info()),'dirty':dirty()}
    write(filename,report)
    return {'success':True,'actor_classes':{k:sum(a['class']==k for a in rows) for k in sorted(set(a['class'] for a in rows))},
            'foliage':[dict(actor=a['name'],**c) for a in rows for c in a['components'] if 'instances' in c],
            'landscape':[a for a in rows if 'landscape_material' in a],
            'lights':[a for a in rows if 'light' in a], 'dependency_packages':len(deps),'camera':report['camera']}


def open_demo():
    assert not (OUT/'original_scene.json').exists(),'Original map already copied and inspected; use save_and_reload'
    before=dirty()
    assert not any(before.values()),before
    previous=EDITOR.get_editor_world()
    initial={'map':path(previous),'camera':str(EDITOR.get_level_viewport_camera_info()),'dirty':before}
    REG.scan_paths_synchronous([PACK],force_rescan=True)
    assert LIB.does_asset_exist(MAP),MAP
    write('editor_before.json',initial)
    assert LEVEL.load_level(MAP)
    return scene_snapshot()


def node(mat, cls, **props):
    obj=EDIT.create_material_expression(mat,cls)
    for k,v in props.items(): obj.set_editor_property(k,v)
    return obj


def link(a,out,b,pin):
    assert EDIT.connect_material_expressions(a,out,b,pin),(path(a),pin)


def scalar(mat,name,value):
    return node(mat,unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=value,group='GuLi Style')


def color(mat,name,rgb):
    return node(mat,unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=unreal.LinearColor(*rgb,1),group='GuLi Style')


def custom(mat,code,inputs,one=False,desc=''):
    n=node(mat,unreal.MaterialExpressionCustom,code=code,desc=desc,
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1 if one else unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    defs=[]
    for name in inputs:
        v=unreal.CustomInput();v.set_editor_property('input_name',name);defs.append(v)
    n.set_editor_property('inputs',defs)
    for name,(src,pin) in inputs.items(): link(src,pin,n,name)
    return n


def save(obj):
    assert path(obj).startswith(BASE+'/'),path(obj)
    LIB.set_metadata_tag(obj,'GuLi.Style.Owner',OWNER)
    LIB.set_metadata_tag(obj,'GuLi.Style.Version','FullPack.v1')
    LIB.set_metadata_tag(obj,'GuLi.Style.Approval','PineStyleAdaptation_v1 approved; full Demo_Map pending final review')
    assert LIB.save_loaded_asset(obj,False),path(obj)


PALETTES={
    'Leaf':((.20,.34,.085),(.105,.215,.064),(.055,.12,.078)),
    'Bark':((.29,.18,.103),(.175,.11,.077),(.090,.074,.069)),
    'Grass':((.205,.355,.092),(.145,.285,.078),(.083,.192,.09)),
    'Rock':((.43,.445,.49),(.265,.28,.335),(.145,.175,.235)),
    'Wood':((.32,.20,.11),(.215,.13,.085),(.12,.094,.082)),
    'Flower':((.88,.82,.62),(.61,.57,.44),(.39,.42,.38)),
}
MATERIALS={'Leaf':'Materials/Leaves/M_Leaf','Bark':'Materials/Bark/M_Bark','Grass':'Materials/Grass/M_Grass',
    'Rock':'Materials/Rock/M_Rock','Wood':'Materials/WoodStructures/M_Wood','Flower':'Materials/Flowers/M_Flower'}


def art_direction():
    sun=next(a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.DirectionalLight))
    return tuple((-sun.get_actor_forward_vector()).to_tuple())


def toon_body(mat,kind):
    palettes=PALETTES[kind]
    n=node(mat,unreal.MaterialExpressionVertexNormalWS)
    uv=node(mat,unreal.MaterialExpressionTextureCoordinate,coordinate_index=0)
    root=.58 if kind=='Leaf' else (.44 if kind=='Grass' else 0.)
    inp={'N':(n,''),'UV':(uv,''),'L':(color(mat,'ArtLightDirection',art_direction()),'RGB'),
        'DarkCut':(scalar(mat,'ShadeThreshold',.18),''),'LightCut':(scalar(mat,'LightThreshold',.46),''),
        'Up':(scalar(mat,'NormalUpBias',.22 if kind=='Leaf' else (.45 if kind in ('Grass','Flower') else 0.)),''),
        'RootShade':(scalar(mat,'RootShade',root),'')}
    for key,rgb in zip(('Bright','Mid','Shadow'),palettes):
        inp[key]=(color(mat,{'Bright':'ToneLight','Mid':'ToneMid','Shadow':'ToneShadow'}[key],rgb),'RGB')
    return custom(mat,
        'float3 nn=normalize(lerp(normalize(N),float3(0,0,1),Up)); '
        'float d=dot(nn,normalize(L))-RootShade*smoothstep(.12,.92,UV.y); '
        'return d<DarkCut?Shadow:(d<LightCut?Mid:Bright);',inp,
        desc='Approved broad three-tone palette; fixed ArtLightDirection aligned to Demo sun; no micro-normal/color noise.')


def root_wind(mat,height,amplitude):
    return custom(mat,
        'float h=saturate((P.z-BaseZ)/max(Height,.001)); '
        'float phase=dot(W.xy,float2(.0037,.0023)); '
        'float wave=sin(T*6.2831853/max(Period,.1)+phase); '
        'return normalize(float3(Dir.xy,.00001))*Amp*Strength*h*h*wave;',
        {'P':(node(mat,unreal.MaterialExpressionPreSkinnedPosition),''),
         'W':(node(mat,unreal.MaterialExpressionWorldPosition),'XYZ'),
         'T':(node(mat,unreal.MaterialExpressionTime),''),'BaseZ':(scalar(mat,'MeshBaseZ',0.),''),
         'Height':(scalar(mat,'MeshHeight',height),''),'Period':(scalar(mat,'WindPeriodSeconds',4.),''),
         'Dir':(color(mat,'WindDirection',(.85,.35,0)),'RGB'),'Amp':(scalar(mat,'WindAmplitudeCm',amplitude),''),
         'Strength':(scalar(mat,'WindStrength',1.),'')},
        desc='Root-pinned local height weight, phase in world space; WindStrength=0 exactly still. Native map scale.')


def material(kind):
    guard()
    mat=unreal.load_asset(BASE+'/'+MATERIALS[kind])
    assert LIB.get_metadata_tag(mat,'GuLi.Style.Owner')!=OWNER,'Already adapted: '+kind
    original_wpo=EDIT.get_material_property_input_node(mat,unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    original_pin=EDIT.get_material_property_input_node_output_name(mat,unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('used_with_instanced_static_meshes',True)
    for prop in ('BaseColor','Specular','Roughness','Normal','SubsurfaceColor'):
        unreal.MaterialNodeService.disconnect_output(path(mat),prop)
    body=toon_body(mat,kind)
    if kind=='Rock':
        # Preserve the source's optional moss regions, but replace noise with one clean slope band.
        moss=color(mat,'MossTone',(.14,.245,.072))
        strength=scalar(mat,'MossAmount',0.)
        normal=node(mat,unreal.MaterialExpressionVertexNormalWS)
        body=custom(mat,'float k=step(MinSlope,N.z)*Amount;return lerp(Base,Moss,k);',
            {'Base':(body,''),'Moss':(moss,'RGB'),'N':(normal,''),'Amount':(strength,''),
             'MinSlope':(scalar(mat,'MossSlope',.75),'')},desc='Broad optional moss cap; no mottled texture overlay.')
    assert EDIT.connect_material_property(body,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if kind=='Leaf':
        sample=node(mat,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name='LeafTexture',
            texture=unreal.load_asset(BASE+'/Textures/Leaves/T_PineLeaf'),
            mip_value_mode=unreal.TextureMipValueMode.TMVM_MIP_BIAS,const_mip_value=2)
        alpha=custom(mat,'return saturate((A-Cut)*8+.5);',{'A':(sample,'A'),'Cut':(scalar(mat,'LeafCoverageThreshold',.28),'')},True)
        assert EDIT.connect_material_property(alpha,'',unreal.MaterialProperty.MP_OPACITY_MASK)
        mat.set_editor_property('opacity_mask_clip_value',.5)
    if kind in ('Grass','Flower'):
        wpo=root_wind(mat,23.32 if kind=='Grass' else 90.,.7 if kind=='Grass' else 2.)
    elif kind in ('Leaf','Bark'):
        wpo=custom(mat,'return Original*Strength;',{'Original':(original_wpo,original_pin),
            'Strength':(scalar(mat,'WindStrength',.35),'')})
    else:
        wpo=None
    if wpo: assert EDIT.connect_material_property(wpo,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    EDIT.layout_material_expressions(mat)
    EDIT.recompile_material(mat)
    save(mat)
    return {'path':path(mat),'kind':kind}


def plants():
    return {'success':True,'masters':[material(k) for k in ('Leaf','Bark','Grass','Flower')]}


def structures():
    return {'success':True,'masters':[material(k) for k in ('Rock','Wood')]}


def set_palette(mi,palette):
    for key,rgb in zip(('ToneLight','ToneMid','ToneShadow'),palette):
        EDIT.set_material_instance_vector_parameter_value(mi,key,unreal.LinearColor(*rgb,1))
        stored=next(v.parameter_value for v in mi.vector_parameter_values if str(v.parameter_info.name)==key)
        assert all(abs(a-b)<1e-5 for a,b in zip(stored.to_tuple(),(*rgb,1))),(path(mi),key)


def instances():
    guard()
    rows=json.loads((OUT/'original_assets.json').read_text(encoding='utf-8'))
    done=[]
    for row in rows:
        if row['class']!='MaterialInstanceConstant': continue
        mi=unreal.load_asset(row['path']); name=mi.get_name()
        kind=next((k for k,v in MATERIALS.items() if row['parent'].split('.')[0]==BASE+'/'+v),None)
        if name=='MI_LeafSmall':kind='Leaf'
        if not kind:continue
        palette=PALETTES[kind]
        if kind=='Rock' and '/Dark/' in path(mi):
            palette=((.255,.29,.35),(.155,.20,.265),(.08,.125,.19))
        if kind=='Bark' and ('White' in name or name=='MI_PineDeadBark'):
            palette=((.48,.455,.39),(.33,.31,.28),(.215,.23,.235))
        if kind=='Bark' and 'BrokenArea' in name:
            palette=((.45,.295,.15),(.30,.185,.105),(.19,.135,.095))
        if name=='MI_Fern':palette=((.24,.38,.10),(.15,.265,.082),(.073,.16,.088))
        if name=='MI_Bush':palette=((.24,.34,.11),(.135,.235,.086),(.068,.14,.085))
        if kind=='Flower':
            palette={
              'MI_Flower1':((.88,.83,.67),(.66,.63,.51),(.42,.46,.405)),
              'MI_Flower2':((.68,.09,.105),(.46,.055,.08),(.265,.046,.088)),
              'MI_Flower3':((.81,.47,.07),(.57,.29,.055),(.30,.17,.08)),
              'MI_Stem':PALETTES['Grass']}[name]
        set_palette(mi,palette)
        EDIT.set_material_instance_vector_parameter_value(mi,'ArtLightDirection',unreal.LinearColor(*art_direction(),1))
        if kind in ('Leaf','Bark'):
            EDIT.set_material_instance_scalar_parameter_value(mi,'WindStrength',.35)
            EDIT.set_material_instance_scalar_parameter_value(mi,'Wind Sway',.006)
            if kind=='Leaf':
                EDIT.set_material_instance_scalar_parameter_value(mi,'wind Intensity',.05)
                EDIT.set_material_instance_scalar_parameter_value(mi,'Wind Speed',.55)
                if name in ('MI_Bush','MI_Fern'):
                    EDIT.set_material_instance_scalar_parameter_value(mi,'RootShade',.22)
        EDIT.update_material_instance(mi)
        save(mi); done.append({'path':path(mi),'kind':kind,'palette':palette})
    write('adapted_instances.json',done)
    return {'success':True,'count':len(done)}


def landscape():
    guard()
    mat=unreal.load_asset(BASE+'/Materials/Landscape/M_Landscape')
    assert LIB.get_metadata_tag(mat,'GuLi.Style.Owner')!=OWNER,'Already adapted'
    graph=json.loads(unreal.MaterialNodeService.export_material_graph(path(mat)))
    baseline=json.loads((OUT/'original_graph_M_Landscape.json').read_text(encoding='utf-8'))
    count=len(baseline['expressions'])
    assert [n['class'] for n in graph['expressions'][:count]]==[n['class'] for n in baseline['expressions']]
    for extra in reversed(graph['expressions'][count:]):
        assert unreal.MaterialNodeService.delete_expression(path(mat),extra['id'])
    # Preserve original auto slope mask and grass outputs, six paint layers and landscape height/weights.
    old_output=EDIT.get_material_property_input_node(mat,unreal.MaterialProperty.MP_BASE_COLOR)
    pending=[old_output];seen=set();auto=None
    while pending:
        n=pending.pop()
        if path(n) in seen:continue
        seen.add(path(n))
        if isinstance(n,unreal.MaterialExpressionMaterialFunctionCall):
            if n.get_editor_property('material_function').get_name()=='MF_Auto':auto=n
        pending += [x for x in EDIT.get_inputs_for_material_expression(mat,n) if x]
    assert auto,'Missing original auto layer'
    grass=color(mat,'GuLi_GroundGrass',(.14,.255,.064))
    rock=color(mat,'GuLi_GroundRock',(.265,.285,.32))
    gravel=color(mat,'GuLi_GroundGravel',(.215,.155,.10))
    dirt=color(mat,'GuLi_GroundDirt',(.37,.29,.17))
    automatic=custom(mat,'return lerp(Grass,Rock,saturate(Mask));',
        {'Grass':(grass,'RGB'),'Rock':(rock,'RGB'),'Mask':(auto,'Mask')})
    blend=node(mat,unreal.MaterialExpressionLandscapeLayerBlend)
    names=['AutoLayer','Grass_WithFoliage','Grass','Dirt_Gravel','Dirt_Light','Rock']
    layers=[]
    for name in names:
        item=unreal.LayerBlendInput()
        item.set_editor_property('layer_name',name)
        item.set_editor_property('blend_type',unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
        item.set_editor_property('preview_weight',1. if name=='AutoLayer' else 0.)
        layers.append(item)
    blend.set_editor_property('layers',layers)
    for name,src in zip(names,(automatic,grass,grass,gravel,dirt,rock)):
        link(src,'' if src==automatic else 'RGB',blend,'Layer '+name)
    tone=custom(mat,'float d=dot(normalize(N),normalize(L));float3 shade=d<.05?float3(.59,.66,.76):(d<.42?float3(.82,.86,.91):1);return C*shade;',
        {'C':(blend,''),'N':(node(mat,unreal.MaterialExpressionVertexNormalWS),''),
         'L':(color(mat,'ArtLightDirection',art_direction()),'RGB')},
        desc='Three broad slope tones over original painted layers. Matte lit ground receives tree and rock shadows.')
    assert EDIT.connect_material_property(tone,'',unreal.MaterialProperty.MP_BASE_COLOR)
    for p in ('Normal','Roughness','Specular'):unreal.MaterialNodeService.disconnect_output(path(mat),p)
    rough=scalar(mat,'GuLi_Roughness',1.)
    assert EDIT.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    zero=node(mat,unreal.MaterialExpressionConstant,r=0.)
    assert EDIT.connect_material_property(zero,'',unreal.MaterialProperty.MP_SPECULAR)
    EDIT.layout_material_expressions(mat);EDIT.recompile_material(mat);save(mat)
    mi=unreal.load_asset(BASE+'/Materials/Landscape/MI_Landscape');EDIT.update_material_instance(mi);save(mi)
    return {'success':True,'map_geometry_and_paint_weights_preserved':True,'ground_receives_scene_shadows':True}


def grass_mesh():
    guard()
    mesh=unreal.load_asset(BASE+'/Meshes/Grass/SM_Grass')
    sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    build=sub.get_lod_build_settings(mesh,0)
    before=str(build)
    build.set_editor_property('build_scale3d',unreal.Vector(1.35,1.35,.6))
    sub.set_lod_build_settings(mesh,0,build)
    mesh.set_editor_property('positive_bounds_extension',unreal.Vector(2,2,1))
    mesh.set_editor_property('negative_bounds_extension',unreal.Vector(2,2,1))
    save(mesh)
    types=[]
    for name in ('LGT_Auto','LGT_Placeable'):
        obj=unreal.load_asset(BASE+'/Misc/LandscapeGrassTypes/'+name)
        varieties=list(obj.get_editor_property('grass_varieties'))
        for v in varieties:
            v.set_editor_property('grass_density',unreal.PerPlatformFloat(default=550.))
            v.set_editor_property('cast_contact_shadow',False)
            v.set_editor_property('cast_dynamic_shadow',False)
        obj.set_editor_property('grass_varieties',varieties);save(obj)
        types.append({'path':path(obj),'settings':[str(v) for v in obj.get_editor_property('grass_varieties')]})
    report={'success':True,'build_before':before,'build_after':str(sub.get_lod_build_settings(mesh,0)),
        'triangles':mesh.get_num_triangles(0),'bounds':str(mesh.get_bounds()),'types':types,
        'painted_foliage_instances_and_actor_transforms':'unchanged','grass_distribution':'Original landscape layers retained; auto density 1000 -> 550'}
    write('grass_changes.json',report)
    return report


def environment_finish():
    world=guard()
    sky=next(a for a in ACTORS.get_all_level_actors() if a.get_name()=='SkySphere_0')
    sky_values={'Colors determined by sun position':False,'Cloud opacity':.32,'Sun brightness':18.,
        'Zenith Color':unreal.LinearColor(.075,.24,.37,1),
        'Horizon color':unreal.LinearColor(.32,.49,.55,1),
        'Cloud color':unreal.LinearColor(.73,.79,.76,1),'Horizon Falloff':3.}
    before={k:str(sky.get_editor_property(k)) for k in sky_values}
    for k,v in sky_values.items():sky.set_editor_property(k,v)
    sky.call_method('RefreshMaterial')
    pp=next(a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.PostProcessVolume))
    settings=pp.get_editor_property('settings')
    pp_values={'override_color_contrast':True,'color_contrast':unreal.Vector4(1,1,1,1),
        'override_white_tint':True,'white_tint':0.,'override_film_shoulder':False,'override_film_white_clip':False,
        'override_vignette_intensity':True,'vignette_intensity':.08,
        'override_motion_blur_amount':True,'motion_blur_amount':0.,
        'override_ambient_occlusion_intensity':True,'ambient_occlusion_intensity':.35}
    pp_before={k:str(settings.get_editor_property(k)) for k in pp_values}
    for k,v in pp_values.items():settings.set_editor_property(k,v)
    pp.set_editor_property('settings',settings)
    # All original decal geometry/UVs remain; adapt the emissive graphic's palette and sharpness.
    decal=unreal.load_asset(BASE+'/Decals/M_Glyph')
    if LIB.get_metadata_tag(decal,'GuLi.Style.Owner')!=OWNER:
        alpha=EDIT.get_material_property_input_node(decal,unreal.MaterialProperty.MP_OPACITY)
        shape=custom(decal,'return smoothstep(.4,.6,A);',{'A':(alpha,'')},True,
            desc='Clean source glyph silhouette; original UV shape retained.')
        assert EDIT.connect_material_property(shape,'',unreal.MaterialProperty.MP_OPACITY)
        EDIT.recompile_material(decal);save(decal)
    mi=unreal.load_asset(BASE+'/Decals/MI_Glyph')
    EDIT.set_material_instance_vector_parameter_value(mi,'Color',unreal.LinearColor(.19,.63,.69,1))
    EDIT.set_material_instance_scalar_parameter_value(mi,'Emissiveness',.8)
    EDIT.update_material_instance(mi);save(mi)
    grass=unreal.load_asset(BASE+'/Materials/Grass/MI_Grass')
    set_palette(grass,((.185,.32,.089),(.145,.265,.078),(.108,.21,.075)))
    EDIT.set_material_instance_scalar_parameter_value(grass,'RootShade',.30)
    EDIT.update_material_instance(grass);save(grass)
    LIB.set_metadata_tag(world,'GuLi.Style.Owner',OWNER)
    LIB.set_metadata_tag(world,'GuLi.Style.Source','Byte copy of supplied Demo_Map; layout not recreated')
    assert LEVEL.save_current_level()
    # Preview-only AA; do not silently alter project RendererSettings.
    rendering_before={k:unreal.SystemLibrary.get_console_variable_int_value(k) for k in ('r.AntiAliasingMethod','r.ScreenPercentage')}
    unreal.SystemLibrary.execute_console_command(world,'r.AntiAliasingMethod 2')
    unreal.SystemLibrary.execute_console_command(world,'r.ScreenPercentage 100')
    write('environment_changes.json',{'sky_before':before,'sky_after':{k:str(sky.get_editor_property(k)) for k in sky_values},
        'pp_before':pp_before,'pp_after':{k:str(pp.settings.get_editor_property(k)) for k in pp_values},
        'preview_console_before':rendering_before,'preview_console_after':{'r.AntiAliasingMethod':2,'r.ScreenPercentage':100},
        'project_config_changed':False,'sun_and_actor_transforms':'unchanged'})
    return {'success':True,'dirty':dirty()}


def lod_batch():
    guard()
    records_file=OUT/'mesh_lods.json'
    records=json.loads(records_file.read_text(encoding='utf-8')) if records_file.is_file() else []
    completed={r['path'] for r in records}
    source=[r for r in json.loads((OUT/'original_assets.json').read_text(encoding='utf-8')) if r['class']=='StaticMesh']
    todo=[r for r in source if r['path'] not in completed][:8]
    subsystem=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for row in todo:
        mesh=unreal.load_asset(row['path'])
        small='/Grass/' in row['path'] or '/Flowers/' in row['path']
        options=unreal.StaticMeshReductionOptions()
        options.set_editor_property('auto_compute_lod_screen_size',False)
        settings=[]
        for fraction,size in zip((1.,.7,.45),(1.,.035 if small else .16,.012 if small else .055)):
            setting=unreal.StaticMeshReductionSettings()
            setting.set_editor_property('percent_triangles',fraction)
            setting.set_editor_property('screen_size',size)
            settings.append(setting)
        options.set_editor_property('reduction_settings',settings)
        result=subsystem.set_lods(mesh,options)
        assert mesh.get_num_lods()==3,(path(mesh),result)
        if small:
            mesh.set_editor_property('positive_bounds_extension',unreal.Vector(3,3,2))
            mesh.set_editor_property('negative_bounds_extension',unreal.Vector(3,3,2))
        save(mesh)
        after=[mesh.get_num_triangles(i) for i in range(3)]
        assert after[0]==row['triangles'][0],(path(mesh),row['triangles'],after)
        records.append({'path':path(mesh),'before':row['triangles'],'after':after,
            'screen_sizes':[1.,.035 if small else .16,.012 if small else .055]})
        write('mesh_lods.json',records)
    return {'success':True,'completed':len(records),'total':len(source),'last':records[-len(todo):] if todo else []}


VIEWS={
    '03_final_entry':{'location':[18727.628906,-2725.349609,1265.167847],'rotation':[-12.436695,-167.277588,0],'fov':90},
    '04_overview':{'location':[29000,-33000,24500],'target':[0,0,800],'fov':55},
    '05_top':{'location':[0,0,76000],'rotation':[-90,0,0],'fov':55},
    '06_wood_stone':{'location':[6100,-2400,2500],'target':[2400,1500,500],'fov':55},
    '07_understory':{'location':[16000,6800,2300],'target':[15700,7900,500],'fov':55},
    '08_glyph':{'location':[17700,-14300,3650],'target':[19548,-15143,3009],'fov':45},
}
for meters in (200,800,1800):
    arm=meters*100
    VIEWS['09_commander_'+str(meters)+'m']={'location':[-arm*math.cos(math.radians(55)),0,600+arm*math.sin(math.radians(55))],
        'target':[0,0,600],'fov':45,'arm_m':meters,'pitch_degrees':55}


def camera():
    cams=[a for a in ACTORS.get_all_level_actors() if a.get_actor_label()=='GuLiDemo_ReviewCamera']
    if cams:return cams[0]
    cam=ACTORS.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(0,0,1000))
    cam.set_actor_label('GuLiDemo_ReviewCamera')
    cam.set_folder_path('GuLiReview')
    cam.set_editor_property('is_editor_only_actor',True)
    return cam


def set_view(name):
    guard()
    cam=camera();v=VIEWS[name]
    pos=unreal.Vector(*v['location'])
    if 'target' in v:rot=unreal.MathLibrary.find_look_at_rotation(pos,unreal.Vector(*v['target']))
    else:rot=unreal.Rotator(pitch=v['rotation'][0],yaw=v['rotation'][1],roll=v['rotation'][2])
    cam.set_actor_location(pos,False,False);cam.set_actor_rotation(rot,False)
    cam.get_component_by_class(unreal.CameraComponent).set_field_of_view(v['fov'])
    EDITOR.set_level_viewport_camera_info(pos,rot)
    unreal.ViewportService.set_game_view(True);unreal.ViewportService.set_realtime(True)
    unreal.ViewportService.set_exposure(True,0.)
    ACTORS.set_selected_level_actors([])
    return cam


def gallery():
    global DEMO_GALLERY_HANDLE,DEMO_GALLERY_STATE,DEMO_SHOT
    guard()
    assert not globals().get('DEMO_GALLERY_HANDLE')
    names=list(VIEWS)
    DEMO_GALLERY_STATE={'index':0,'phase':'camera','images':[],'started':time.monotonic(),'last':time.monotonic()}
    def tick(dt):
        global DEMO_GALLERY_HANDLE,DEMO_SHOT
        s=DEMO_GALLERY_STATE
        try:
            if time.monotonic()-s['started']>300:raise RuntimeError('Gallery time limit')
            if s['index']>=len(names):
                h=DEMO_GALLERY_HANDLE;DEMO_GALLERY_HANDLE=None
                unreal.unregister_slate_post_tick_callback(h)
                set_view('03_final_entry')
                assert LEVEL.save_current_level()
                write('gallery.json',{'success':True,'images':s['images'],'AA':'TAA (preview override; project config unchanged)'})
                write('camera_presets.json',VIEWS)
                return
            name=names[s['index']]
            if s['phase']=='camera':
                set_view(name);s.update(phase='settle',last=time.monotonic())
            elif s['phase']=='settle' and time.monotonic()-s['last']>4.:
                DEMO_SHOT=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(OUT/'Previews'/(name+'.png')),camera(),delay=1.)
                s.update(phase='capture',last=time.monotonic())
            elif s['phase']=='capture' and DEMO_SHOT.is_task_done():
                p=OUT/'Previews'/(name+'.png')
                assert p.is_file() and p.stat().st_size>10000,str(p)
                s['images'].append(str(p));s.update(phase='camera',index=s['index']+1)
        except Exception:
            write('gallery.json',{'success':False,'state':s,'traceback':traceback.format_exc()})
            if DEMO_GALLERY_HANDLE:unreal.unregister_slate_post_tick_callback(DEMO_GALLERY_HANDLE)
            DEMO_GALLERY_HANDLE=None
    DEMO_GALLERY_HANDLE=unreal.register_slate_post_tick_callback(tick)
    return {'success':True,'queued':names}


def audit(recompile_diagnostics=False, report_name='readback.json'):
    guard()
    source=json.loads((OUT/'original_scene.json').read_text(encoding='utf-8'))
    scene_snapshot('adapted_scene.json')
    latest=json.loads((OUT/'adapted_scene.json').read_text(encoding='utf-8'))
    current={a['name']:a for a in latest['actors']}
    differences=[];placed=0
    for row in source['actors']:
        after=current.get(row['name'])
        if not after:differences.append({'missing_actor':row['name']});continue
        for key in ('class','location','rotation','scale'):
            if row[key]!=after[key]:differences.append({'actor':row['name'],'key':key,'before':row[key],'after':after[key]})
        byname={c['name']:c for c in after['components']}
        for comp in row['components']:
            if comp['name'] not in byname:differences.append({'missing_component':comp['name']});continue
            c=byname[comp['name']]
            for key in ('mesh','instances','instance_transform_sha256'):
                if key in comp and comp[key]!=c.get(key):differences.append({'actor':row['name'],'component':comp['name'],'key':key})
            if 'instances' in comp:placed+=comp['instances']
    rows=json.loads((OUT/'original_assets.json').read_text(encoding='utf-8'))
    checked=[];unadapted=[]
    for row in rows:
        if row['class'] not in ('StaticMesh','Material','MaterialInstanceConstant','LandscapeGrassType'):continue
        obj=unreal.load_asset(row['path'])
        adapted=LIB.get_metadata_tag(obj,'GuLi.Style.Owner')==OWNER
        if not adapted:unadapted.append(row['path'])
        info={'path':row['path'],'class':row['class'],'adapted':adapted}
        if isinstance(obj,unreal.StaticMesh):
            info['lod_triangles']=[obj.get_num_triangles(i) for i in range(obj.get_num_lods())]
            info['materials']=[path(s.material_interface) for s in obj.static_materials]
        checked.append(info)
    diagnostics={}
    # VibeUE's diagnostics forces rendering recompilation. Never use it as a
    # routine read-only audit; the original compile evidence is already saved.
    if recompile_diagnostics:
        for row in rows:
            if row['class']=='Material':
                d=unreal.MaterialNodeService.get_material_diagnostics(row['path'])
                diagnostics[row['path']]={'available':d.success,'compiled_ok':d.is_compiled_ok,
                    'errors':str(d.compile_errors),'referenced_textures':list(d.referenced_texture_paths)}
    report={'success':not differences and not unadapted,'layout_differences':differences,'unadapted':unadapted,
        'original_actor_count':len(source['actors']),'current_actor_count':len(latest['actors']),
        'original_placed_foliage_instances':placed,'assets':checked,'material_diagnostics':diagnostics,
        'shader_recompile_diagnostics_performed':recompile_diagnostics,
        'dirty':dirty(),'user_visual_approval':'v1 approved; full Demo_Map pending',
        'performance':'GPU and draw calls not measured; LOD counts are geometry accounting only',
        'source_meshes':'Existing supplied UE meshes; no invented Blender/FBX sources'}
    write(report_name,report)
    return {k:v for k,v in report.items() if k not in ('assets','material_diagnostics')}


def final_readback():
    assert not globals().get('DEMO_GALLERY_HANDLE')
    result=audit(report_name='final_readback.json')
    report=json.loads((OUT/'final_readback.json').read_text(encoding='utf-8'))
    sky=next(a for a in ACTORS.get_all_level_actors() if a.get_name()=='SkySphere_0')
    report['sky_saved_values']={k:str(sky.get_editor_property(k)) for k in
        ('Zenith Color','Horizon color','Cloud opacity','Sun brightness')}
    report['session_console_values']={k:unreal.SystemLibrary.get_console_variable_int_value(k) for k in
        ('r.AntiAliasingMethod','r.ScreenPercentage','r.ParallelVelocity')}
    report['previous_compile_evidence']='readback.json; not recompiled in this final layout audit'
    report['editor_stability']='Supplemental gallery succeeded in default parallel rendering; earlier crash root cause unresolved'
    write('final_readback.json',report)
    return result


def save_and_reload():
    world=guard()
    assert not globals().get('DEMO_GALLERY_HANDLE')
    # Source environment blueprints are dirtied by UE's mesh rebuild/reference refresh.
    # They are backed-up package-local assets, never gameplay blueprints.
    for suffix in ('BP_Floating_Rock_Dark','BP_Floating_Rock_Light','BP_Spinning_Rock'):
        bp=unreal.load_asset(BASE+'/Blueprints/'+suffix)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        save(bp)
    assert not dirty()['content'],dirty()
    set_view('03_final_entry')
    LIB.set_metadata_tag(world,'GuLi.Style.Owner',OWNER)
    assert LEVEL.save_current_level()
    assert not any(dirty().values()),dirty()
    assert LEVEL.load_level(PACK+'/Maps/Overview_Map')
    assert LEVEL.load_level(MAP)
    assert path(EDITOR.get_editor_world()).split('.')[0]==MAP
    sky=next(a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.SkyLight))
    sky.light_component.recapture_sky()
    return {'success':True,'reopened_saved_original_demo_map':True,'dirty':dirty(),
        'sky_zenith_saved':str(next(a for a in ACTORS.get_all_level_actors() if a.get_name()=='SkySphere_0').get_editor_property('Zenith Color'))}


def dispatch(action):
    try:
        result=globals()[action]()
    except Exception:
        result={'success':False,'traceback':traceback.format_exc()}
        write('error_'+action+'.json',result)
    unreal.MCPythonHelper.submit_result(json.dumps(result,ensure_ascii=False))
