"""Run in UE only AFTER user B approval and the approved Editor build/reload.

No PIE, screenshots or gameplay testing. Imports exact reviewed samples, updates
formal references, reads saved assets back. Does not touch gameplay model size.
"""
import unreal,json,math,hashlib,contextlib,io
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
ART=ROOT/'ArtSource/CommanderRecoil_20261114'
LIB=unreal.EditorAssetLibrary;EDIT=unreal.MaterialEditingLibrary;TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
OWNER='GuLi.CommanderRecoil.20261114.v1'
SOURCES=json.loads((ART/'Sources/formal-sources.json').read_text(encoding='utf8'))
CONTRACT=json.loads((ART/'Sources/material-contract.json').read_text(encoding='utf8'))
REPORT={'success':False,'assets':[],'models':{},'user_visual_acceptance':False}

def save(a):
    assert LIB.save_loaded_asset(a,False),a.get_path_name()
    REPORT['assets'].append(a.get_path_name());return a

def create(path,cls,factory):
    a=unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    assert not a or LIB.get_metadata_tag(a,'GuLi.RecoilOwner')==OWNER,path
    if not a:
        folder,name=path.rsplit('/',1);LIB.make_directory(folder);a=TOOLS.create_asset(name,folder,cls,factory)
    assert a,path
    LIB.set_metadata_tag(a,'GuLi.RecoilOwner',OWNER);return a

def texture(source,path,full_float=True):
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(source),destination_path=path.rsplit('/',1)[0],destination_name=path.rsplit('/',1)[1],automated=True,replace_existing=True,save=False,factory=unreal.TextureFactory()).items():task.set_editor_property(k,v)
    TOOLS.import_asset_tasks([task]);a=unreal.load_asset(path);assert a,path
    for k,v in dict(srgb=False,compression_settings=unreal.TextureCompressionSettings.TC_HDR_F32 if full_float else unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
        compression_no_alpha=False,filter=unreal.TextureFilter.TF_NEAREST,mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
        address_x=unreal.TextureAddress.TA_CLAMP,address_y=unreal.TextureAddress.TA_CLAMP,never_stream=True).items():a.set_editor_property(k,v)
    LIB.set_metadata_tag(a,'GuLi.RecoilOwner',OWNER);LIB.set_metadata_tag(a,'GuLi.SourceSHA256',hashlib.sha256(source.read_bytes()).hexdigest());return save(a)

def node(owner,cls,**props):
    n=EDIT.create_material_expression_in_function(owner,cls)
    for k,v in props.items():n.set_editor_property(k,v)
    return n

def link(a,b,pin='',output=''):
    assert EDIT.connect_material_expressions(a,output,b,pin),(str(a),str(b),pin)

def extend_function(path,unit,textures):
    fn=unreal.load_asset(path);assert fn,path
    custom=next(n for n in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if n.get_outer()==fn and ('PioneerMath' in n.get_editor_property('code') or 'RigidMath' in n.get_editor_property('code')))
    inputs=list(custom.get_editor_property('inputs'));names={str(p.get_editor_property('input_name')) for p in inputs}
    params={}
    for n in unreal.ObjectIterator(unreal.MaterialExpression):
        if n.get_outer()==fn and isinstance(n,(unreal.MaterialExpressionTextureObjectParameter,unreal.MaterialExpressionScalarParameter)):
            params[str(n.get_editor_property('parameter_name'))]=n
    for key,value in [('RecoilPosition',textures[0]),('RecoilRotation',textures[1]),('RecoilFrames',66.),('RecoilEnabled',0.),('RecoilMirrorGunY',0.),('RecoilMissileEnabled',0.),('RecoilMissilePeakRadians',math.radians(8.))]:
        if key in names:continue
        pin=unreal.CustomInput();pin.set_editor_property('input_name',key);inputs.append(pin)
        params[key]=node(fn,unreal.MaterialExpressionTextureObjectParameter,parameter_name=key,texture=value,
            sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR) if not isinstance(value,float) else node(fn,unreal.MaterialExpressionScalarParameter,parameter_name=key,default_value=value)
    for key in ('RecoilL','RecoilR','RecoilM'):
        if key not in names:
            pin=unreal.CustomInput();pin.set_editor_property('input_name',key);inputs.append(pin)
    custom.set_editor_property('inputs',inputs)
    for key,source in params.items():
        if key.startswith('Recoil'):link(source,custom,key)
    for side,key in enumerate(('RecoilL','RecoilR','RecoilM')):
        if key in names:continue
        a=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=63+side if side<2 else 67,const_default_value=side*33.)
        b=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=65+side if side<2 else 68,const_default_value=side*33.)
        switch=node(fn,unreal.MaterialExpressionPreviousFrameSwitch);link(a,switch,'Current Frame');link(b,switch,'Previous Frame');link(switch,custom,key)
    source=ROOT/'Scripts/CommanderRecoil'/('HeavyWithRecoil.hlsl' if unit=='WarMachine' else 'PioneerWithRecoil.hlsl')
    code=source.read_text().replace('// GULI_RECOIL_SAMPLING (expanded by the material authoring script)',(ROOT/'Scripts/CommanderRecoil/RecoilSampling.hlsl').read_text())
    custom.set_editor_property('code',code);EDIT.update_material_function(fn)
    LIB.set_metadata_tag(fn,'GuLi.RecoilOwner',OWNER);save(fn)
    return fn

