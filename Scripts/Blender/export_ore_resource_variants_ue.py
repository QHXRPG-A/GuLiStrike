"""Export the 24 authored ore units from the current Blender file to UE FBX.

Run export() through Blender MCP or run this file in the source .blend.
Temporary copies remove review offsets and pack OreGlow in vertex alpha.
The source objects, source file, selection and active scene are preserved.
"""

import hashlib
import json
from pathlib import Path

import bpy
from mathutils import Matrix

ROOT = Path('D:/UE5.7/test1')
OUTPUT = ROOT / 'ArtSource/Resources/Ores/UEExport'
OWNER = 'GuLiStrike.OreUEImport.v01'
STATES = ('Full', 'Partial', 'Remnant')


def expected_names():
    return [f'Ore_{kind}_{family:02d}_{state}'
            for kind in ('Blue', 'Red') for family in range(1, 5)
            for state in STATES]


def export():
    if bpy.context.mode != 'OBJECT':
        raise RuntimeError('Switch Blender to Object Mode before export.')
    sources = [bpy.data.objects.get(n) for n in expected_names()]
    if any(o is None for o in sources):
        raise RuntimeError('The loaded file does not contain all 24 ore units.')
    for obj in sources:
        if obj.type != 'MESH' or obj.get('ore_role') != 'unit_asset':
            raise RuntimeError('Unexpected source object: ' + obj.name)
        if obj.modifiers or obj.parent:
            raise RuntimeError('Unbaked modifiers/parent require inspection: ' + obj.name)
        if any(abs(s - 1) > 1e-6 for s in obj.scale) or any(abs(r) > 1e-6 for r in obj.rotation_euler):
            raise RuntimeError('Unexpected source rotation/scale: ' + obj.name)
        if len(obj.data.materials) != 2:
            raise RuntimeError('Expected crystal/rock slots: ' + obj.name)
        if not obj.data.color_attributes.get('OreColor') or not obj.data.attributes.get('OreGlow'):
            raise RuntimeError('Missing authored appearance attributes: ' + obj.name)

    OUTPUT.mkdir(parents=True, exist_ok=True)
    manifest_path = OUTPUT / 'export_manifest.json'
    previous = json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.exists() else {}
    if previous and previous.get('owner') != OWNER:
        raise RuntimeError('Export directory belongs to another generator.')
    existing_names = {r['name'] for r in previous.get('models', [])}
    for obj in sources:
        fbx = OUTPUT / ('SM_' + obj.name + '.fbx')
        if fbx.exists() and obj.name not in existing_names:
            raise RuntimeError('Unowned export file already exists: ' + str(fbx))

    original_scene = bpy.context.window.scene
    original_selection = list(bpy.context.selected_objects)
    original_active = bpy.context.view_layer.objects.active
    source_dirty = bpy.data.is_dirty
    export_scene = bpy.data.scenes.new('ORE UE Export Temporary')
    export_scene['ore_export_owner'] = OWNER
    export_scene.unit_settings.system = 'METRIC'
    export_scene.unit_settings.scale_length = 1.0
    temporary = []
    rows = []
    material_settings = {}
    try:
        bpy.context.window.scene = export_scene
        for source in sources:
            mesh = source.data.copy()
            obj = bpy.data.objects.new('SM_' + source.name, mesh)
            export_scene.collection.objects.link(obj)
            temporary.append((obj, mesh))
            obj.matrix_world = Matrix.Identity(4)
            for key in ('ore_type', 'ore_family', 'ore_family_name', 'ore_state', 'ore_role'):
                obj[key] = source[key]
            obj['ore_export_owner'] = OWNER
            color = mesh.color_attributes['OreColor']
            glow = mesh.attributes['OreGlow']
            for face in mesh.polygons:
                alpha = float(glow.data[face.index].value)
                if not 0.0 <= alpha <= 1.0:
                    raise RuntimeError('Glow outside 0..1: ' + source.name)
                for loop_index in face.loop_indices:
                    rgba = list(color.data[loop_index].color)
                    color.data[loop_index].color = (*rgba[:3], alpha)
            mesh.color_attributes.active_color = color
            mesh.color_attributes.render_color_index = list(mesh.color_attributes).index(color)
            mesh.calc_loop_triangles()
            bounds = [[min(v.co[i] for v in mesh.vertices), max(v.co[i] for v in mesh.vertices)] for i in range(3)]
            if abs(bounds[2][0]) > 1e-5:
                raise RuntimeError('Ground pivot moved: ' + source.name)
            if len(mesh.loop_triangles) > 15000:
                raise RuntimeError('Triangle budget exceeded: ' + source.name)
            colors_linear = [tuple(c.color) for c in color.data]
            for mat in mesh.materials:
                bsdf = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
                material_settings[mat.name] = {
                    'roughness': float(bsdf.inputs['Roughness'].default_value),
                    'metallic': float(bsdf.inputs['Metallic'].default_value),
                    'specular': float(bsdf.inputs['Specular IOR Level'].default_value),
                    'coat_weight': float(bsdf.inputs['Coat Weight'].default_value),
                    'coat_roughness': float(bsdf.inputs['Coat Roughness'].default_value),
                    'emission_strength': 0.22 if mat.name.endswith('_Crystal') else 0.0,
                }
            bpy.context.view_layer.objects.active = obj
            obj.select_set(True)
            path = OUTPUT / (obj.name + '.fbx')
            result = bpy.ops.export_scene.fbx(
                filepath=str(path), check_existing=False, use_selection=True,
                object_types={'MESH'}, global_scale=1.0, apply_unit_scale=True,
                apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Y', axis_up='Z',
                use_mesh_modifiers=True, mesh_smooth_type='FACE', use_triangles=True,
                use_tspace=False, colors_type='LINEAR', prioritize_active_color=True,
                use_custom_props=True, bake_anim=False, path_mode='STRIP',
            )
            if 'FINISHED' not in result or not path.is_file():
                raise RuntimeError('FBX export failed: ' + source.name)
            obj.select_set(False)
            rows.append({
                'name': source.name, 'asset_name': obj.name, 'fbx': str(path),
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                'ore_type': source['ore_type'], 'family': source['ore_family'],
                'family_name': source['ore_family_name'], 'state': source['ore_state'],
                'triangles': len(mesh.loop_triangles), 'bounds_local_m': bounds,
                'dimensions_m': [b[1] - b[0] for b in bounds],
                'material_slots': [m.name for m in mesh.materials],
                'color_linear_min': [min(c[i] for c in colors_linear) for i in range(4)],
                'color_linear_max': [max(c[i] for c in colors_linear) for i in range(4)],
            })
        report = {
            'owner': OWNER, 'success': True, 'source_blend': bpy.data.filepath,
            'source_had_unsaved_state': source_dirty, 'blender_version': bpy.app.version_string,
            'asset_count': len(rows), 'models': rows, 'materials': material_settings,
            'fbx_units': 'metres with FBX unit scale; UE converts to centimetres',
            'color_contract': 'FBX raw linear RGB from OreColor; linear alpha from OreGlow. UE import/build performs an internal sRGB roundtrip; the final VertexColor material input receives the raw normalized bytes.',
            'pivot_contract': 'Each export object is at identity; local bottom Z=0; review offsets are excluded.',
        }
        manifest_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        return {'success': True, 'asset_count': len(rows), 'manifest': str(manifest_path)}
    finally:
        bpy.context.window.scene = original_scene
        for obj, mesh in temporary:
            bpy.data.objects.remove(obj, do_unlink=True)
            if mesh.users == 0:
                bpy.data.meshes.remove(mesh)
        bpy.data.scenes.remove(export_scene)
        for obj in bpy.context.view_layer.objects:
            obj.select_set(obj in original_selection)
        bpy.context.view_layer.objects.active = original_active


if __name__ == '__main__':
    print(json.dumps(export(), ensure_ascii=False))
