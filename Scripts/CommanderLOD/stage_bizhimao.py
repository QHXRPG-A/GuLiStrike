"""Stage the complete three-tier BiZhiMao mesh/material/vertex-VAT group for review."""
import sys,json,hashlib
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"Scripts/CommanderLOD"))
from common import require_unapproved_candidate
require_unapproved_candidate()
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
import material_helpers as helpers
from material_helpers import *
ns=helpers.__dict__
assert len(META['lods'])==3 and META['runtime_bones']==0
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
for role in ['BaseColor','InternalLineMask','FunctionalMask','ORM']:
    copy(SOURCE+f'/Textures/T_ControlRigMech_{role}_2K',BASE+f'/Textures/T_BiZhiMao_{role}_2K')
REPORT.update(version=META['version'],runtime_bones=0,lods=[])
def vertex_function(textures):
    path=BASE+'/VAT/MF_BiZhiMao_VertexVAT';fn=create(path,unreal.MaterialFunction,unreal.MaterialFunctionFactoryNew())
    code=(ROOT/'Scripts/BiZhiMao/BiZhiMaoVertexVAT.hlsl').read_text(encoding='utf8')
    digest=hashlib.sha256((code+json.dumps(META['lods'],sort_keys=True)).encode()).hexdigest()
    if LIB.get_metadata_tag(fn,'GuLi.VertexVAT')==digest:return fn
    EDIT.delete_all_material_expressions_in_function(fn)
    inputs=dict(Position=node(fn,unreal.MaterialExpressionPreSkinnedPosition),LocalNormal=node(fn,unreal.MaterialExpressionPreSkinnedNormal),
        VertexIndex=node(fn,unreal.MaterialExpressionTextureCoordinate,coordinate_index=3),
        AimWeights=node(fn,unreal.MaterialExpressionTextureCoordinate,coordinate_index=4),
        LODIndex=node(fn,unreal.MaterialExpressionTextureCoordinate,coordinate_index=5))
    for i,lod in enumerate(META['lods']):
        inputs['Position'+str(i)]=node(fn,unreal.MaterialExpressionTextureObject,texture=textures[i][0],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        inputs['Rotation'+str(i)]=node(fn,unreal.MaterialExpressionTextureObject,texture=textures[i][1],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        for key,value in dict(Count=lod['vertex_count'],Rows=lod['rows_per_frame'],Height=lod['height'],Samples=lod['frames_per_clip']).items():
            inputs[key+str(i)]=constant(fn,value)
    for key,value in dict(TextureWidth=4096,CanonicalSamples=32,UpperPivot=META['upper_pivot_cm'],PitchPivot=META['pitch_pivot_cm'],PitchAxis=META['pitch_axis']).items():inputs[key]=constant(fn,value)
    for i,pair in enumerate(META['gun_linkages']):
        inputs[f'LinkageAnchorA{i}']=constant(fn,pair['anchor_a']);inputs[f'LinkageAnchorB{i}']=constant(fn,pair['anchor_b'])
    for name,current_index,previous_index in [('Frame',51,55),('UpperYaw',52,56),('GunPitch',53,57),('OtherFrame',59,61),('DirectionBlend',60,62)]:
        current=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=current_index,const_default_value=0)
        previous=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=previous_index,const_default_value=0)
        switch=node(fn,unreal.MaterialExpressionPreviousFrameSwitch)
        link(current,switch,'Current Frame');link(previous,switch,'Previous Frame');inputs[name]=switch
    custom=node(fn,unreal.MaterialExpressionCustom,code=code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        desc='True vertex VAT; static geometry, no runtime skeleton; phase-aligned gait and marked turret/piston vertices')
    pins=[]
    for name in inputs:
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    custom.set_editor_property('inputs',pins)
    normal=unreal.CustomOutput();normal.set_editor_property('output_name','AnimatedNormal');normal.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('additional_outputs',[normal])
    for name,value in inputs.items():link(value,custom,name)
    for i,(name,pin) in enumerate([('Offset',''),('Normal','AnimatedNormal')]):
        output=node(fn,unreal.MaterialExpressionFunctionOutput,output_name=name,sort_priority=i);link(world(fn,custom,pin),output)
    EDIT.update_material_function(fn);LIB.set_metadata_tag(fn,'GuLi.VertexVAT',digest);save(fn)
    return fn