def instance(source,path,textures,heavy,frames):
    mi=create(path,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    EDIT.set_material_instance_parent(mi,source)
    # UE 5.7's texture setter performs the write but always returns false.
    # Verify the value through the getter rather than trusting that return flag.
    for k,v in [('RecoilPosition',textures[0]),('RecoilRotation',textures[1])]:
        EDIT.set_material_instance_texture_parameter_value(mi,k,v)
        assert EDIT.get_material_instance_texture_parameter_value(mi,k)==v,k
    for k,v in [('RecoilFrames',float(frames)),('RecoilEnabled',1.),('RecoilMirrorGunY',1. if heavy else 0.),('RecoilMissileEnabled',1. if heavy else 0.)]:
        EDIT.set_material_instance_scalar_parameter_value(mi,k,v)
        assert abs(EDIT.get_material_instance_scalar_parameter_value(mi,k)-v)<.0001,k
    EDIT.update_material_instance(mi);return save(mi)

def install_recoil(unit):
    heavy=unit=='WarMachine';folder=ART/unit/('Combined_v2' if heavy else '')
    meta=json.loads((folder/'recoil.json').read_text(encoding='utf8'))
    base='/Game/GuLiStrike/Commander/Units/'+('WM01' if heavy else 'DefaultSoldier')+'/Recoil_v1'
    tex=[texture(folder/'Textures'/f'T_Recoil{k}.exr',base+'/Textures/T_Recoil'+k) for k in ('Position','Rotation')]
    paths=[x['path'] for x in CONTRACT['functions'] if ('Pioneer_BoneVAT' in x['path'])== (not heavy) and 'SweeperSummon' not in x['path']]
    for path in paths:extend_function(path,unit,tex)
    factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.GuLiRecoilVATDefinition)
    data=create(base+'/DA_Recoil',unreal.GuLiRecoilVATDefinition,factory);samples=[]
    for row in meta['samples']:
        s=unreal.GuLiRecoilVATSample()
        for k,v in row.items():s.set_editor_property(k,unreal.Quat(*v) if k=='upper_rotation' else unreal.Vector(*v))
        samples.append(s)
    mesh=unreal.load_asset(meta['source_mesh']);bounds=mesh.get_bounds();radius=bounds.box_extent.length()
    # Conservative union includes the old locomotion/hover bounds at any recoil
    # phase. This is a render-only component bound; gameplay footprint is unchanged.
    extra=meta['peak_upper_translation_cm']+max(meta['peak_gun_translation_cm'])+radius*2*math.sin(math.radians(14)/2)
    low=[min(meta['render_bounds']['min'][i],getattr(bounds.origin,'xyz'[i])-getattr(bounds.box_extent,'xyz'[i])-extra) for i in range(3)]
    high=[max(meta['render_bounds']['max'][i],getattr(bounds.origin,'xyz'[i])+getattr(bounds.box_extent,'xyz'[i])+extra) for i in range(3)]
    props=dict(position_texture=tex[0],rotation_texture=tex[1],frames_per_side=meta['frames_per_side'],cycle_seconds=meta['cycle_seconds'],
        missile_frames=meta.get('missile_frames',0),missile_enter_seconds=meta.get('missile_enter_seconds',.1),
        missile_hold_seconds=meta.get('missile_hold_seconds',1.2),missile_return_seconds=meta.get('missile_return_seconds',.5),
        missile_peak_pitch_degrees=meta.get('missile_peak_pitch_degrees',8.),
        upper_pivot=unreal.Vector(*meta['upper_pivot']),gun_pivots=[unreal.Vector(*p) for p in meta['gun_pivots']],reference_muzzles=[unreal.Vector(*p) for p in meta['reference_muzzles']],
        mirror_gun_pivot_y=heavy,render_bounds=unreal.Box(unreal.Vector(*low),unreal.Vector(*high)),samples=samples,
        upper_part_ids=meta['upper_parts'],left_gun_part_ids=meta['left_parts'],right_gun_part_ids=meta['right_parts'],source_sha256=meta['source_sha256'])
    for k,v in props.items():data.set_editor_property(k,v)
    assert data.is_valid_definition()
    errors=list(data.validate_imported_textures());assert not errors,errors
    save(data)
    # UE's source-LOD count includes triangles omitted by its FBX render export.
    # Keep both contracts explicit; this installer never rebuilds/reimports geometry.
    counts=[mesh.get_num_triangles(i) for i in range(mesh.get_num_lods())]
    assert len(counts)==len(meta['lods'])==3 and counts[0]==meta['lods'][0]['triangles']
    before_path=ART/'formal-mesh-topology-before.json'
    before=json.loads(before_path.read_text(encoding='utf8')) if before_path.exists() else {}
    if unit not in before:
        before[unit]={'mesh':mesh.get_path_name(),'source_lod_triangles':counts}
        before_path.write_text(json.dumps(before,indent=2),encoding='utf8')
    assert counts==before[unit]['source_lod_triangles']
    # Parent to the exact formal materials, preserving approved team/outline inputs.
    for index,path in enumerate(SOURCES[unit]['materials']):
        src=unreal.load_asset(path);assert src,path
        mi=instance(src,base+'/Materials/MI_Recoil_'+str(index),tex,heavy,len(meta['samples']));mesh.set_material(index,mi)
    assert data.bind_mesh(mesh);save(mesh)
    assert [mesh.get_num_triangles(i) for i in range(3)]==before[unit]['source_lod_triangles']
    assert unreal.GuLiRecoilVATDefinition.get_mesh_definition(mesh)==data
    # Shared hit/wreck/phase materials use native per-model dynamic parameters.
    for path in CONTRACT[unit]+(list(CONTRACT['overlays'].values()) if not heavy else ['/Game/GuLiStrike/FX/UnitFeedback/M_UnitWreckRust','/Game/GuLiStrike/FX/UnitFeedback/M_UnitHitWhite_Instanced','/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportBody']):
        mat=unreal.load_asset(path);assert mat,path;EDIT.recompile_material(mat);save(mat)
    REPORT['models'][unit]={'mesh':mesh.get_path_name(),'data':data.get_path_name(),'triangles':counts,'fbx_render_triangles':[r['triangles'] for r in meta['lods']],'geometry_unchanged':True,'samples':len(data.get_editor_property('samples')),'render_bounds_cm':[low,high],'source_sha256':meta['source_sha256']}

