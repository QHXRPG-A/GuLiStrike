"""Blend the approved phase shader into copies of every production body shader.

At BlinkStrength=0 the copied original graph is the exact endpoint, including
team overrides and outline WPO. Only project-owned generated copies are rebuilt.
"""
import json
import re
import sys
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(ROOT/'Scripts/GroundMech/Blink'))
import importlib
import author_blink_body_material as shape_shader
importlib.reload(shape_shader)
COLOR,MASK,OFFSET=shape_shader.COLOR,shape_shader.MASK,shape_shader.OFFSET

FOLDER='/Game/GuLiStrike/FX/GroundMech/Blink/Materials'
DEFINITION='/Game/GuLiStrike/FX/GroundMech/Blink/DA_GroundMech_BlinkVfx'
OUT=ROOT/'ArtSource/FX/GroundMech/Blink_20261027/Shape_E_v1'


def bind_definition(bindings):
    cls=unreal.load_class(None,'/Script/GuLiStrike.GuLiGroundMechBlinkVfxDefinition')
    if not cls:
        return {'bound':False,'reason':'native_class_not_loaded'}
    obj=unreal.load_asset(DEFINITION) if unreal.EditorAssetLibrary.does_asset_exist(DEFINITION) else None
    if not obj:
        factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',cls)
        folder,name=DEFINITION.rsplit('/',1)
        obj=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,cls,factory)
    values=[]
    variant_type=unreal.GuLiGroundMechBlinkMaterialVariant
    for item in bindings:
        variant=variant_type()
        variant.set_editor_property('source',unreal.load_asset(item['source']))
        variant.set_editor_property('phase',unreal.load_asset(item['phase']))
        values.append(variant)
    obj.set_editor_property('body_materials',values)
    if not unreal.EditorAssetLibrary.save_asset(DEFINITION,only_if_is_dirty=True):
        raise RuntimeError('Blink material-definition save failed')
    return {'bound':True,'path':DEFINITION,'variants':len(obj.get_editor_property('body_materials'))}


