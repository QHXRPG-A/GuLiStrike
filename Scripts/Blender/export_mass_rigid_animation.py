"""Static-only mechanical animation export. Run with Blender --background --python.

Preserves the approved meshes/UV0/materials. UV1 = pivot XY in UE cm;
UV2 = pivot Z, rigid part ID. FBX's V flip is accounted for at export.
No armature, skin weights, animation sequence, or skeletal mesh is exported.
"""
import bpy
import hashlib
import json
import math
import re
import sys
from collections import Counter
from pathlib import Path
from mathutils import Vector, Matrix

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/MechanicalAnimation_20260929'
SOURCE = {
    'WarMachine': ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6/WarMachine_LevelNodes_Review.blend',
    'Sweeper': ROOT/'ArtSource/StyleAdjust_20260917/Models/Sweeper_NoInk.blend',
}
REPORT = ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6/WarMachine_handbuilt_report.json'
WHEEL_LABELS = ('FL', 'FR', 'RL', 'RR')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bounds(points):
    return [[min(p[i] for p in points) for i in range(3)],
            [max(p[i] for p in points) for i in range(3)]]


def war_part(name, y=0):
    name = re.sub(r'\.\d+$', '', name.replace('ReviewPart::', ''))
    if name.startswith(('WM missile pod ', 'WM missile tube ', 'WM launch tube ', 'WM rounded missile ',
                        'WM launcher ', 'WM rim upper wrap')):
        return 10 if y >= 0 else 11
    if name.startswith('WM hover '):
        suffix = re.search(r'_(F|R)_(L|R)$', name)
        assert suffix, name
        return 6 + WHEEL_LABELS.index(''.join(suffix.groups()))
    side = 0 if '_L' in name else 1
    if name.startswith(('WM twin gun barrel', 'WM twin gun muzzle', 'WM gun barrel sleeve', 'WM gun cooling slot')):
        return 4 + side
    if name.startswith(('WM cannon ', 'WM twin cannon receiver', 'WM ivory barrel collar', 'WM receiver ')):
        return 2 + side
    if name.startswith(('WM suspension ', 'WM toe ', 'WM belly ', 'WM mechanical waist',
                        'WM lower forward prow', 'WM prow ', 'WM rear engine', 'WM rear vent recess')):
        return 0
    return 1


