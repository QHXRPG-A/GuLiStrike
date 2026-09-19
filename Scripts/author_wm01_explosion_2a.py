"""Adapt the user-selected 2A explosion to WM01's runtime uniform-scale contract.

Run through Scripts/ue_exec.py. The marketplace source remains unchanged.
Only renderer attributes are scaled; original materials and animation remain.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/wm01_missile_update_20260919'
SOURCE = '/Game/Stylized_Explosion_Pack_Vol1/VFX/N_Stylized_Explosion_2A_Classic_Explosion'
DEST = '/Game/GuLiStrike/FX/CommanderWeapons/NS_WM01_Explosion_2A'
FIELD = '/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_MissileExplosion'

assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
scope = {'__name__': 'wm01_existing_scratch_helpers'}
source = (ROOT / 'Scripts/build_commander_combat_effects.py').read_text(encoding='utf8')
source = source.split('\ntry:\n    for name, func in', 1)[0]
exec(compile(source, 'build_commander_combat_effects.py', 'exec'), scope)
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
system = unreal.load_asset(DEST) if assets.does_asset_exist(DEST) else assets.duplicate_asset(SOURCE, DEST)
assert system
ns, em = unreal.NiagaraService, unreal.NiagaraEmitterService
if not ns.parameter_exists(DEST, 'User.VisualScale'):
    assert ns.add_user_parameter(DEST, 'User.VisualScale', 'Float', '1.0')
assert ns.set_parameter(DEST, 'User.VisualScale', '1.0')
report = {'source': SOURCE, 'destination': DEST, 'emitters': [], 'saved': []}
for emitter in ns.list_emitters(DEST):
    name = str(emitter.emitter_name)
    local = em.get_emitter_properties(DEST, name).local_space
    for stage in ['ParticleSpawn', 'ParticleUpdate']:
        module_name = 'WM01VisualScaleSpawn' if stage == 'ParticleSpawn' else 'WM01VisualScaleUpdate'
        modules = em.list_modules(DEST, name)
        if not any(str(m.module_name) == module_name for m in modules):
            scope['scratch'](DEST, name, stage, module_name,
                [('P','Position','Particles.Position'), ('Size','vec2','Particles.SpriteSize'),
                 ('Owner','Position','Engine.Owner.Position'), ('Scale','float','User.VisualScale')],
                [('VisualPosition','Position','Particles.WM01VisualPosition'),
                 ('VisualSize','vec2','Particles.WM01VisualSize')],
                ('VisualPosition=P*Scale;' if local else 'VisualPosition=Owner+(P-Owner)*Scale;')
                + 'VisualSize=Size*Scale;')
    # Copy the current binding's native type/flags; rename only its attribute.
    renderers = [r for r in unreal.ObjectIterator(unreal.NiagaraSpriteRendererProperties)
                 if r.get_path_name().startswith(system.get_path_name()+':')
                 and r.get_outer().get_name().startswith(name)]
    assert len(renderers) == 1, (name, [r.get_path_name() for r in renderers])
    renderer = renderers[0]
    for python_name, native_name, old, new in [
        ('position_binding','PositionBinding','Position','WM01VisualPosition'),
        ('sprite_size_binding','SpriteSizeBinding','SpriteSize','WM01VisualSize')]:
        binding = renderer.get_editor_property(python_name).export_text()
        if new not in binding:
            binding = binding.replace('"Particles.'+old+'"', '"Particles.'+new+'"').replace('"'+old+'"','"'+new+'"')
            assert em.set_renderer_property(DEST, name, 0, native_name, binding)
        assert new in renderer.get_editor_property(python_name).export_text()
    report['emitters'].append({'name':name,'local_space':local,
        'position_binding':renderer.get_editor_property('position_binding').export_text(),
        'size_binding':renderer.get_editor_property('sprite_size_binding').export_text()})

system.set_editor_property('max_pool_size',64)
system.set_editor_property('pool_prime_size',0)
result = ns.compile_with_results(DEST)
report['compile'] = {'success':bool(result.success),'errors':list(map(str,result.errors)),'warnings':list(map(str,result.warnings))}
assert result.success and not result.errors, report['compile']
assert assets.save_loaded_asset(system,only_if_is_dirty=True)
report['saved'].append(DEST)
field = unreal.load_asset(FIELD)
variants = list(field.activation_variants)
assert len(variants) == 1
variants[0].set_editor_property('system',system)
variants[0].set_editor_property('scale_parameter_name','User.VisualScale')
field.set_editor_property('activation_variants',variants)
assert assets.save_loaded_asset(field,only_if_is_dirty=True)
report['saved'].append(FIELD)
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'scale-adaptation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'saved':report['saved'],'compile':report['compile'],'emitter_count':len(report['emitters'])}))
