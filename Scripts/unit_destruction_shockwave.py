"""Calibrate project-owned destruction waves against their authored fireball size.

This is an editor authoring helper, not a gameplay radius. Sizes come from each
System's active inputs, size curves and renderer mesh, never from unit-type IDs.
Re-running the helper can only tighten an oversized wave; it cannot compound a
scale reduction. The body's parameters and the runtime model-size scale stay put.
"""
import math
import re
import unreal

MAX_DIAMETER_RATIO = 2.5
ROOT = '/Game/GuLiStrike/FX/UnitFeedback/'
NS = unreal.NiagaraService
EM = unreal.NiagaraEmitterService


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def inputs(path, emitter):
    return {(str(x.script_type), str(x.parameter_name)): str(x.value)
            for x in NS.list_rapid_iteration_params(path, emitter)}


def number(values, emitter, name, stage='ParticleSpawn'):
    return float(values[(stage, 'Constants.' + emitter + '.' + name)])


def curve_upper_bound(path, emitter, suffix):
    """Read the graph DI, not a compiled copy. Cubic control hull bounds overshoot."""
    prefix = path + '.' + path.rsplit('/', 1)[1] + ':' + emitter + '_'
    matches = []
    for obj in unreal.ObjectIterator(unreal.Object):
        try:
            obj_path = obj.get_path_name()
            if (obj_path.startswith(prefix) and '.NiagaraGraph_' in obj_path
                    and obj_path.endswith(suffix)
                    and obj.get_class().get_name() == 'NiagaraDataInterfaceCurve'):
                matches.append(obj.get_editor_property('Curve').export_text())
        except Exception:
            continue
    require(len(matches) == 1, 'Ambiguous/missing size curve: ' + prefix + suffix)
    text = matches[0]
    require('TangentWeightMode=' not in text, 'Weighted curve needs explicit review')
    key_text = text.split('Keys=(', 1)[1].split('),DefaultValue=', 1)[0]
    keys = [dict(re.findall(r'(\w+)=([^,()]+)', row))
            for row in re.findall(r'\(([^()]*)\)', key_text)]
    require(len(keys) >= 2, 'Missing size keys: ' + prefix)
    values = [float(k.get('Value', 0)) for k in keys]
    for left, right in zip(keys, keys[1:]):
        if left.get('InterpMode') == 'RCIM_Cubic':
            dt = float(right.get('Time', 0)) - float(left.get('Time', 0))
            values.extend((float(left.get('Value', 0)) + dt * float(left.get('LeaveTangent', 0)) / 3,
                           float(right.get('Value', 0)) - dt * float(right.get('ArriveTangent', 0)) / 3))
    return require(max(values), 'Non-positive size curve: ' + prefix)


def mesh_diameter(path, emitter):
    renderer = EM.get_renderer_details(path, emitter, 0)
    mesh = require(unreal.EditorAssetLibrary.load_asset(renderer.mesh_path), 'Missing wave mesh')
    bounds = mesh.get_bounding_box()
    size = bounds.max - bounds.min
    return max(size.x, size.y, size.z)


def set_number(path, emitter, stage, name, value, changes):
    full_name = 'Constants.' + emitter + '.' + name
    old = number(inputs(path, emitter), emitter, name, stage)
    # Round downward so float serialization cannot enlarge the requested cap.
    value = math.floor(value * 1e6) / 1e6
    if old - value <= 1e-6:
        return old
    require(NS.set_rapid_iteration_param_by_stage(path, emitter, stage, full_name, str(value)), full_name)
    new = number(inputs(path, emitter), emitter, name, stage)
    require(abs(new - value) < 2e-5, 'Parameter readback failed: ' + full_name)
    changes.append({'emitter': emitter, 'stage': stage, 'parameter': full_name, 'old': old, 'new': new})
    return new