def run(unit):
    source = SOURCE[unit]
    source_hash = digest(source)
    bpy.ops.wm.open_mainfile(filepath=str(source))
    if unit == 'WarMachine':
        meshes = [bpy.data.objects[n] for n in ('WarMachine_After_Body', 'WarMachine_After_Contour')]
    else:
        meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
        assert len(meshes) == 1
    records = []
    partitions = {}
    all_original = []
    for obj in meshes:
        # Preserve exact neutral world geometry while removing the old authoring rig.
        if obj.parent and obj.parent.type == 'ARMATURE':
            assert all(p.matrix_basis == Matrix.Identity(4) for p in obj.parent.pose.bones)
        matrix = obj.matrix_world.copy()
        obj.parent = None
        obj.matrix_world = matrix
        for modifier in list(obj.modifiers):
            assert modifier.type == 'ARMATURE', (obj.name, modifier.type)
            obj.modifiers.remove(modifier)
        obj.data.transform(obj.matrix_world)
        obj.matrix_world = Matrix.Identity(4)
        group_names = {g.index: g.name for g in obj.vertex_groups}
        ids = []
        for vertex in obj.data.vertices:
            groups = [group_names[g.group] for g in vertex.groups if g.weight > .5]
            if unit == 'WarMachine':
                groups = [g for g in groups if g.startswith('ReviewPart::')]
                assert len(groups) == 1, (obj.name, vertex.index, groups)
                part = war_part(groups[0], vertex.co.y)
            else:
                assert len(groups) == 1, groups
                name = groups[0]
                part = 6 + WHEEL_LABELS.index(name.removeprefix('Wheel_')) if name.startswith('Wheel_') else 2 if name == 'Gun_Pitch' else 0
            ids.append(part)
            assert groups[0] not in partitions or partitions[groups[0]] == part, ('Part crosses rigid partitions', groups[0])
            partitions[groups[0]] = part
        original = [v.co.copy() for v in obj.data.vertices]
        all_original.extend(original)
        records.append((obj, ids, original))
    pivots = {i: Vector() for i in range(12)}
    sockets = {}
    if unit == 'WarMachine':
        report = json.loads(REPORT.read_text(encoding='utf-8'))
        def authored(point):
            return (Vector(point) + Vector(report['source_offset'])) * report['source_scale']
        upper = authored((-.65, 0, 1.9 + report['stance_body_lift']))
        pivots[1] = upper
        pivots[10] = pivots[11] = upper
        for side in range(2):
            p = authored((-1.55, (1 if side == 0 else -1)*1.95, 2.56 + report['stance_body_lift']))
            pivots[2+side] = pivots[4+side] = p
        sockets['Rigid_UpperYaw'] = list(upper * 100)
    else:
        report = json.loads((ROOT/'ArtSource/TacticalStyle_20260916/Production_Handbuilt/Sweeper_handbuilt_report.json').read_text(encoding='utf-8'))
        upper = Vector()
        pivots[2] = Vector(report['sockets_m']['Rig_GunPitch'])
    sockets['Rigid_GunPitch_01'] = list(pivots[2]*100)
    if unit == 'WarMachine':
        sockets['Rigid_GunPitch_02'] = list(pivots[3]*100)
    radii = []
    for i, label in enumerate(WHEEL_LABELS):
        if unit == 'WarMachine':
            body, ids, points = records[0]
            # The top of each hub is the fixed disc bearing, not the ankle/support arm.
            points = [p for p, part in zip(points, ids) if part == 6+i]
            lo, hi = bounds(points)
            pivots[6+i] = Vector(((lo[0]+hi[0])*.5, (lo[1]+hi[1])*.5, hi[2]))
            sockets['Rigid_Disc_'+label] = list(pivots[6+i]*100)
        else:
            pivots[6+i] = Vector(report['wheel_pivots_m']['Wheel_'+label])
            sockets['Rigid_Wheel_'+label] = list(pivots[6+i]*100)
            points = [p for _, ids, vertices in records for p, part in zip(vertices, ids) if part == 6+i]
            radius = max(math.hypot(p.x-pivots[6+i].x, p.z-pivots[6+i].z) for p in points)*100
            radii.append(radius)
    out = OUT/unit
    out.mkdir(parents=True, exist_ok=True)
    counts = Counter()
    for obj, ids, original in records:
        obj.hide_set(False)
        obj.hide_viewport = obj.hide_render = False
        while len(obj.data.uv_layers) > 1:
            obj.data.uv_layers.remove(obj.data.uv_layers[-1])
        xy = obj.data.uv_layers.new(name='RigidPivotXY_UE')
        zp = obj.data.uv_layers.new(name='RigidPivotZ_Part_UE')
        for loop in obj.data.loops:
            part = ids[loop.vertex_index]
            p = pivots[part]*100
            xy.data[loop.index].uv = (p.x, 1-p.y)
            zp.data[loop.index].uv = (p.z, 1-part)
        obj.data.uv_layers.active_index = 0
        obj.data.uv_layers[0].active_render = True
        obj.vertex_groups.clear()
        for part in sorted(set(ids)):
            group = obj.vertex_groups.new(name='RigidPart_%02d' % part)
            group.add([i for i, value in enumerate(ids) if value == part], 1, 'REPLACE')
        assert all((p-v.co).length < 1e-7 for p, v in zip(original, obj.data.vertices))
        counts.update(ids)
    for obj in list(bpy.data.objects):
        if obj not in meshes:
            bpy.data.objects.remove(obj, do_unlink=True)
    assert not any(o.type == 'ARMATURE' for o in bpy.data.objects)
    bpy.context.scene.name = unit+'_RigidVertexAnimation'
    bpy.ops.object.select_all(action='DESELECT')
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    blend = out/(unit+'_RigidEditable.blend')
    fbx = out/('SM_'+unit+'_Rigid.fbx')
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    bpy.ops.export_scene.fbx(filepath=str(fbx), use_selection=True, object_types={'MESH'},
        global_scale=1, apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
        axis_forward='-Y', axis_up='Z', mesh_smooth_type='FACE', bake_anim=False,
        path_mode='STRIP', use_custom_props=True)
    manifest = dict(unit=unit, profile=1 if unit == 'WarMachine' else 2, source=str(source),
        source_sha256=source_hash, geometry_unchanged=True, skeleton_count=0, uv_channels=3,
        source_bounds_m=bounds(all_original), sockets_cm=sockets, wheel_radii_cm=radii,
        upper_pivot_cm=list(upper*100), partitions=partitions, vertex_counts=dict(counts),
        files={'blend':str(blend), 'fbx':str(fbx)}, scale=0.2,
        missile_pod_parts=[10,11] if unit == 'WarMachine' else [], custom_data_floats=31)
    # Verify the static FBX independently and retain only compact evidence.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(fbx), use_custom_normals=True)
    imported = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    assert not any(o.type == 'ARMATURE' for o in bpy.context.scene.objects)
    actual = bounds([o.matrix_world@v.co for o in imported for v in o.data.vertices])
    err = max(abs(actual[i][j]-manifest['source_bounds_m'][i][j]) for i in range(2) for j in range(3))
    assert err < 0.0001 and all(len(o.data.uv_layers) == 3 for o in imported)
    manifest['fbx_readback'] = dict(meshes=len(imported), skeletons=0, max_bounds_error_m=err)
    assert digest(source) == source_hash
    (out/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print('MASS_RIGID_EXPORT_OK', json.dumps(dict(unit=unit, counts=dict(counts), readback=manifest['fbx_readback'])), flush=True)


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else ['WarMachine', 'Sweeper']
    for unit in args:
        run(unit)
