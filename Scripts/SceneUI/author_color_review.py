"""Author isolated palette materials; never edit source meshes or gameplay references."""
import hashlib
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT/'ArtSource/LocalTeamColorReview_20261008'
BASE = '/Game/GuLiStrike/Review/LocalTeamColor_20261008'
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
OWNER = 'LocalTeamColorReview.20261008.v1'
REPORT = {'success': False, 'runtime_integrated': False, 'models': [], 'source_assets_modified': False}

def expressions(material):
    EDIT.get_num_material_expressions(material) # Hydrate lazily loaded editor graph data after a save/reopen.
    return [x for x in unreal.ObjectIterator(unreal.MaterialExpression)
            if x.get_path_name().startswith(material.get_path_name()+':')]

def code_of(expression):
    return expression.get_editor_property('code') if isinstance(expression,unreal.MaterialExpressionCustom) else ''

def save(asset):
    assert asset.get_path_name().startswith(BASE+'/')
    LIB.set_metadata_tag(asset,'GuLi.Owner',OWNER)
    assert LIB.save_loaded_asset(asset,False), asset.get_path_name()

def copy(source,target):
    asset = unreal.load_asset(target) if LIB.does_asset_exist(target) else None
    if asset:
        assert LIB.get_metadata_tag(asset,'GuLi.Owner')==OWNER
    else:
        asset=LIB.duplicate_asset(source,target)
    assert asset, (source,target)
    LIB.set_metadata_tag(asset,'GuLi.SourceAsset',source)
    save(asset)
    return asset

def node(mat,cls,**props):
    result=EDIT.create_material_expression(mat,cls)
    for key,value in props.items(): result.set_editor_property(key,value)
    return result

def pin(name):
    value=unreal.CustomInput(); value.set_editor_property('input_name',name); return value

def rgb(code,linear=True):
    values=[int(code[i:i+2],16)/255. for i in (1,3,5)]
    return [(x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4) for x in values] if linear else values

def vec(values): return unreal.LinearColor(*values,1)

def base_material(material):
    while isinstance(material,unreal.MaterialInstance): material=material.get_editor_property('parent')
    return material

def color_mask(input_name,codes,linear=True):
    parts=[]
    for code in codes:
        color=','.join(f'{x:.9f}' for x in rgb(code,linear))
        parts.append(f'(1-step(0.025,length({input_name}.rgb-float3({color}))))')
        if linear:
            encoded=','.join(f'{x:.9f}' for x in rgb(code,False))
            parts.append(f'(1-step(0.025,length({input_name}.rgb-float3({encoded}))))')
    return 'saturate('+ '+'.join(parts)+')'

