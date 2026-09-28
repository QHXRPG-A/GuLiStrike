"""Build the owned six-depth cel card materials. Run through Scripts/ue_exec.py.

Marketplace materials are read/copy sources only. No map switching or PIE here.
"""
import json
import traceback
from pathlib import Path
import unreal

ROOT = '/Game/GuLiStrike/Cards/WarMachineTarot'
SOURCE = '/Game/Assets/card/ParallaxCardMaterial'
FILES = Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards')
ART = FILES / 'Cel_Closeups_v3'
REPORT = {'success': False, 'textures': [], 'materials': []}
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def expr(material, cls, x=0, y=0, **values):
    node = MEL.create_material_expression(material, cls, x, y)
    for key, value in values.items():
        node.set_editor_property(key, value)
    return node


def custom(material, code, names, output=unreal.CustomMaterialOutputType.CMOT_FLOAT4, x=0, y=0):
    node = expr(material, unreal.MaterialExpressionCustom, x, y, code=code, output_type=output)
    inputs = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    node.set_editor_property('inputs', inputs)
    return node


def link(source, output, target, pin):
    ok=MEL.connect_material_expressions(source, output, target, pin)
    if not ok and pin in ['Input','Coordinates']:ok=MEL.connect_material_expressions(source, output, target, '')
    assert ok, pin


def finish(material):
    MEL.recompile_material(material)
    assert EAL.save_loaded_asset(material, False), material.get_name()


def import_texture(filename, name):
    task = unreal.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = ROOT + '/Textures'
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(ROOT + '/Textures/' + name)
    assert texture, filename
    texture.set_editor_property('srgb', True)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_BC7)
    texture.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property('never_stream', True)
    assert EAL.save_loaded_asset(texture, False)
    REPORT['textures'].append({'path': texture.get_path_name(), 'size': [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()], 'alpha': True})
    return texture