def calibrate(path):
    require(path.startswith(ROOT), 'Only project-owned destruction assets may be changed')
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE')
    require(unreal.EditorAssetLibrary.load_asset(path), 'Missing System: ' + path)
    before = {str(e.emitter_name): bool(e.is_enabled) for e in NS.list_emitters(path)}
    report = {'path': path, 'maximum_diameter_ratio': MAX_DIAMETER_RATIO, 'changes': []}
    if 'Shockwave' in before and 'Explosion001' in before:
        body = inputs(path, 'Explosion001')
        wave = inputs(path, 'Shockwave')
        diameter = number(body, 'Explosion001', 'InitializeParticle.Uniform Sprite Size')
        mesh_scale = [float(v) for v in re.findall(r'-?\d+(?:\.\d+)?',
                      wave[('ParticleSpawn', 'Constants.Shockwave.InitializeParticle.Mesh Scale')])]
        require(len(mesh_scale) == 3, 'Invalid mesh scale')
        peak = curve_upper_bound(path, 'Shockwave', 'NiagaraNodeInput_0.FloatFromCurve_FloatCurve')
        base = mesh_diameter(path, 'Shockwave') * max(mesh_scale) * peak
        old_growth = number(wave, 'Shockwave', 'FloatFromCurve003.Scale Curve', 'ParticleUpdate')
        growth = set_number(path, 'Shockwave', 'ParticleUpdate', 'FloatFromCurve003.Scale Curve',
                            min(old_growth, diameter * MAX_DIAMETER_RATIO / base), report['changes'])
        report.update(body_diameter=diameter, wave_diameter_before=base * old_growth,
                      wave_diameter_upper_bound=base * growth, diameter_ratio=base * growth / diameter)
        require(inputs(path, 'Explosion001') == body, 'Fireball inputs changed')
    elif all(e in before for e in ('main', 'refr_mesh', 'smoke_shockwave')):
        require(not EM.get_emitter_properties(path, 'refr_mesh').local_space,
                'Normalize refraction space first: local-space mesh rendering would apply owner scale twice')
        body = inputs(path, 'main')
        smoke = inputs(path, 'smoke_shockwave')
        refraction = inputs(path, 'refr_mesh')
        # Use the smallest randomly selected fireball's peak, not an unused Init default.
        diameter = number(body, 'main', 'RandomRangeFloat002.Minimum') * curve_upper_bound(
            path, 'main', 'Value_Scale_Factor_FloatCurve') * number(body, 'main', 'FloatFromCurve.Scale Curve', 'ParticleUpdate')
        radius = diameter * MAX_DIAMETER_RATIO / 2
        smoke_radius = number(smoke, 'smoke_shockwave', 'RandomRangeFloat002.Maximum') * curve_upper_bound(
            path, 'smoke_shockwave', 'Value.Scale Factor.FloatCurve') * number(
                smoke, 'smoke_shockwave', 'FloatFromCurve.Scale Curve', 'ParticleUpdate') / 2
        lifetime_multiplier = next(float(p.current_value) for p in NS.list_parameters(path)
                                   if str(p.parameter_name) == 'User.LifetimeMult')
        lifetime = number(smoke, 'smoke_shockwave', 'RandomRangeFloat.Maximum') * lifetime_multiplier
        require(lifetime > 0 and radius > smoke_radius, 'Smoke size/lifetime requires author review')
        old_speed = number(smoke, 'smoke_shockwave', 'RandomRangeFloat003.Maximum')
        # Conservative envelope: no drag credit; the actual Drag module only reduces travel.
        # Both local-space travel and sprite size share the existing uniform owner scale.
        factor = min(1.0, (radius - smoke_radius) / (old_speed * lifetime))
        speed = old_speed
        for bound in ('Minimum', 'Maximum'):
            value = number(smoke, 'smoke_shockwave', 'RandomRangeFloat003.' + bound)
            updated = set_number(path, 'smoke_shockwave', 'ParticleSpawn', 'RandomRangeFloat003.' + bound,
                                 value * factor, report['changes'])
            if bound == 'Maximum':
                speed = updated
        mesh_base = mesh_diameter(path, 'refr_mesh') * number(refraction, 'refr_mesh', 'RandomRangeFloat001.Maximum') * curve_upper_bound(
            path, 'refr_mesh', 'Value_Scale_Factor_FloatCurve')
        mesh_span = 0
        for stage in ('ParticleSpawn', 'ParticleUpdate'):
            old = number(refraction, 'refr_mesh', 'FloatFromCurve.Scale Curve', stage)
            value = set_number(path, 'refr_mesh', stage, 'FloatFromCurve.Scale Curve',
                               min(old, diameter * MAX_DIAMETER_RATIO / mesh_base), report['changes'])
            mesh_span = max(mesh_span, mesh_base * value)
        smoke_span = 2 * (speed * lifetime + smoke_radius)
        report.update(body_diameter=diameter, smoke_diameter_upper_bound=smoke_span,
                      refraction_diameter_upper_bound=mesh_span,
                      diameter_ratio=max(smoke_span, mesh_span) / diameter,
                      smoke_envelope='max-speed * max-lifetime + max-sprite-radius; drag ignored conservatively')
        require(inputs(path, 'main') == body, 'Fireball inputs changed')
    else:
        raise RuntimeError('Unknown destruction graph; inspect before calibrating: ' + path)
    require(report['diameter_ratio'] <= MAX_DIAMETER_RATIO + 1e-5, 'Wave exceeds cap')
    require({str(e.emitter_name): bool(e.is_enabled) for e in NS.list_emitters(path)} == before,
            'Emitter topology changed')
    return report
