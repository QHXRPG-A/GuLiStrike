"""Shared rigid WPO function, regular cel/contour materials, and state overlays.

The original approved materials are preserved. Only new WPO derivatives and the
generic overlay graphs are edited; generic overlay Kind=0 keeps other units still.
"""
import json,traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/MechanicalAnimation_20260929'
EDIT=unreal.MaterialEditingLibrary;LIB=unreal.EditorAssetLibrary
DEST='/Game/Commander/Units/MechanicalAnimation'
FN_PATH=DEST+'/MF_GuLiRigidMechanical'
REPORT={'success':False,'materials':[]}

def node(owner,cls,**props):
    n=EDIT.create_material_expression_in_function(owner,cls) if isinstance(owner,unreal.MaterialFunction) else EDIT.create_material_expression(owner,cls)
    for k,v in props.items():n.set_editor_property(k,v)
    return n
def link(a,b,pin='',out=''):
    assert EDIT.connect_material_expressions(a,out,b,pin),(a.get_name(),b.get_name(),pin,out)
def world_vector(owner,source,out=''):
    n=node(owner,unreal.MaterialExpressionTransform,transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
           transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(source,n,'',out);return n

def make_function():
    fn=unreal.load_asset(FN_PATH)
    version=LIB.get_metadata_tag(fn,'GuLi.RigidWPO') if fn else ''
    if version=='v3':return fn
    if version=='v1':
        # Preserve input/output GUIDs: rebuilding these would disconnect every material caller.
        expressions=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==fn]
        custom=next(n for n in expressions if isinstance(n,unreal.MaterialExpressionCustom))
        for n in expressions:
            if isinstance(n,unreal.MaterialExpressionPerInstanceCustomData) and n.get_editor_property('data_index')>=12:
                n.set_editor_property('data_index',n.get_editor_property('data_index')+3)
        pins=list(custom.get_editor_property('inputs'))
        for i in range(11,14):
            pin=unreal.CustomInput();pin.set_editor_property('input_name','V'+str(i));pins.append(pin)
        custom.set_editor_property('inputs',pins)
        for i in range(11,14):
            current=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=1+i,const_default_value=0)
            previous=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=15+i,const_default_value=0)
            switch=node(fn,unreal.MaterialExpressionPreviousFrameSwitch)
            link(current,switch,'Current Frame');link(previous,switch,'Previous Frame');link(switch,custom,'V'+str(i))
        # Complete the v3 pin migration before compiling the current HLSL.
        version='v2'
    if version=='v2':
        expressions=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==fn]
        custom=next(n for n in expressions if isinstance(n,unreal.MaterialExpressionCustom))
        pins=list(custom.get_editor_property('inputs'))
        assert 'PodVisible' not in [str(p.get_editor_property('input_name')) for p in pins]
        pin=unreal.CustomInput();pin.set_editor_property('input_name','PodVisible');pins.append(pin)
        custom.set_editor_property('inputs',pins)
        current=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=29,const_default_value=1)
        previous=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=30,const_default_value=1)
        switch=node(fn,unreal.MaterialExpressionPreviousFrameSwitch)
        link(current,switch,'Current Frame');link(previous,switch,'Previous Frame');link(switch,custom,'PodVisible')
        custom.set_editor_property('code',(ROOT/'Scripts/Materials/GuLiRigidMechanical.hlsl').read_text())
        EDIT.update_material_function(fn);LIB.set_metadata_tag(fn,'GuLi.RigidWPO','v3');assert LIB.save_loaded_asset(fn,False)
        return fn
    if fn:
        # UE 5.7 removes from its expression array while iterating it. Repeat
        # until empty so a repaired build cannot retain orphan function inputs.
        while EDIT.get_num_material_expressions_in_function(fn):EDIT.delete_all_material_expressions_in_function(fn)
    else:fn=unreal.AssetToolsHelpers.get_asset_tools().create_asset('MF_GuLiRigidMechanical',DEST,unreal.MaterialFunction,unreal.MaterialFunctionFactoryNew())
    assert fn
    kind=node(fn,unreal.MaterialExpressionFunctionInput,input_name='Kind',input_type=unreal.FunctionInputType.FUNCTION_INPUT_SCALAR)
    upper=node(fn,unreal.MaterialExpressionFunctionInput,input_name='UpperPivot',input_type=unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3)
    inputs={'Kind':kind,'UpperPivot':upper,'Position':node(fn,unreal.MaterialExpressionPreSkinnedPosition),
        'LocalNormal':node(fn,unreal.MaterialExpressionPreSkinnedNormal),
        'PivotXY':node(fn,unreal.MaterialExpressionTextureCoordinate,coordinate_index=1),
        'PivotZPart':node(fn,unreal.MaterialExpressionTextureCoordinate,coordinate_index=2)}
    for i in range(14):
        current=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=1+i,const_default_value=0)
        previous=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=15+i,const_default_value=0)
        switch=node(fn,unreal.MaterialExpressionPreviousFrameSwitch)
        link(current,switch,'Current Frame');link(previous,switch,'Previous Frame');inputs['V'+str(i)]=switch
    current=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=29,const_default_value=1)
    previous=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=30,const_default_value=1)
    switch=node(fn,unreal.MaterialExpressionPreviousFrameSwitch)
    link(current,switch,'Current Frame');link(previous,switch,'Previous Frame');inputs['PodVisible']=switch
    custom=node(fn,unreal.MaterialExpressionCustom,code=(ROOT/'Scripts/Materials/GuLiRigidMechanical.hlsl').read_text(),
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,desc='Rigid local offset + rotated local normal')
    pins=[]
    for n in inputs:
        pin=unreal.CustomInput();pin.set_editor_property('input_name',n);pins.append(pin)
    custom.set_editor_property('inputs',pins)
    output_pin=unreal.CustomOutput();output_pin.set_editor_property('output_name','AnimatedNormal');output_pin.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('additional_outputs',[output_pin])
    for n,src in inputs.items():link(src,custom,n)
    for priority,(name,out) in enumerate([('Offset',''),('Normal','AnimatedNormal')]):
        transformed=world_vector(fn,custom,out)
        output=node(fn,unreal.MaterialExpressionFunctionOutput,output_name=name,sort_priority=priority)
        link(transformed,output,'')
    EDIT.update_material_function(fn);LIB.set_metadata_tag(fn,'GuLi.RigidWPO','v3');assert LIB.save_loaded_asset(fn,False)
    return fn