def main(bind_only=False):
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError('Stop PIE before authoring phase material variants')
    OUT.mkdir(parents=True,exist_ok=True)
    path=OUT/'material_variants_readback.json'
    if bind_only:
        report=json.loads(path.read_text(encoding='utf8'))
        report['definition']=bind_definition(report['variants'])
        if not report['definition']['bound']:
            raise RuntimeError(report['definition'])
    else:
        rows=json.loads((ROOT/'data/Json/DT_GuLiStrikeMech_Skills.json').read_text(encoding='utf8'))
        tuning=next(r for r in rows if r['Name']=='Blink')
        cls=unreal.load_class(None,'/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light_C')
        mesh=unreal.get_default_object(cls).get_editor_property('mesh')
        lib=unreal.MaterialEditingLibrary
        report={'variants':[],'usage':'independent_moving_energy_proxy_only','original_mesh_materials_untouched':True,'reference_approval':'approved','visual_approval':'pending_player'}
        for source in mesh.get_materials():
            if not isinstance(source,unreal.Material) or not source.get_path_name().startswith('/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Materials/'):
                raise RuntimeError('Unexpected production shader; refusing generic recolor')
            target=FOLDER+'/'+source.get_name()+'_BlinkPhase'
            if unreal.EditorAssetLibrary.does_asset_exist(target):
                if not unreal.EditorAssetLibrary.delete_asset(target):
                    raise RuntimeError('Cannot rebuild owned phase shader: '+target)
            material=unreal.EditorAssetLibrary.duplicate_asset(source.get_path_name(),target)
            if not material:raise RuntimeError('Shader duplication failed')
            properties=[unreal.MaterialProperty.MP_EMISSIVE_COLOR,unreal.MaterialProperty.MP_OPACITY_MASK,
                        unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET,unreal.MaterialProperty.MP_BASE_COLOR]
            originals={p:(lib.get_material_property_input_node(material,p),lib.get_material_property_input_node_output_name(material,p)) for p in properties}
            inputs={}
            def node(key,kind):
                obj=lib.create_material_expression(material,kind,-1600,80*len(inputs))
                obj.set_editor_property('desc','Blink3C: '+key);inputs[key]=obj;return obj
            def connect(a,b,pin,output=''):
                if not lib.connect_material_expressions(a,output or '',b,pin):raise RuntimeError('Shader connection: '+pin)
            def scalar(key,name,value):
                obj=node(key,unreal.MaterialExpressionScalarParameter)
                obj.set_editor_property('parameter_name',name);obj.set_editor_property('default_value',float(value));return obj
            raw_p=node('PVertex',unreal.MaterialExpressionPreSkinnedPosition)
            interp=node('P',unreal.MaterialExpressionVertexInterpolator);connect(raw_p,interp,'VS')
            raw_n=node('NrmVertex',unreal.MaterialExpressionVertexNormalWS)
            interp=node('Nrm',unreal.MaterialExpressionVertexInterpolator);connect(raw_n,interp,'VS')
            node('Now',unreal.MaterialExpressionTime);node('View',unreal.MaterialExpressionCameraVectorWS)
            scalar('Strength','BlinkStrength',1);scalar('StartAt','BlinkStartTime',0);scalar('ProxyFade','BlinkProxyFade',1)
            scalar('FlickerHz','BlinkFlickerHz',tuning['BlinkShapeFlickerHz'])
            scalar('JitterCm','BlinkFragmentOffsetCm',tuning['BlinkShapeFragmentOffsetCm'])
            scalar('Emission','BlinkBodyEmission',tuning['BlinkEnergyEmission'])
            scalar('ArmorLight','BlinkArmorLight',1 if 'Cream' in source.get_name() else 0)
            scalar('AccentLight','BlinkAccentLight',1 if 'Amber' in source.get_name() else 0)
            scalar('EdgeSolid','BlinkEdgeSolid',1 if 'Outline' in source.get_name() else 0)
            for key,name,value in [('EnergyTint','EnergyTint',(.03,1,.008,1)),('DashDirection','BlinkDirection',(1,0,0,0))]:
                obj=node(key,unreal.MaterialExpressionVectorParameter)
                obj.set_editor_property('parameter_name',name);obj.set_editor_property('default_value',unreal.LinearColor(*value))
            old_light,old_pin=originals[unreal.MaterialProperty.MP_EMISSIVE_COLOR]
            if not old_light:old_light=node('OriginalBlack',unreal.MaterialExpressionConstant3Vector);old_pin=''
            inputs['SourceColor']=old_light
            color=re.sub(r'float3 base = [^;]+;','float3 base = SourceColor.rgb;',COLOR)
            common=['P','Now','StartAt','Strength','FlickerHz']
            built={}
            for label,code,names,kind in [('Light',color,common+['Nrm','View','Emission','SourceColor','EnergyTint','ProxyFade','ArmorLight','AccentLight','EdgeSolid'],unreal.CustomMaterialOutputType.CMOT_FLOAT3),
                ('Mask',MASK,common+['ProxyFade'],unreal.CustomMaterialOutputType.CMOT_FLOAT1),
                ('Offset',OFFSET,common+['JitterCm','DashDirection','Nrm'],unreal.CustomMaterialOutputType.CMOT_FLOAT3)]:
                obj=node(label,unreal.MaterialExpressionCustom);obj.set_editor_property('code',code);obj.set_editor_property('output_type',kind)
                pins=[]
                for name in names:
                    pin=unreal.CustomInput();pin.set_editor_property('InputName',name);pins.append(pin)
                obj.set_editor_property('inputs',pins)
                for name in names:
                    expr=inputs[name+'Vertex'] if label=='Offset' and name in ('P','Nrm') else inputs[name]
                    connect(expr,obj,name,old_pin if name=='SourceColor' else '')
                built[label]=obj
            def output(expr,prop):
                if not lib.connect_material_property(expr,'',prop):raise RuntimeError('Shader property output failed')
            output(built['Light'],unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            for prop,label,kind in [(unreal.MaterialProperty.MP_OPACITY_MASK,'Mask',unreal.MaterialExpressionMultiply),
                                    (unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET,'Offset',unreal.MaterialExpressionAdd)]:
                old,pin=originals[prop]
                result=built[label]
                if old:
                    result=node('Combine'+label,kind);connect(old,result,'A',pin);connect(built[label],result,'B')
                output(result,prop)
            # Lit source shaders keep their exact normal/roughness/light response
            # at zero; their reflected base color fades as the emissive phase rises.
            old,pin=originals[unreal.MaterialProperty.MP_BASE_COLOR]
            if old:
                inverse=node('BaseWeight',unreal.MaterialExpressionOneMinus);connect(inputs['Strength'],inverse,'Input')
                multiply=node('BaseBlend',unreal.MaterialExpressionMultiply);connect(old,multiply,'A',pin);connect(inverse,multiply,'B')
                output(multiply,unreal.MaterialProperty.MP_BASE_COLOR)
            material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
            material.set_editor_property('max_world_position_offset_displacement',36.0)
            lib.set_material_usage(material,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
            unreal.MaterialService.compile_material(target)
            diag=unreal.MaterialNodeService.get_material_diagnostics(target)
            if not diag.success or not diag.is_compiled_ok or diag.compile_errors:raise RuntimeError(diag.export_text())
            if not unreal.EditorAssetLibrary.save_asset(target,only_if_is_dirty=True):raise RuntimeError('Variant save failed')
            report['variants'].append({'source':source.get_path_name(),'phase':target,'diagnostics':diag.export_text()})
        report['definition']=bind_definition(report['variants'])
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps({'variants':len(report['variants']),'definition':report['definition'],'proxy_only':True}))


main(bool(globals().get('GULI_BIND_ONLY',False)))