def decorate_custom(mat,mode,codes):
    nodes=expressions(mat)
    teams=[n for n in nodes if isinstance(n,unreal.MaterialExpressionVectorParameter) and str(n.get_editor_property('parameter_name'))=='ReviewTeamColor']
    marks=[n for n in nodes if isinstance(n,unreal.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name'))=='ReviewRegionMark']
    team=teams[0] if teams else node(mat,unreal.MaterialExpressionVectorParameter,parameter_name='ReviewTeamColor',default_value=vec(rgb('#2877DB',mode!='Palette')))
    mark=marks[0] if marks else node(mat,unreal.MaterialExpressionScalarParameter,parameter_name='ReviewRegionMark',default_value=0)
    count=0
    for expression in nodes:
        if not isinstance(expression,unreal.MaterialExpressionCustom): continue
        inputs=list(expression.get_editor_property('inputs'))
        names=[str(p.get_editor_property('input_name')) for p in inputs]
        if mode not in names: continue
        code=expression.get_editor_property('code')
        if 'GuLiMutableRegion' in code:
            original_start={'Color':'float light=','Base':'float d=','Palette':'float3 s=','Team':'float code='}[mode]
            assert original_start in code,(mat.get_name(),mode,code)
            code=code[code.index(original_start):]
        mask='step(.2,(1.0-BT.y)-floor(1.0-BT.y+.001))' if mode=='Team' else color_mask(mode,codes,mode!='Palette')
        # Texture atlases include compressed/painted shades of the same authored color block.
        # These bounded hue families exclude warm-white, gray structure and gold function colors.
        if mode=='Color':
            mask='saturate(step(Color.r*1.4,Color.b)*step(Color.r*1.2,Color.g)*step(Color.g*1.05,Color.b)+step(Color.g*1.6,Color.r)*step(Color.b*1.7,Color.r)*step(Color.b*.85,Color.g)*(1-step(Color.b*1.6,Color.g)))'
        if mode=='Base':
            mask='step(Base.g*2.0,Base.r)*step(Base.b*1.5,Base.g)*step(Base.b*3.0,Base.r)*step(.025,Base.r)'
        expression.set_editor_property('inputs',inputs+[pin(n) for n in ('ReviewTeamColor','ReviewRegionMark') if n not in names])
        prefix=f'float GuLiMutableRegion={mask}; {mode}=lerp({mode},ReviewTeamColor,GuLiMutableRegion); '
        yellow='float3(1,.65,.015)' if mode!='Palette' else 'float3(1,.85,.13)'
        expression.set_editor_property('code',prefix+f'{mode}=lerp({mode},{yellow},GuLiMutableRegion*step(.5,ReviewRegionMark)); '+code)
        assert EDIT.connect_material_expressions(team,'RGB',expression,'ReviewTeamColor')
        assert EDIT.connect_material_expressions(mark,'',expression,'ReviewRegionMark')
        count+=1
    for duplicate in teams[1:]+marks[1:]: EDIT.delete_material_expression(mat,duplicate)
    assert count or any('GuLiMutableRegion' in code_of(x) for x in expressions(mat)), (mat.get_name(),mode)
    return mode=='Palette'

def decorate_pbr(mat,mode,codes,mesh):
    existing=next((x for x in expressions(mat) if 'float3 C=lerp(Original,ReviewTeamColor' in code_of(x)),None)
    if existing:
        if mode=='palette':
            old=code_of(existing)
            existing.set_editor_property('code',f'float GuLiMutableRegion={color_mask("Original",codes)};'+old.split(';',1)[1])
            # Recreate only the annotation helper so it follows this revised palette mask.
            marker=next((x for x in expressions(mat) if isinstance(x,unreal.MaterialExpressionCustom) and
                         'return GuLiMutableRegion*step(.5,ReviewRegionMark)' in code_of(x)),None)
            if marker:marker.set_editor_property('code',code_of(existing).split(';',1)[0]+'; return GuLiMutableRegion*step(.5,ReviewRegionMark);')
        decorate_region_emission(mat,existing)
        return False
    source=EDIT.get_material_property_input_node(mat,unreal.MaterialProperty.MP_BASE_COLOR)
    output=EDIT.get_material_property_input_node_output_name(mat,unreal.MaterialProperty.MP_BASE_COLOR)
    attribute=None
    if not source:
        for candidate in expressions(mat):
            if isinstance(candidate,unreal.MaterialExpressionMakeMaterialAttributes):
                names=EDIT.get_material_expression_input_names(candidate)
                linked=EDIT.get_inputs_for_material_expression(mat,candidate)
                if 'BaseColor' in names and names.index('BaseColor')<len(linked):
                    source=linked[names.index('BaseColor')]; attribute=candidate; output=''
                    break
    if not source: source=node(mat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.5,.5,.5,1))
    team=node(mat,unreal.MaterialExpressionVectorParameter,parameter_name='ReviewTeamColor',default_value=vec(rgb('#2877DB')))
    mark=node(mat,unreal.MaterialExpressionScalarParameter,parameter_name='ReviewRegionMark',default_value=0)
    custom=node(mat,unreal.MaterialExpressionCustom,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs=[pin('Original'),pin('ReviewTeamColor'),pin('ReviewRegionMark')]
    mask='1.0' if mode=='slot' else color_mask('Original',codes)
    if mode=='band':
        bounds=mesh.get_bounds(); origin=bounds.origin; extent=bounds.box_extent
        # A narrow designation band on an existing placeholder/armor slot, not a whole-model tint.
        zcenter=origin.z+extent.z*.05
        mask=f'(1-step({max(1.,extent.z*.11):.6f},abs(LocalPosition.z-{zcenter:.6f})))'
        world=node(mat,unreal.MaterialExpressionWorldPosition)
        local=node(mat,unreal.MaterialExpressionTransformPosition,
                   transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                   transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
        assert EDIT.connect_material_expressions(world,'',local,'')
        inputs.append(pin('LocalPosition'))
    custom.set_editor_property('inputs',inputs)
    custom.set_editor_property('code',f'float GuLiMutableRegion={mask}; float3 C=lerp(Original,ReviewTeamColor,GuLiMutableRegion); return lerp(C,float3(1,.65,.015),GuLiMutableRegion*step(.5,ReviewRegionMark));')
    for n,o,p in ((source,output,'Original'),(team,'RGB','ReviewTeamColor'),(mark,'','ReviewRegionMark')):
        assert EDIT.connect_material_expressions(n,o,custom,p)
    if mode=='band': assert EDIT.connect_material_expressions(local,'',custom,'LocalPosition')
    if attribute: assert EDIT.connect_material_expressions(custom,'',attribute,'BaseColor')
    else: assert EDIT.connect_material_property(custom,'',unreal.MaterialProperty.MP_BASE_COLOR)
    decorate_region_emission(mat,custom,attribute)
    return False

def decorate_region_emission(mat,remap,attribute=None):
    """Only the yellow annotation variant exposes the mask independently of PBR metal lighting."""
    if LIB.get_metadata_tag(mat,'GuLi.Review.RegionAnnotation')=='EmissiveMask':return
    if attribute is None:
        attribute=next((x for x in expressions(mat) if isinstance(x,unreal.MaterialExpressionMakeMaterialAttributes)
                        and remap in EDIT.get_inputs_for_material_expression(mat,x)),None)
    names=[str(p.get_editor_property('input_name')) for p in remap.get_editor_property('inputs')]
    sources=list(EDIT.get_inputs_for_material_expression(mat,remap))
    mask=node(mat,unreal.MaterialExpressionCustom,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    mask.set_editor_property('inputs',[pin(n) for n in names])
    mask.set_editor_property('code',code_of(remap).split(';',1)[0]+'; return GuLiMutableRegion*step(.5,ReviewRegionMark);')
    for name,source in zip(names,sources):
        if source:assert EDIT.connect_material_expressions(source,'',mask,name)
    original=EDIT.get_material_property_input_node(mat,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    output=EDIT.get_material_property_input_node_output_name(mat,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if attribute:
        input_names=EDIT.get_material_expression_input_names(attribute)
        linked=EDIT.get_inputs_for_material_expression(mat,attribute)
        if 'EmissiveColor' in input_names and input_names.index('EmissiveColor')<len(linked):original=linked[input_names.index('EmissiveColor')];output=''
    if not original:original=node(mat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(0,0,0,1))
    display=node(mat,unreal.MaterialExpressionCustom,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                 code='return lerp(OriginalEmission,float3(1,.65,.015),RegionMask);')
    display.set_editor_property('inputs',[pin('OriginalEmission'),pin('RegionMask')])
    assert EDIT.connect_material_expressions(original,output,display,'OriginalEmission')
    assert EDIT.connect_material_expressions(mask,'',display,'RegionMask')
    if attribute:assert EDIT.connect_material_expressions(display,'',attribute,'EmissiveColor')
    else:assert EDIT.connect_material_property(display,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    LIB.set_metadata_tag(mat,'GuLi.Review.RegionAnnotation','EmissiveMask')

PROFILES={
 'DefaultSoldier':('Color',['#87CEEB','#E97868'],'现有天蓝及珊瑚装甲色块；灯具、暖白和底盘固定'),
 'WM01':('Base',['#DD6038','#AC452B'],'橙色装甲及其暗橙分区；机械蓝灰、暖白、黄灯及线稿固定'),
 'SweeperSummon':('Base',['#DD6038'],'橙色侧甲；车轮、机枪、蓝灰机械结构、黄灯固定，保留无线稿例外'),
 'BiZhiMao':('Palette',['#557B78','#8E3A2A'],'青绿装甲与砖红标识；深灰结构、米色固定'),
 'BiZhiMaoConstruction':('Palette',['#557B78','#8E3A2A'],'沿用彼之矛装甲和标识分区，建造体结构固定'),
 'ShieldGenerator':('palette',['#22676C','#853B37'],'青绿装甲和砖红标识；青色功能发光、灰白结构固定'),
 'SentryTurret':('palette',['#48907A','#37775F','#47816B'],'原绿色彩绘甲片候选；灰色机构和炮口固定'),
 'MissileTurret':('slot',[],'仅 MetalPlate / OrangeMetalPlate 装甲槽；线缆、镜片、危险条纹和结构固定'),
 'ManualOutpost':('band',[],'无现成队色标记：候选为混凝土侧面窄阵营条带，混凝土主体固定'),
 'BasicBarracks':('band',[],'当前玩法占位 Cube：仅窄阵营条带，未制作新建筑模型'),
 'TerritoryStronghold':('band',[],'当前玩法占位 Cylinder：仅窄阵营条带，未制作新建筑模型'),
 'ResourceFactory':('band',[],'实际工厂蓝图 Body 的 Armor 材质窄条带；门、室内、细部与坡道固定')
}

def author_model(model):
    key=model['name']; folder=BASE+'/'+key
    if model['group']=='SSF': profile=('Team',[],'复用现有 UV4 队色标记/Accent 分区；主装甲、结构、线稿、平台、灯具和无人机固定')
    else: profile=PROFILES[key]
    mode,codes,description=profile
    materials=[]; palette_srgb={}; derived={}
    mesh=unreal.load_asset(model['mesh'])
    for slot in model['slots']:
        material=unreal.load_asset(slot['material']['path']) if slot['material'] else None
        mutable=material is not None and 'Contour' not in slot['name'] and 'Outline' not in slot['name']
        if key=='MissileTurret': mutable=slot['name'] in ('MetalPlate','OrangeMetalPlate')
        if key=='ResourceFactory': mutable=material is not None and material.get_name()=='M_RPF_Armor'
        if key.startswith('SSF_'): mutable=slot['index']==0
        if not mutable:
            materials.append({'slot':slot['index'],'name':slot['name'],'fixed':True,'source':material.get_path_name() if material else None})
            continue
        source_base=base_material(material)
        signature=source_base.get_path_name()
        if signature not in derived:
            parent=copy(signature,folder+'/M_Review_'+source_base.get_name())
            if mode in ('Color','Base','Palette','Team'): srgb=decorate_custom(parent,mode,codes)
            else: srgb=decorate_pbr(parent,mode,codes,mesh)
            if key=='SentryTurret' and LIB.get_metadata_tag(parent,'GuLi.Review.DiffuseInput')!='OriginalTexture':
                # The imported legacy parent has zero diffuse weights. Read the already assigned
                # source texture in this review copy so its green armor can actually be marked.
                sample=node(parent,unreal.MaterialExpressionTextureSampleParameter2D,
                            parameter_name='DiffuseColorMap',texture=EDIT.get_material_instance_texture_parameter_value(material,'DiffuseColorMap'))
                target=next(x for x in expressions(parent) if 'float3 C=lerp(Original,ReviewTeamColor' in code_of(x))
                assert EDIT.connect_material_expressions(sample,'RGB',target,'Original')
                LIB.set_metadata_tag(parent,'GuLi.Review.DiffuseInput','OriginalTexture')
            EDIT.recompile_material(parent);save(parent);derived[signature]=(parent,srgb)
        parent,srgb=derived[signature]
        entry={'slot':slot['index'],'name':slot['name'],'fixed':False,'source':material.get_path_name(),'derived_parent':parent.get_path_name()}
        for variant,color in (('Blue','#2877DB'),('Red','#D7534D'),('Regions','#2877DB')):
            target=folder+'/MI_'+variant+'_'+str(slot['index'])
            if isinstance(material,unreal.MaterialInstanceConstant): inst=copy(material.get_path_name(),target)
            else:
                inst=unreal.load_asset(target) if LIB.does_asset_exist(target) else TOOLS.create_asset(target.rsplit('/',1)[1],folder,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
                LIB.set_metadata_tag(inst,'GuLi.Owner',OWNER)
            EDIT.set_material_instance_parent(inst,parent)
            EDIT.set_material_instance_vector_parameter_value(inst,'ReviewTeamColor',vec(rgb(color,not srgb)))
            EDIT.set_material_instance_scalar_parameter_value(inst,'ReviewRegionMark',1 if variant=='Regions' else 0)
            EDIT.update_material_instance(inst);save(inst);entry[variant.lower()]=inst.get_path_name()
        materials.append(entry)
    config=dict(model)
    config.pop('slots')
    config.update(version=OWNER,mutable_regions=description,source_palette_candidates=codes,
                  blue_reference='#2877DB',red_reference='#D7534D',enemy_non_blue_palette='#D7534D',
                  configuration_status='candidate_pending_region_review',runtime_enabled=False,
                  geometry_animation_lod_unchanged=True,materials=materials)
    if key=='SentryTurret': config['preview_only_repair']='Imported legacy diffuse weights are zero; the isolated parent reads the existing DiffuseColorMap. Original assets are unchanged.'
    (OUT/'Configs').mkdir(exist_ok=True)
    (OUT/'Configs'/(key+'.json')).write_text(json.dumps(config,ensure_ascii=False,indent=2),encoding='utf8')
    REPORT['models'].append(config)

try:
    assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None
    source=json.loads((OUT/'source-readback.json').read_text(encoding='utf8'))
    assert source['success'] and len(source['models'])==18
    models=source['models']
    # The gameplay factory's displayed mesh is the blueprint assembly, not its catalog Cube fallback.
    factory=next(m for m in models if m['name']=='ResourceFactory')
    factory['mesh']='/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Meshes/SM_RPF_Body.SM_RPF_Body'
    factory['kind']='StaticMesh'
    real=unreal.load_asset(factory['mesh'])
    factory['slots']=[]
    for i,slot in enumerate(real.get_editor_property('static_materials')):
        mat=slot.get_editor_property('material_interface')
        factory['slots'].append({'index':i,'name':str(slot.get_editor_property('material_slot_name')),'material':{'path':mat.get_path_name()}})
    # Only the Armor slot can receive the designation candidate.
    for model in models:
        author_model(model)
        (OUT/'asset-authoring.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
    REPORT['success']=True
except Exception:
    REPORT['error']=traceback.format_exc()
(OUT/'asset-authoring.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
unreal.log(str({'success':REPORT['success'],'models':len(REPORT['models']),'error':REPORT.get('error')}))
