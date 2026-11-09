"""Numerical authoring calibration of the full-expansion fireball, without image inspection."""
import json
import math
import re
from pathlib import Path
import unreal

OUT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'Artifacts/GroundExplosiveEnhancement_20261109'
PATH = '/Game/GuLiStrike/FX/GroundMech/ExplosivePackage/NS_GroundExplosiveImpact'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
asset = unreal.load_asset(PATH)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def vector(text):
    return tuple(float(v) for v in re.findall(r'[XYZW]=([-+0-9.eE]+)', text))


def extent(p, mesh_radius):
    x, y, _ = vector(p['position'])
    sx, sy, sz = vector(p['scale'])
    qx, qy, qz, qw = vector(p['orientation'])
    # These emitters rotate around the vertical axis, preserving a horizontal ellipse.
    assert abs(qx) + abs(qy) < 1e-6
    angle = 2 * math.atan2(qz, qw)
    ca, sa = math.cos(angle), math.sin(angle)
    result = 0.
    for i in range(1024):
        a = 2 * math.pi * i / 1024
        ex, ey = mesh_radius * sx * math.cos(a), mesh_radius * sy * math.sin(a)
        result = max(result, math.hypot(x + ca*ex - sa*ey, y + sa*ex + ca*ey))
    return result


bursts = []
for run in range(16):
    component = unreal.NiagaraFunctionLibrary.spawn_system_at_location(
        world, asset, unreal.Vector(0, 0, 1000), auto_destroy=False, auto_activate=False, pre_cull_check=False)
    nearest = {}
    try:
        component.set_visibility(False)
        component.set_variable_float('User.Area_Scale', 1.)
        component.activate(True)
        for step in range(100):
            component.advance_simulation(1, .005)
            data = json.loads(unreal.GuLiGroundMechFireEditorLibrary.read_explosive_impact_particles(component))
            for emitter in data['emitters']:
                if emitter['emitter'] not in ('NE_Main_Explosion', 'NE_Main_Explosion001'): continue
                radius = emitter['meshes'][0]['sphere_radius']
                for p in emitter['particles']:
                    age = p['age'] / p['lifetime']
                    key = (emitter['emitter'], p['index'])
                    error = abs(age - .2)
                    if key not in nearest or error < nearest[key]['age_error']:
                        nearest[key] = {'emitter': key[0], 'particle': p, 'age_error': error,
                                        'time': (step+1)*.005, 'mesh_radius_cm': radius}
    finally:
        component.deactivate()
        component.destroy_component(component.get_owner() or component)
    values = list(nearest.values())
    assert values and all(v['age_error'] < .003 for v in values)
    for v in values: v['horizontal_edge_cm'] = extent(v['particle'], v['mesh_radius_cm'])
    bursts.append({'run': run, 'peak_edge_cm': max(v['horizontal_edge_cm'] for v in values), 'particles': values})

maximum = max(b['peak_edge_cm'] for b in bursts)
report = {
    'radius_cm': math.ceil(maximum), 'measured_peak_edge_cm': maximum,
    'burst_count': len(bursts), 'sample_step_seconds': .005, 'peak_normalized_age': .2,
    'basis': 'ScaleMeshSize三轴首次完成圆整展开的平台起点NormalizedAge=0.2；使用主火球两组粒子位置、实际100cm网格半径、XYZ缩放及旋转求水平外沿。保留未修改的User.Area_Scale=1。',
    'excluded': 'NE_Impact闪光；Color002深色烟雾层；展开平台之后的烟雾漂移。未使用Niagara剔除包围盒。',
    'material': {'world_position_offset': False, 'hot_layers': ['Color', 'Color_2'], 'smoke_layer': 'Color002'},
    'rounding': '16次随机爆发采样的最大峰值水平外沿向上取整至1cm；不宣称逐像素可见边缘精度。',
    'source': PATH, 'asset_modified': False, 'visual_acceptance': 'pending_user', 'bursts': bursts}
(OUT/'radius-calibration.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({k: report[k] for k in ('radius_cm','measured_peak_edge_cm','burst_count','asset_modified')}, ensure_ascii=False))
