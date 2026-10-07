"""Author the approved BiZhiMao vertex VAT definition after native reload."""
import json
from pathlib import Path
import unreal

ART = Path('D:/UE5.7/test1/ArtSource/CommanderLOD_20261005')
approval = json.loads((ART / 'approval_B.json').read_text(encoding='utf8'))
assert approval['approval_B'] == 'approved'
metadata = json.loads((ART / 'BiZhiMao/vertex_metadata.json').read_text(encoding='utf8'))
ready_file = ART / 'Reports/formal_prepared_BiZhiMao.json'
ready = json.loads(ready_file.read_text(encoding='utf8'))
base = ready['formal_root']
path = base + '/VAT/DA_BiZhiMao_VAT'
lib = unreal.EditorAssetLibrary
asset = lib.load_asset(path)
owner = 'CommanderLOD.3Tier.v1.BiZhiMao'
if asset:
    assert lib.get_metadata_tag(asset, 'GuLi.Owner') == owner
else:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.GuLiVATDefinition)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_BiZhiMao_VAT', base+'/VAT', unreal.GuLiVATDefinition, factory)
assert asset
for field, value in dict(bone_position=None, bone_rotation=None, bones=[], bone_deltas=[], muzzles=[],
    vertex_animation=True, directional_blend=True, upper_bone_name=unreal.Name('None'),
    pitch_bone_name=unreal.Name('None'), frames_per_second=30,
    upper_pivot=unreal.Vector(*metadata['upper_pivot_cm']), pitch_pivot=unreal.Vector(*metadata['pitch_pivot_cm']),
    pitch_axis=unreal.Vector(*metadata['pitch_axis']), upper_turn_rate_degrees_per_second=metadata['upper_turn_rate'],
    pitch_turn_rate_degrees_per_second=metadata['pitch_turn_rate'], minimum_pitch_degrees=metadata['pitch_limits'][0],
    maximum_pitch_degrees=metadata['pitch_limits'][1]).items():
    asset.set_editor_property(field, value)
for field in ('gameplay_bounds', 'runtime_render_bounds'):
    bounds = metadata[field + '_cm']
    asset.set_editor_property(field, unreal.Box(unreal.Vector(*bounds['min']), unreal.Vector(*bounds['max'])))
lods = []
for index, entry in enumerate(metadata['lods']):
    assert index == entry['lod']
    lod = unreal.GuLiVertexVATLOD()
    for field in ('vertex_count', 'texture_width', 'rows_per_frame', 'frames_per_clip', 'texture_frames'):
        lod.set_editor_property(field, entry[field])
    for role in ('Position', 'Rotation'):
        texture = lib.load_asset(base + '/Textures/T_BiZhiMao_Vertex' + role + '_LOD' + str(index))
        assert texture
        lod.set_editor_property(role.lower(), texture)
    lods.append(lod)
asset.set_editor_property('vertex_lo_ds', lods)
clips = []
# Step receives world-space cm/s. Scale the source stride with the approved model scale.
stride_scale = metadata['presentation_scale']
for source in metadata['clips']:
    clip = unreal.GuLiVATClip()
    for field in ('first_frame', 'frame_count', 'duration_seconds', 'loop'):
        clip.set_editor_property(field, source[field])
    clip.set_editor_property('name', source['name'])
    clip.set_editor_property('stride_centimeters', source['stride_cm'] * stride_scale)
    clips.append(clip)
asset.set_editor_property('clips', clips)
for field, suffix in [('hit_material','Hit'), ('wreck_material','Wreck'), ('phase_material','Phase')]:
    material = lib.load_asset(base + '/VAT/MI_BiZhiMao_' + suffix)
    assert material
    asset.set_editor_property(field, material)
assert asset.is_valid_definition()
errors = list(asset.call_method('ValidateImportedTextures'))
assert not errors, errors
assert len(asset.get_editor_property('vertex_lo_ds')) == 3
assert not any(asset.get_editor_property(f) for f in ('bones','bone_deltas','muzzles','bone_position','bone_rotation'))
lib.set_metadata_tag(asset, 'GuLi.Owner', owner)
lib.set_metadata_tag(asset, 'GuLi.ApprovedVersion', approval['version'])
lib.set_metadata_tag(asset, 'GuLi.ApprovedSHA256', approval['content_sha256'])
lib.set_metadata_tag(asset, 'GuLi.CommanderLOD', 'LOD0Near/LOD1Middle/LOD2Far;Bapproved;VertexVATNoBones')
assert lib.save_loaded_asset(asset, False)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([base], force_rescan=True)
opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
dependencies = [str(p) for p in (registry.get_dependencies(path, opts) or [])]
assert not any('/LODReview_' in p for p in dependencies)
if not any(a['formal'] == asset.get_path_name() for a in ready['assets']):
    ready['assets'].append(dict(source='Approved vertex_metadata.json', formal=asset.get_path_name(), class_name='GuLiVATDefinition', dependencies=dependencies))
clip_rows = [dict(name=str(c.get_editor_property('name')), first_frame=c.get_editor_property('first_frame'),
    frame_count=c.get_editor_property('frame_count'), stride_cm=c.get_editor_property('stride_centimeters'),
    duration_seconds=c.get_editor_property('duration_seconds'), loop=c.get_editor_property('loop')) for c in asset.get_editor_property('clips')]
ready.update(state='native_configured_verified', vat_definition=asset.get_path_name(), runtime_fields_available=True,
    animation=dict(asset=asset.get_path_name(), valid=True, bones=0, bone_deltas=0, muzzles=0,
        vertex_lod_count=3, clips=clip_rows, source_stride_scale=stride_scale,
        straight_speed_cm_s=144, straight_cycles_per_second=144/(300*stride_scale), texture_validation_errors=errors))
ready_file.write_text(json.dumps(ready, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, vat_definition=asset.get_path_name(),
    lod_count=3, runtime_bones=0, clips=clip_rows, texture_validation_errors=errors)))
