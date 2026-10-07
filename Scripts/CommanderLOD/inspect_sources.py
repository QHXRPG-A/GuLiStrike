"""Inventory frozen Blender sources without changing any source file."""
import bpy
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/UE5.7/test1')
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
sources = {
    'DefaultSoldier': ROOT / 'ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/RSGMech_Delivery_UE_v1.blend',
    'WM01': ROOT / 'ArtSource/MechanicalAnimation_20260929/WarMachine/WarMachine_RigidEditable.blend',
    'SweeperSummon': ROOT / 'ArtSource/MechanicalAnimation_20260929/Sweeper/Sweeper_RigidEditable.blend',
    'BiZhiMao': ROOT / 'ArtSource/Mechs/BiZhiMao_20261005/LOD_v2/BiZhiMao_LOD_v2.blend',
}
report = {}
for name, path in sources.items():
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    bpy.ops.wm.open_mainfile(filepath=str(path))
    objects = []
    for ob in bpy.data.objects:
        if ob.type != 'MESH':
            continue
        ob.data.calc_loop_triangles()
        objects.append(dict(name=ob.name, triangles=len(ob.data.loop_triangles),
            uv_layers=[layer.name for layer in ob.data.uv_layers],
            materials=[material.name if material else None for material in ob.data.materials],
            modifiers=[modifier.type for modifier in ob.modifiers],
            parent=ob.parent.name if ob.parent else None, visible=not ob.hide_render))
    report[name] = dict(path=str(path), sha256=digest, scene=bpy.context.scene.name,
        camera=bpy.context.scene.camera.name if bpy.context.scene.camera else None,
        objects=objects, actions=[action.name for action in bpy.data.actions],
        unchanged=hashlib.sha256(path.read_bytes()).hexdigest() == digest)
(ART / 'Reports').mkdir(parents=True, exist_ok=True)
(ART / 'Reports/blender_sources.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print('COMMANDER_LOD_SOURCE_INVENTORY', json.dumps({name:dict(objects=len(row['objects']), unchanged=row['unchanged']) for name,row in report.items()}), flush=True)