textures=[]
for lod in META['lods']:
    pair=[]
    for role in ['Position','Rotation']:
        tex=imported(lod[role.lower()],BASE+f'/Textures/T_BiZhiMao_Vertex{role}_LOD{lod["lod"]}',factory=unreal.TextureFactory())
        for key,value in dict(srgb=False,compression_settings=unreal.TextureCompressionSettings.TC_HDR if role=='Position' else unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
            compression_no_alpha=False,filter=unreal.TextureFilter.TF_NEAREST,mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
            address_x=unreal.TextureAddress.TA_CLAMP,address_y=unreal.TextureAddress.TA_CLAMP,never_stream=True).items():tex.set_editor_property(key,value)
        save(tex);pair.append(tex)
    textures.append(pair)
fn=vertex_function(textures)
body=ns['material'](BASE+'/Materials/M_BiZhiMao_Toon3_SparseLines',fn)
# FBX vertex colours carry the authored sRGB bytes; the unlit shader decodes once.
outline=ns['material'](BASE+'/Materials/M_BiZhiMao_Outline',fn,True)
instances=[]
for i in range(3):
    mat=create(BASE+f'/Materials/MI_BiZhiMao_LOD{i}',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    EDIT.set_material_instance_parent(mat,body);EDIT.set_material_instance_scalar_parameter_value(mat,'InternalLineStrength',[.3,.22,0][i]);save(mat);instances.append(mat)
for name,tint,strength in [('Hit',(1,1,1),0),('Wreck',(.25,.25,.25),1),('Phase',(.22,.65,1),.7)]:
    mat=create(BASE+'/VAT/MI_BiZhiMao_'+name,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    EDIT.set_material_instance_parent(mat,body);EDIT.set_material_instance_vector_parameter_value(mat,'StatusTint',unreal.LinearColor(*tint,1))
    EDIT.set_material_instance_scalar_parameter_value(mat,'StatusTintStrength',strength);save(mat)
sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for role in ['VAT','Construction']:
    mesh=imported(META['lods'][0]['fbx'],BASE+'/Meshes/SM_BiZhiMao_'+role,ns['fbx_options'](),unreal.FbxFactory())
    for i in range(1,3):assert sub.import_lod(mesh,i,META['lods'][i]['fbx'])==i
    slots=[unreal.StaticMaterial(material_interface=instances[0],material_slot_name='Body_LOD0'),
        unreal.StaticMaterial(material_interface=outline,material_slot_name='Outline')]
    slots += [unreal.StaticMaterial(material_interface=instances[i],material_slot_name=f'Body_LOD{i}') for i in range(1,3)]
    mesh.set_editor_property('static_materials',slots)
    for i in range(3):
        build=sub.get_lod_build_settings(mesh,i)
        for key,value in dict(use_full_precision_u_vs=True,recompute_normals=False,recompute_tangents=False,
            generate_lightmap_u_vs=False,remove_degenerates=False).items():build.set_editor_property(key,value)
        sub.set_lod_build_settings(mesh,i,build);sub.set_lod_material_slot(mesh,0 if i==0 else i+1,i,0)
        if i<2:sub.set_lod_material_slot(mesh,1,i,1)
    sub.set_lod_screen_sizes(mesh,META['screen_sizes'])
    actual=META['gameplay_bounds_cm'];wanted=META['runtime_render_bounds_cm']
    positive=[max(0,wanted['max'][i]-actual['max'][i]) for i in range(3)]
    negative=[max(0,actual['min'][i]-wanted['min'][i]) for i in range(3)]
    mesh.set_editor_property('positive_bounds_extension',unreal.Vector(*positive) if role=='VAT' else unreal.Vector(0,0,0))
    mesh.set_editor_property('negative_bounds_extension',unreal.Vector(*negative) if role=='VAT' else unreal.Vector(0,0,0));save(mesh)
    if role=='VAT':
        REPORT['lods']=[dict(lod=i,triangles=mesh.get_num_triangles(i),sections=mesh.get_num_sections(i),vertices=mesh.get_num_vertices(i),
            animation_vertices=META['lods'][i]['vertex_count'],animation_texture_size=[META['lods'][i]['width'],META['lods'][i]['height']]) for i in range(3)]

REPORT['success']=True
REPORT['animation_gpu_bytes']=META['gpu_animation_bytes']
(ART/'Reports/ue_vertex_import.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,lods=REPORT['lods'],asset_count=len(REPORT['art_assets']))))
