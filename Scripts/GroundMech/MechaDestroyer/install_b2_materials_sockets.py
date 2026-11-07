import unreal,json,math
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export')
BASE='/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2';LIB=unreal.EditorAssetLibrary;E=unreal.MaterialEditingLibrary
mesh=unreal.load_asset(BASE+'/Meshes/SK_GroundMech_A4B2')
PALETTE={'Cream':'FEE4D9','Sand':'D5C09C','Mechanism':'2C3735','Ink':'1A182F','Amber':'EE9D58','TeamPrimary':'6AA4BE','TeamSecondary':'274E61','Outline':'1A182F'}
FACTORS={'TeamPrimary':[(.12,.20,.24),(.38,.88,1),(.86,1.30,1.35)],'TeamSecondary':[(.42,.50,.56),(.90,1,1.12),(1.60,1.75,1.9)],'Mechanism':[(.60,.65,.75),(1.70,1.65,1.90),(2.70,2.60,2.95)],'Cream':[(.75,.70,.61),(1.04,1.02,.93),(1.21,1.18,1.08)],'Sand':[(.54,.53,.48),(.96,.95,.90),(1.24,1.22,1.14)]}
def linear(h):
    c=[int(h[i:i+2],16)/255 for i in (0,2,4)]
    return unreal.LinearColor(*[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in c],1)
def save(o):
    LIB.set_metadata_tag(o,'GuLi.ModelProduction.Owner','GuLi.GroundMech.ApprovedA4.B2');assert LIB.save_loaded_asset(o,False)
def expressions(material,kind):
    return [e for e in unreal.ObjectIterator(kind) if e.get_path_name().startswith(material.get_path_name()+':')]

def install_leg_bands(material,role,custom,new_graph=False):
    def factor(values):
        return 'float3('+','.join(map(str,values))+')'
    tones=[factor(f) for f in FACTORS[role]]
    if role=='TeamPrimary':
        red=[(.42,.38,.43),(.96,.88,.94),(1.38,1.34,1.37)]
        tones=[f'lerp({blue},{factor(tint)},step(C.b,C.r))' for blue,tint in zip(tones,red)]
    code=f"""float L=dot(normalize(N),normalize(float3(-.40,.62,.68)))*.5+.5;
float3 dark={tones[0]}, mid={tones[1]}, light={tones[2]};
if (PartAlpha >= .5 || LegBandHalfWidth <= 0)
    return C * (L < .38 ? dark : (L < .89 ? mid : light));
float w=clamp(LegBandHalfWidth, .0001, .1);
float lower=smoothstep(.38-w, .38+w, L);
float upper=smoothstep(.89-w, .89+w, L);
return C * lerp(lerp(dark,mid,lower),light,upper);"""
    names=[str(v.get_editor_property('input_name')) for v in custom.get_editor_property('inputs')]
    assert names in (['C','N'],['C','N','PartAlpha','LegBandHalfWidth']),names
    vertices=[] if new_graph else expressions(material,unreal.MaterialExpressionVertexColor)
    widths=[] if new_graph else [e for e in expressions(material,unreal.MaterialExpressionScalarParameter)
            if str(e.get_editor_property('parameter_name'))=='LegBandHalfWidth']
    assert len(vertices)<=1 and len(widths)<=1
    if custom.get_editor_property('code')==code and len(names)==4 and vertices and widths:
        linked=list(E.get_inputs_for_material_expression(material,custom))
        if linked[2:]==[vertices[0],widths[0]] and E.get_input_node_output_name_for_material_expression(custom,vertices[0])=='A':
            return False
    material.modify();custom.modify()
    if len(names)==2:
        inputs=list(custom.get_editor_property('inputs'))
        for key in ('PartAlpha','LegBandHalfWidth'):
            pin=unreal.CustomInput();pin.set_editor_property('input_name',key);inputs.append(pin)
        custom.set_editor_property('inputs',inputs)
    vertex=vertices[0] if vertices else E.create_material_expression(material,unreal.MaterialExpressionVertexColor,-430,490)
    if widths:
        width=widths[0]
    else:
        width=E.create_material_expression(material,unreal.MaterialExpressionScalarParameter,-430,630)
        width.set_editor_property('parameter_name','LegBandHalfWidth')
        width.set_editor_property('default_value',.05)
        width.set_editor_property('slider_min',0.)
        width.set_editor_property('slider_max',.1)
    custom.set_editor_property('description','Three toon bands; rigid leg pieces blend only at the band edges')
    custom.set_editor_property('code',code)
    assert E.connect_material_expressions(vertex,'A',custom,'PartAlpha')
    assert E.connect_material_expressions(width,'',custom,'LegBandHalfWidth')
    return True

leg_bands_only=globals().get('GULI_A4B2_LEG_BANDS_ONLY',False)
if leg_bands_only:
    assert mesh.has_vertex_colors(),'Import the masked A4B2 mesh before updating its leg materials'
