"""Add rigid turn joints to the current static source without changing neutral geometry.

Run in background Blender with --factory-startup --python this_file -- [--apply].
The accepted historic mesh is used only to recover semantic vertex memberships.
"""
import bpy
import hashlib
import json
import math
import re
import shutil
import struct
import sys
from pathlib import Path
from collections import Counter
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / 'ArtSource/MechanicalAnimation_20260929/WarMachine'
OUT = ROOT / 'ArtSource/WarMachineTurn_20260930'
LABELS = ('FL', 'FR', 'RL', 'RR')


def signature(mesh):
    h = hashlib.sha256()
    for v in mesh.vertices:
        h.update(struct.pack('<3f', *v.co))
    for p in mesh.polygons:
        h.update(struct.pack('<ii', p.material_index, len(p.vertices)))
        for v in p.vertices:
            h.update(struct.pack('<i', v))
    for v in mesh.uv_layers[0].data:
        h.update(struct.pack('<2f', *v.uv))
    return h.hexdigest()


def run(apply=False):
    manifest = json.loads((FOLDER / 'manifest.json').read_text(encoding='utf-8'))
    source = Path(manifest['source'])
    assert hashlib.sha256(source.read_bytes()).hexdigest() == manifest['source_sha256']
    bpy.ops.wm.open_mainfile(filepath=source.as_posix())
    semantics, reference = {}, {}
    for name in ('WarMachine_After_Body', 'WarMachine_After_Contour'):
        obj = bpy.data.objects[name]
        names = {g.index: g.name for g in obj.vertex_groups}
        groups = [[names[g.group] for g in v.groups if g.weight > .5 and names[g.group].startswith('ReviewPart::')] for v in obj.data.vertices]
        assert all(len(g) == 1 for g in groups)
        semantics[name] = [g[0] for g in groups]
        reference[name] = [obj.matrix_world @ v.co for v in obj.data.vertices]
    # Recover the actual beam bearing and pin axes, not the decorative hub centers.
    editable = source.with_name('WarMachine_LevelNodes_Editable.blend')
    bpy.ops.wm.open_mainfile(filepath=editable.as_posix())
    joints = []
    for label in LABELS:
        suffix = label[0] + '_' + label[1]
        beam = bpy.data.objects['WM suspension diagonal_' + suffix]
        pin = bpy.data.objects['WM suspension cross pin_' + suffix]
        root = sum((beam.matrix_world @ v.co for v in list(beam.data.vertices)[:4]), Vector()) / 4
        end = sum((pin.matrix_world @ v.co for v in pin.data.vertices), Vector()) / len(pin.data.vertices)
        delta = end - root
        axis = Vector((-delta.y, delta.x, 0)).normalized()
        joints.append(dict(label=label, root_cm=list(root * 100), end_cm=list(end * 100), axis=list(axis)))
    bpy.ops.wm.open_mainfile(filepath=(FOLDER / 'WarMachine_RigidEditable.blend').as_posix())
    before, counts, changed = {}, {}, {}
    all_counts = Counter()
    for obj in bpy.data.objects:
        if obj.type != 'MESH':
            continue
        assert obj.name in semantics and len(obj.data.vertices) == len(semantics[obj.name])
        assert max((v.co - p).length for v, p in zip(obj.data.vertices, reference[obj.name])) < 1e-5
        assert not obj.modifiers and len(obj.data.uv_layers) == 3
        before[obj.name] = signature(obj.data)
        assignments = {}
        for index, name in enumerate(semantics[obj.name]):
            short = re.sub(r'\.\d+$', '', name.removeprefix('ReviewPart::'))
            corner = re.search(r'_(F|R)_(L|R)(?: |$)', short)
            if not corner:
                continue
            leg = LABELS.index(''.join(corner.groups()))
            if short.startswith(('WM suspension diagonal_', 'WM suspension pivot eye_')):
                part = 12 + leg
            elif short.startswith(('WM suspension clevis ', 'WM suspension cross pin_', 'WM suspension pin ',
                                   'WM suspension toe ', 'WM suspension hub seat_', 'WM toe amber optic_')):
                part = 16 + leg
            else:
                continue
            assignments[index] = part
            changed[name] = part
        assert set(assignments.values()) == set(range(12, 20))
        counts[obj.name] = {str(p): sum(v == p for v in assignments.values()) for p in range(12, 20)}
        if not apply:
            continue
        for part in range(12, 20):
            vertices = [i for i, p in assignments.items() if p == part]
            for group in list(obj.vertex_groups):
                if group.name.startswith('RigidPart_'):
                    group.remove(vertices)
            group = obj.vertex_groups.get(f'RigidPart_{part:02d}') or obj.vertex_groups.new(name=f'RigidPart_{part:02d}')
            group.add(vertices, 1, 'REPLACE')
        for loop in obj.data.loops:
            if loop.vertex_index in assignments:
                part = assignments[loop.vertex_index]
                joint = joints[(part - 12) % 4]
                pivot = joint['root_cm' if part < 16 else 'end_cm']
                obj.data.uv_layers[1].data[loop.index].uv = (pivot[0], 1 - pivot[1])
                obj.data.uv_layers[2].data[loop.index].uv = (pivot[2], 1 - part)
        assert signature(obj.data) == before[obj.name]
        for face in obj.data.polygons:
            ids = {round(1 - obj.data.uv_layers[2].data[i].uv.y) for i in face.loop_indices}
            assert len(ids) == 1, 'A face crosses a rigid joint'
        vertex_parts = {}
        for loop in obj.data.loops:
            part = round(1 - obj.data.uv_layers[2].data[loop.index].uv.y)
            assert vertex_parts.setdefault(loop.vertex_index, part) == part
        all_counts.update(vertex_parts.values())
    report = dict(applied=apply, geometry_uv0_material_unchanged=True, signatures=before,
                  joints=joints, partition_counts=counts, changed_groups=changed)
    if apply:
        OUT.mkdir(parents=True, exist_ok=True)
        for filename in ('WarMachine_RigidEditable.blend', 'SM_WarMachine_Rigid.fbx', 'manifest.json'):
            backup = OUT / ('before_' + filename)
            if not backup.exists():
                shutil.copy2(FOLDER / filename, backup)
        manifest['partitions'].update(changed)
        manifest['leg_joints_cm'] = joints
        manifest['custom_data_floats'] = 51
        manifest['rigid_version'] = 4
        manifest['vertex_counts'] = dict(sorted(all_counts.items()))
        for joint in joints:
            manifest['sockets_cm']['Rigid_LegRoot_' + joint['label']] = joint['root_cm']
            manifest['sockets_cm']['Rigid_LegEnd_' + joint['label']] = joint['end_cm']
        bpy.ops.wm.save_as_mainfile(filepath=(FOLDER / 'WarMachine_RigidEditable.blend').as_posix())
        bpy.ops.object.select_all(action='DESELECT')
        for obj in bpy.data.objects:
            if obj.type == 'MESH':
                obj.select_set(True)
                bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.fbx(filepath=(FOLDER / 'SM_WarMachine_Rigid.fbx').as_posix(), use_selection=True,
            object_types={'MESH'}, global_scale=1, apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
            axis_forward='-Y', axis_up='Z', mesh_smooth_type='FACE', bake_anim=False,
            path_mode='STRIP', use_custom_props=True)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=(FOLDER / 'SM_WarMachine_Rigid.fbx').as_posix(), use_custom_normals=True)
        meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
        assert len(meshes) == 2 and not any(o.type == 'ARMATURE' for o in bpy.context.scene.objects)
        assert all(len(o.data.uv_layers) == 3 for o in meshes)
        points = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
        bounds = [[fn(p[i] for p in points) for i in range(3)] for fn in (min, max)]
        error = max(abs(bounds[i][j] - manifest['source_bounds_m'][i][j]) for i in range(2) for j in range(3))
        assert error < .0001
        readback_parts = set()
        for o in meshes:
            for face in o.data.polygons:
                parts = {round(1 - o.data.uv_layers[2].data[i].uv.y) for i in face.loop_indices}
                assert len(parts) == 1 and parts <= set(range(20))
                readback_parts.update(parts)
        assert readback_parts == set(range(20))
        manifest['fbx_readback'] = dict(meshes=2, skeletons=0, max_bounds_error_m=error,
                                        parts=sorted(readback_parts), version=4)
        report['fbx_readback'] = manifest['fbx_readback']
        report['fbx_sha256'] = hashlib.sha256((FOLDER / 'SM_WarMachine_Rigid.fbx').read_bytes()).hexdigest()
        (FOLDER / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
        (OUT / 'rig-source.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('TURN_RIG_RESULT', json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    run('--apply' in sys.argv)
