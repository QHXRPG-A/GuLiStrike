"""Update the owned formal surface material, retaining its geometry and CPD contract."""
import hashlib
import importlib
import json
import sys
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/Buildings/OutpostSymmetry_20261029/CoreBoost_x2_v1'
OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'Scripts'))
import Models.outpost_surface_style as outpost_surface_style
importlib.reload(outpost_surface_style)
from Models.outpost_surface_style import surface_code,STYLE_VERSION,HDR_PEAKS,CHROMA_PROTECTION,GAIN_VS_HDR_V1,BELT_HALF_HEIGHT_CM

PATH='/Game/GuLiStrike/Buildings/OutpostSymmetric/Materials/M_OutpostSymmetric_Toon'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_game_world() is None,'Apply outside PIE'
target_dirty=PATH in [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
assert not target_dirty or globals().get('GULI_OUTPOST_REPAIR_MATERIAL',False),'Unsaved target material'
material=unreal.load_asset(PATH)
assert unreal.EditorAssetLibrary.get_metadata_tag(material,'GuLi.ModelProduction.Owner')=='GuLi.OutpostSymmetric.B_v1.20261029'
mesh=unreal.load_asset('/Game/GuLiStrike/Buildings/OutpostSymmetric/Meshes/SM_OutpostSymmetric')
before=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))
nodes=[n for n in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if n.get_outer()==material]
assert len(nodes)==1
old=nodes[0].get_editor_property('code');new=surface_code()
if target_dirty:assert old==new,'Only repair this script\'s pending shader update'
# The non-luminous palette, three shade bands and line-mask blend remain exact.
old_end=old.index('// Retain') if '// Retain' in old else old.index('if(m==4)')
assert old[old.index('float m='):old_end].strip()==new[new.index('float m='):new.index('// Retain')].strip()
if not (OUT/'surface-before.hlsl').exists():
    (OUT/'surface-before.hlsl').write_text(old,encoding='utf8')
    (OUT/'material-before.json').write_text(unreal.MaterialNodeService.export_material_graph(PATH),encoding='utf8')
inputs=list(nodes[0].get_editor_property('inputs'))
if not any(str(p.get_editor_property('input_name'))=='LocalPosition' for p in inputs):
    pin=unreal.CustomInput();pin.set_editor_property('input_name','LocalPosition');inputs.append(pin)
    nodes[0].set_editor_property('inputs',inputs)
positions=[n for n in unreal.ObjectIterator(unreal.MaterialExpressionPreSkinnedPosition) if n.get_outer()==material]
assert len(positions)<=1
position=positions[0] if positions else unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionPreSkinnedPosition,-800,225)
interpolators=[n for n in unreal.ObjectIterator(unreal.MaterialExpressionVertexInterpolator) if n.get_outer()==material]
assert len(interpolators)<=1
interpolator=interpolators[0] if interpolators else unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionVertexInterpolator,-600,225)
assert unreal.MaterialEditingLibrary.connect_material_expressions(position,'',interpolator,unreal.MaterialEditingLibrary.get_material_expression_input_names(interpolator)[0])
assert unreal.MaterialEditingLibrary.connect_material_expressions(interpolator,'',nodes[0],'LocalPosition')
material.modify();nodes[0].set_editor_property('code',new)
unreal.MaterialEditingLibrary.recompile_material(material)
diag=unreal.MaterialNodeService.get_material_diagnostics(PATH)
assert diag.success and diag.is_compiled_ok and not diag.compile_errors,str(diag)
unreal.EditorAssetLibrary.set_metadata_tag(material,'GuLi.Outpost.SurfaceStyle',STYLE_VERSION)
unreal.EditorAssetLibrary.set_metadata_tag(material,'GuLi.Outpost.HDRPeaks',json.dumps(HDR_PEAKS,sort_keys=True))
unreal.EditorAssetLibrary.set_metadata_tag(material,'GuLi.Outpost.BeltEmission','White')
assert unreal.EditorAssetLibrary.save_loaded_asset(material,False)
saved=unreal.load_asset(PATH)
assert next(n for n in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if n.get_outer()==saved).get_editor_property('code')==new
after=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))
assert before==after
assert mesh.get_material(0)==saved
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
poses=[a for a in actors if a.get_actor_label().startswith('EditorPose_Outpost_')]
assert len(poses)==49 and all(a.static_mesh_component.get_material(0)==saved for a in poses)
component=poses[0].static_mesh_component
assert component.get_custom_primitive_data_index_for_vector_parameter('GuLi_TeamPrimary')==8
assert [component.get_custom_primitive_data_index_for_scalar_parameter(n) for n in
    ['GuLi_TeamEnabled','GuLi_TeamLightStrength','GuLi_CoreUseTeamColor']]==[16,17,18]
pp=[]
for volume in [a for a in actors if isinstance(a,unreal.PostProcessVolume)]:
    if volume.get_editor_property('bEnabled'):
        s=volume.get_editor_property('Settings')
        pp.append({'actor':volume.get_actor_label(),'bloom':s.get_editor_property('BloomIntensity'),
            'override_bloom':s.get_editor_property('bOverride_BloomIntensity')})
graph=unreal.MaterialNodeService.export_material_graph(PATH)
(OUT/'material-after.json').write_text(graph,encoding='utf8')
(OUT/'surface.hlsl').write_text(new,encoding='utf8')
result={'success':True,'material':saved.get_path_name(),'style':STYLE_VERSION,'hdr_peaks':HDR_PEAKS,
    'hdr_gain_vs_v1':GAIN_VS_HDR_V1,'white_belt_half_height_cm':BELT_HALF_HEIGHT_CM,'local_position_node':'PreSkinnedPosition -> VertexInterpolator',
    'chroma_protection':CHROMA_PROTECTION,'cpd_unchanged':[8,16,17,18],
    'geometry_snapshot_unchanged':before==after,'triangles':after['render_lods'][0]['triangles'],
    'preview_references':len(poses),'existing_postprocess':pp,'postprocess_modified':False,
    'diagnostics':{'success':diag.success,'compiled_ok':diag.is_compiled_ok,'errors':[str(e) for e in diag.compile_errors],
        'textures':[str(p) for p in diag.referenced_texture_paths]},'shader_sha256':hashlib.sha256(new.encode()).hexdigest(),
    'saved':True,'native_compile_for_material_required':False}
(OUT/'material-delivery.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