materials={}
for role,color in PALETTE.items():
    name='M_A4B2_'+role;p=BASE+'/Materials/'+name
    if leg_bands_only:
        if role in FACTORS:
            m=unreal.load_asset(p)
            nodes=expressions(m,unreal.MaterialExpressionCustom)
            assert len(nodes)==1,p
            if install_leg_bands(m,role,nodes[0]):
                E.recompile_material(m)
                assert unreal.MaterialService.compile_material(p),p
                save(m)
            materials[role]=m
        continue
    m=unreal.load_asset(p) if LIB.does_asset_exist(p) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    E.delete_all_material_expressions(m)
    m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);m.set_editor_property('two_sided',False);m.set_editor_property('used_with_skeletal_mesh',True)
    base=E.create_material_expression(m,unreal.MaterialExpressionVectorParameter,-700,0);base.set_editor_property('parameter_name','BaseColor');base.set_editor_property('default_value',linear(color))
    output=base;outpin='RGB'
    if role.startswith('Team'):
        team=E.create_material_expression(m,unreal.MaterialExpressionVectorParameter,-700,160);team.set_editor_property('parameter_name','GuLi_'+role);team.set_editor_property('default_value',linear(color));team.set_editor_property('use_custom_primitive_data',True);team.set_editor_property('primitive_data_index',8 if role=='TeamPrimary' else 12)
        enabled=E.create_material_expression(m,unreal.MaterialExpressionScalarParameter,-700,300);enabled.set_editor_property('parameter_name','GuLi_TeamEnabled');enabled.set_editor_property('default_value',0);enabled.set_editor_property('use_custom_primitive_data',True);enabled.set_editor_property('primitive_data_index',16)
        mix=E.create_material_expression(m,unreal.MaterialExpressionLinearInterpolate,-400,40)
        assert E.connect_material_expressions(base,'RGB',mix,'A') and E.connect_material_expressions(team,'RGB',mix,'B') and E.connect_material_expressions(enabled,'',mix,'Alpha')
        output=mix;outpin=''
    if role in FACTORS:
        n=E.create_material_expression(m,unreal.MaterialExpressionPixelNormalWS,-400,-170)
        custom=E.create_material_expression(m,unreal.MaterialExpressionCustom,0,0);custom.set_editor_property('description','Exactly three toon bands; approved palette and color factors')
        custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        inputs=[]
        for key in ['C','N']:
            i=unreal.CustomInput();i.set_editor_property('input_name',key);inputs.append(i)
        custom.set_editor_property('inputs',inputs)
        install_leg_bands(m,role,custom,new_graph=True)
        assert E.connect_material_expressions(output,outpin,custom,'C') and E.connect_material_expressions(n,'',custom,'N')
        output=custom;outpin=''
    assert E.connect_material_property(output,outpin,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    E.recompile_material(m);save(m);materials[role]=m

def finish_full_install(materials):
    slots=list(mesh.materials)
    for slot in slots:
        role=str(slot.material_slot_name).removeprefix('M_');slot.set_editor_property('material_interface',materials[role])
    mesh.set_editor_property('materials',slots)
    bones={str(b.bone_name):b for b in unreal.SkeletonService.list_bones(mesh.get_path_name())}
    sockets=[]
    for spec in json.loads((OUT.parent/'geometry_report.json').read_text())['sockets']:
        p=unreal.Vector(spec['position'][0]*100,-spec['position'][1]*100,spec['position'][2]*100)
        direction=unreal.Vector(spec['direction'][0],-spec['direction'][1],spec['direction'][2])
        bone=bones[spec['bone']].global_transform
        loc=bone.inverse_transform_location(p)
        local_direction=bone.rotation.inversed().rotate_vector(direction)
        rotation=unreal.MathLibrary.conv_vector_to_rotator(local_direction)
        if not mesh.find_socket(spec['name']):
            assert unreal.SkeletonService.add_socket(mesh.get_path_name(),spec['name'],spec['bone'],loc,rotation,unreal.Vector(1,1,1),False)
        else:
            assert unreal.SkeletonService.set_socket_transform(mesh.get_path_name(),spec['name'],loc,rotation,unreal.Vector(1,1,1))
        s=mesh.find_socket(spec['name']);actual=bone.transform_location(s.relative_location)
        error=(actual-p).length();assert error<.01
        axis=bone.rotation.rotate_vector(s.relative_rotation.quaternion().rotate_vector(unreal.Vector(1,0,0)))
        assert axis.dot(direction)>.9999
        sockets.append({'name':spec['name'],'bone':spec['bone'],'position_cm':list(actual.to_tuple()),'forward':list(axis.to_tuple()),'position_error_cm':error})
    save(mesh)
    report={'success':True,'materials':{k:v.get_path_name() for k,v in materials.items()},'tone_bands':3,'sockets':sockets,'team_driver':'Existing local team system CPD 8 / 12 / 16; no hue changes to Cream/Sand/Mechanism/Ink/Amber','visual_verification':'User'}
    (OUT/'ue_material_socket_install.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return report

if leg_bands_only:
    report={'success':True,'roles':sorted(materials),'default_half_width':.05,'thresholds':[.38,.89]}
else:
    report=finish_full_install(materials)
unreal.MCPythonHelper.submit_result(json.dumps(report))