def make_parallax():
    path = ROOT + '/Materials/M_CelCardParallax'
    material = unreal.load_asset(path)
    if not material:
        material = EAL.duplicate_asset(SOURCE + '/Materials/M_Card_Demo', path)
    assert material
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    # Keep the source animated pattern network, but give each atlas cell its own
    # bounded UV and alpha. Source FlipBook/BumpOffset can sample adjacent cells.
    nodes = [n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer() == material]
    for old in [n for n in nodes if str(n.get_editor_property('desc')).startswith('WM_LAYER_')]:
        MEL.delete_material_expression(material,old)
    nodes = [n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer() == material]
    def at(cls, x, y):
        found = [n for n in nodes if isinstance(n, cls) and n.material_expression_editor_x == x and n.material_expression_editor_y == y]
        assert len(found) == 1, (str(cls), x, y, len(found))
        return found[0]
    near = at(unreal.MaterialExpressionTextureSample, -1936, 48)
    action = at(unreal.MaterialExpressionTextureSample, -1920, 352)
    mask_destination = at(unreal.MaterialExpressionReroute, -3328, 2896)
    samples=[near,action]+[at(unreal.MaterialExpressionTextureSample,-1936,y) for y in [576,800,1024,1232]]
    def owned(cls,x,y,**kw):
        n=expr(material,cls,x,y,**kw)
        n.set_editor_property('desc','WM_LAYER_'+str(x)+'_'+str(y))
        return n
    uv=owned(unreal.MaterialExpressionTextureCoordinate,-7200,-1000)
    depth=next(n for n in nodes if isinstance(n,unreal.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name'))=='Layers global depth')
    alpha=[]
    for i,sample in enumerate(samples):
        y=-1000+i*300
        d=owned(unreal.MaterialExpressionScalarParameter,-7200,y+80,parameter_name=f'Layer {i+1} Depth',default_value=[0,-.16,-.16,-.45,-.75,-1.10][i])
        amount=owned(unreal.MaterialExpressionMultiply,-6980,y+80)
        link(d,'',amount,'A');link(depth,'',amount,'B')
        bump=owned(unreal.MaterialExpressionBumpOffset,-6800,y,height_ratio=.05,reference_plane=0.0)
        link(uv,'',bump,'Coordinate');link(amount,'',bump,'Height')
        offset=owned(unreal.MaterialExpressionVectorParameter,-7200,y+160,parameter_name=f'Layer {i+1} Offset',default_value=unreal.LinearColor(0,0,0,0))
        scale=owned(unreal.MaterialExpressionVectorParameter,-7000,y+160,parameter_name=f'Layer {i+1} Scale',default_value=unreal.LinearColor(1,1,1,1))
        safe=custom(material,f'''float2 p=(UV-0.5-Offset.rg)/max(Scale.rg,float2(0.01,0.01))+0.5;
float valid=step(0.002,p.x)*step(p.x,0.998)*step(0.002,p.y)*step(p.y,0.998);
p=clamp(p,float2(0.0072,0.0048),float2(0.9928,0.9952));
return float3((p+float2({i%3},{i//3}))/float2(3.0,2.0),valid);''',['UV','Offset','Scale'],unreal.CustomMaterialOutputType.CMOT_FLOAT3,x=-6500,y=y)
        safe.set_editor_property('desc',f'WM_LAYER_{i+1}_BoundedUV')
        link(bump,'',safe,'UV');link(offset,'RGB',safe,'Offset');link(scale,'RGB',safe,'Scale')
        rg=owned(unreal.MaterialExpressionComponentMask,-6250,y,r=True,g=True,b=False,a=False)
        bounds=owned(unreal.MaterialExpressionComponentMask,-6250,y+100,r=False,g=False,b=True,a=False)
        link(safe,'',rg,'Input');link(safe,'',bounds,'Input');link(rg,'',sample,'Coordinates')
        opacity=owned(unreal.MaterialExpressionScalarParameter,-6250,y+180,parameter_name=f'Layer {i+1} Opacity',default_value=1.0)
        a=custom(material,'return A*Inside*Opacity;',['A','Inside','Opacity'],unreal.CustomMaterialOutputType.CMOT_FLOAT1,x=-6000,y=y)
        a.set_editor_property('desc',f'WM_LAYER_{i+1}_Alpha')
        link(sample,'A',a,'A');link(bounds,'',a,'Inside');link(opacity,'',a,'Opacity')
        alpha.append(a)
    behind=owned(unreal.MaterialExpressionScalarParameter,-5500,500,parameter_name='Ability behind machinery',default_value=0.)
    nearbehind=owned(unreal.MaterialExpressionScalarParameter,-5500,650,parameter_name='Foreground behind machinery',default_value=0.)
    composite=custom(material,'''float3 c=Color5;
c=lerp(c,Color4,Alpha4);
c=lerp(c,Color3,Alpha3);
float3 front=lerp(lerp(c,Color2,Alpha2),Color1,Alpha1);
float3 back=lerp(lerp(c,Color1,Alpha1),Color2,Alpha2);
c=lerp(front,back,saturate(Behind));
return lerp(c,Color0,Alpha0*(1.0-Alpha2*saturate(NearBehind)));''',[v for i in range(6) for v in [f'Color{i}',f'Alpha{i}']]+['Behind','NearBehind'],unreal.CustomMaterialOutputType.CMOT_FLOAT3,x=-5100,y=0)
    composite.set_editor_property('desc','WM_LAYER_Composite')
    for i in range(6):
        link(samples[i],'RGB',composite,f'Color{i}');link(alpha[i],'',composite,f'Alpha{i}')
    link(behind,'',composite,'Behind')
    link(nearbehind,'',composite,'NearBehind')
    base=at(unreal.MaterialExpressionReroute,112,144)
    link(composite,'',base,'')
    # Emissive's base-artwork reroute is identified from the original graph.
    source_graph=json.loads((FILES/'Inspection'/'M_Card_Demo.json').read_text(encoding='utf-8'))
    info=source_graph['expressions'][153]
    emission_base=at(unreal.MaterialExpressionReroute,info['pos_x'],info['pos_y'])
    link(composite,'',emission_base,'')
    for old in [n for n in nodes if isinstance(n, unreal.MaterialExpressionCustom) and n.material_expression_editor_x == -3900]:
        MEL.delete_material_expression(material, old)
    mask = custom(material, '''float n = NearAlpha * (1.0-BodyAlpha*NearBehind) * saturate((max(max(NearColor.r,NearColor.g),NearColor.b)-0.55)*2.22);
float a = ActionAlpha * (1.0-BodyAlpha*Behind) * saturate((max(max(ActionColor.r,ActionColor.g),ActionColor.b)-0.45)*1.82);
return float4(n,a*(1.0-NearAlpha),0.0,0.0);''', ['NearColor', 'NearAlpha', 'ActionColor', 'ActionAlpha','BodyAlpha','Behind','NearBehind'], x=-3900, y=2400)
    mask.set_editor_property('desc', 'Local highlights follow the same shifted atlas samples as the illustration')
    for node, output, pin in [(near, 'RGB', 'NearColor'), (alpha[0], '', 'NearAlpha'), (action, 'RGB', 'ActionColor'), (alpha[1], '', 'ActionAlpha')]:
        link(node, output, mask, pin)
    link(alpha[2],'',mask,'BodyAlpha');link(behind,'',mask,'Behind');link(nearbehind,'',mask,'NearBehind')
    link(mask, '', mask_destination, '')
    finish(material)
    (FILES/'Inspection'/'M_CelCardParallax.json').write_text(unreal.MaterialNodeService.export_material_graph(path), encoding='utf-8')
    return material


def make_ui(texture):
    path = ROOT + '/Materials/M_CelCardUI'
    material = unreal.load_asset(path)
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_CelCardUI', ROOT+'/Materials', unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(material)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property('two_sided', False)
    uv = expr(material, unreal.MaterialExpressionTextureCoordinate, -600, 0)
    tex = expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -600, 200, parameter_name='UI Map', texture=texture)
    start = expr(material, unreal.MaterialExpressionScalarParameter, -600, 400, parameter_name='Text panel start', default_value=0.775)
    mask = custom(material, '''float edge = max(max(step(UV.x,0.018),step(0.982,UV.x)),step(UV.y,0.010));
float corners = max(step(UV.x+UV.y*1.5,0.080),step((1.0-UV.x)+UV.y*1.5,0.080));
return max(max(edge,corners),step(PanelStart,UV.y));''', ['UV', 'PanelStart'], unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    link(uv, '', mask, 'UV')
    link(start, '', mask, 'PanelStart')
    assert MEL.connect_material_property(tex, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert MEL.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    finish(material)
    return material


def instance(name, parent):
    path = ROOT+'/Materials/'+name
    material = unreal.load_asset(path)
    if not material:
        result = unreal.MaterialService.create_instance(parent.get_path_name(), name, ROOT+'/Materials')
        material = unreal.load_asset(path)
        assert material, str(result)
    return material


try:
    assert not unreal.WidgetService.is_pie_running(), 'Finish the dedicated preview before editing assets'
    textures = {}
    frame=import_texture(ART/'EmptyCardFrame.png','T_CelCardFrame')
    for name in ['FireRate', 'MissileDamage', 'HighSpeed']:
        atlas=ART/'Layers'/'Corrected'/(name+'_Atlas_Mechanical.png')
        if not atlas.is_file():atlas=ART/'Layers'/'Corrected'/(name+'_Atlas.png')
        if not atlas.is_file():atlas=ART/'Layers'/(name+'_Atlas.png')
        textures[name] = (import_texture(atlas, 'T_'+name+'_Layers'),frame)
    parallax = make_parallax()
    ui = make_ui(textures['FireRate'][1])
    for name, tint in [('FireRate',(1.0,0.56,0.12)), ('MissileDamage',(1.0,0.40,0.08)), ('HighSpeed',(0.10,0.65,1.0))]:
        front = instance('MI_'+name, parallax)
        text = instance('MI_'+name+'_UI', ui)
        assert unreal.MaterialService.set_instance_texture_parameter(front.get_path_name(), 'BaseColor Map', textures[name][0].get_path_name())
        assert unreal.MaterialService.set_instance_texture_parameter(text.get_path_name(), 'UI Map', textures[name][1].get_path_name())
        scalars = {'Layers global depth':1.0, 'Layers global scale':1.0, 'Scale 2nd layer':0.0,
                   'Scale 3rd layer':0.0, 'Scale 4th layer':0.0, 'Scale 5th layer':0.0, 'Scale 6th layer':-0.10,
                   'Specular':0.0, 'Roughness':1.0}
        scalars['Ability behind machinery']=1.0 if name in ['HighSpeed','MissileDamage'] else 0.0
        scalars['Foreground behind machinery']=1.0 if name=='HighSpeed' else 0.0
        scalars['Layer 1 Opacity']=0.35 if name=='MissileDamage' else 1.0
        for channel in 'RGBA':
            scalars['Detail emissive exponent ('+channel+' mask)'] = 0.08 if channel in 'RG' else 0.0
        for key, value in scalars.items():
            assert unreal.MaterialService.set_instance_scalar_parameter(front.get_path_name(), key, value), key
        for channel in 'RG':
            assert unreal.MaterialService.set_instance_vector_parameter(front.get_path_name(), 'Detail emissive color ('+channel+' mask)', *tint, 1.0)
        layouts={1:((0,0),(1,1)),2:((0,0),(1,1)),3:((0,0),(1,1)),4:((0,0),(1,1)),5:((0,0),(1,1)),6:((0,0),(1.12,1.12))}
        if name=='FireRate':layouts[2]=((.35,.025),(1,1))
        if name=='MissileDamage':layouts[2]=((-.16,-.10),(.65,.65))
        if name=='HighSpeed':
            layouts[2]=((.17,-.06),(1,1))
            layouts[3]=((0,-.06),(1,1))
        for i,(offset,scale) in layouts.items():
            assert unreal.MaterialService.set_instance_vector_parameter(front.get_path_name(),f'Layer {i} Offset',*offset,0.,0.)
            assert unreal.MaterialService.set_instance_vector_parameter(front.get_path_name(),f'Layer {i} Scale',*scale,1.,1.)
        MEL.update_material_instance(front)
        MEL.update_material_instance(text)
        assert EAL.save_loaded_asset(front, False)
        assert EAL.save_loaded_asset(text, False)
        REPORT['materials'].append({'name':name, 'front':front.get_path_name(), 'text':text.get_path_name(), 'layers':6, 'depth':1.0,'layout':layouts})
    REPORT['success'] = True
except Exception:
    REPORT['error'] = traceback.format_exc()
(FILES/'Inspection'/'owned-materials.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(REPORT,ensure_ascii=False))