def attach(mat,fn,profile=0,upper=(0,0,0)):
    version=LIB.get_metadata_tag(mat,'GuLi.RigidWPO')
    if version=='v3':return
    if version in ('v1','v2'):
        EDIT.recompile_material(mat)
        LIB.set_metadata_tag(mat,'GuLi.RigidWPO','v3');assert LIB.save_loaded_asset(mat,False)
        return
    graph=json.loads(unreal.MaterialNodeService.export_material_graph(mat.get_path_name()))
    call=node(mat,unreal.MaterialExpressionMaterialFunctionCall);assert call.set_material_function(fn)
    kind=node(mat,unreal.MaterialExpressionScalarParameter,parameter_name='RigidKind',default_value=profile,group='Rigid Animation')
    pivot=node(mat,unreal.MaterialExpressionVectorParameter,parameter_name='RigidUpperPivot',default_value=unreal.LinearColor(*upper,0),group='Rigid Animation')
    link(kind,call,'Kind');link(pivot,call,'UpperPivot','RGB')
    old=EDIT.get_material_property_input_node(mat,unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    if old:
        total=node(mat,unreal.MaterialExpressionAdd);link(call,total,'A','Offset');link(old,total,'B')
        assert EDIT.connect_material_property(total,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    else: assert EDIT.connect_material_property(call,'Offset',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    # Evaluate rigid normals on vertices, interpolate into the pixel shader. Using
    # PixelNormalWS alone would leave toon bands frozen while the turret rotates.
    normal=node(mat,unreal.MaterialExpressionVertexInterpolator);link(call,normal,'','Normal')
    normal_ids={n['id'] for n in graph['expressions'] if n['class'] in ['PixelNormalWS','VertexNormalWS']}
    fresh=json.loads(unreal.MaterialNodeService.export_material_graph(mat.get_path_name()))
    original_ids={n['id'] for n in graph['expressions']}
    interp=next(n['id'] for n in fresh['expressions'] if n['class']=='VertexInterpolator' and n['id'] not in original_ids)
    for c in graph['connections']:
        if c['source_id'] in normal_ids:
            assert unreal.MaterialNodeService.connect_expressions(mat.get_path_name(),interp,'',c['target_id'],c['target_input'])
    # Lit wrecks need the world-space normal as well as the original rusty shading.
    if mat.get_editor_property('shading_model')!=unreal.MaterialShadingModel.MSM_UNLIT:
        mat.set_editor_property('tangent_space_normal',False)
        assert EDIT.connect_material_property(normal,'',unreal.MaterialProperty.MP_NORMAL)
    mat.set_editor_property('used_with_instanced_static_meshes',True)
    mat.set_editor_property('max_world_position_offset_displacement',0)
    EDIT.recompile_material(mat)
    LIB.set_metadata_tag(mat,'GuLi.RigidWPO','v3');assert LIB.save_loaded_asset(mat,False)

try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    fn=make_function()
    for unit in globals().get('GULI_RIGID_UNITS',['WarMachine','Sweeper']):
        spec=json.loads((OUT/unit/'manifest.json').read_text(encoding='utf-8'))
        base='/Game/Commander/Units/Tactical/Cel/'+unit
        original=unreal.load_asset(base+'/Meshes/SM_'+unit+'_Cel')
        mesh=unreal.load_asset(base+'/Meshes/SM_'+unit+'_Rigid');assert mesh
        for index,slot in enumerate(original.static_materials):
            source=slot.material_interface
            target=DEST+'/'+source.get_name()+'_Rigid'
            mat=unreal.load_asset(target) if LIB.does_asset_exist(target) else LIB.duplicate_asset(source.get_path_name(),target)
            attach(mat,fn,spec['profile'],spec['upper_pivot_cm']);mesh.set_material(index,mat)
            REPORT['materials'].append(target)
        assert LIB.save_loaded_asset(mesh,False)
    for path in ['/Game/GuLiStrike/FX/UnitFeedback/M_UnitWreckRust','/Game/GuLiStrike/FX/UnitFeedback/M_UnitHitWhite_Instanced','/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportBody']:
        mat=unreal.load_asset(path);assert mat;attach(mat,fn);REPORT['materials'].append(path)
    REPORT['success']=True
except:REPORT['error']=traceback.format_exc()
(ROOT/'ArtSource/WarMachineHover_20260929/ue-materials.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(REPORT))