def install_miner():
    folder=ART/'Miner';meta=json.loads((folder/'metadata.json').read_text(encoding='utf8'))
    base='/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/QuadrupedMiner_10k_v5'
    mesh=unreal.load_asset(base+'/Meshes/SM_QuadrupedMiner_VAT');data=unreal.load_asset(base+'/Animations/DA_QuadrupedMiner_VAT');assert mesh and data
    for i in range(3):
        # Topology, rest vertices, UV IDs and materials are byte-equivalent as data;
        # only moving samples changed. Never rebuild the current mesh for this update.
        old=json.loads((ROOT/f'ArtSource/QuadrupedMiner_10k_v5/lod{i}.json').read_text(encoding='utf8'))
        new=json.loads((folder/f'lod{i}.json').read_text(encoding='utf8'))
        assert old==new,(i,'mesh changed')
        for role,ext in [('Position','exr'),('Normal','png')]:texture(folder/f'Textures/{role}_LOD{i}.{ext}',base+f'/Textures/T_{role}_LOD{i}',role=='Position')
    for k,v in dict(source_sha256=meta['source_sha256'],walk_duration_seconds=28/24,mining_duration_seconds=2.46,
        muzzle_samples=[unreal.Vector(*p) for p in meta['muzzle_samples_cm']]).items():data.set_editor_property(k,v)
    assert data.is_valid_data();save(data)
    REPORT['models']['Miner']={'data':data.get_path_name(),'triangles':[mesh.get_num_triangles(i) for i in range(3)],'walk_duration':data.get_editor_property('walk_duration_seconds'),'mining_duration':data.get_editor_property('mining_duration_seconds'),'mesh_unchanged':True,'muzzle_samples':len(data.get_editor_property('muzzle_samples'))}

def main():
    assert globals().get('USER_B_APPROVED') is True,'User B action review required before formal import'
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True),'Stop PIE with user permission before formal authoring'
    assert hasattr(unreal,'GuLiRecoilVATDefinition'),'Build/reload the approved native code first'
    install_recoil('WarMachine');install_recoil('Pioneer');install_miner()
    REPORT['success']=True
    (ART/'ue-actions-readback.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps(REPORT,ensure_ascii=False))

if __name__=='__main__':main()
