"""Repair the formal burning material and rebuild only the finite burst."""
import json
import runpy
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/GroundRevision_20261103'
BASE = '/Game/GuLiStrike/FX/GroundMech/Burning/'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
assert not any(BASE + n in dirty for n in ('M_GroundBurningBullet', 'NS_GroundBurningBullet', 'NS_GroundBurningBurst'))
material = unreal.load_asset(BASE + 'M_GroundBurningBullet')
shader = next(n for n in unreal.ObjectIterator(unreal.MaterialExpressionCustom)
              if n.get_outer() == material and 'HeadDistance' in str(n.get_editor_property('code')))
migration = runpy.run_path(str(ROOT / 'Scripts/GroundMech/Rogue/burning_bullet_shader.py'))
migrate = migration['migrate']
before = str(shader.get_editor_property('code'))
after = migrate(before)
assert migrate(after) == after, 'Shader migration must be repeatable'
shader.modify()
shader.set_editor_property('code', after)
palette = migration['apply_palette'](unreal, material)
unreal.MaterialEditingLibrary.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
bullet = unreal.load_asset(BASE + 'NS_GroundBurningBullet')
bindings = []
for emitter in unreal.NiagaraService.summarize(BASE + 'NS_GroundBurningBullet').emitter_names:
    for i, _ in enumerate(unreal.NiagaraEmitterService.list_renderers(BASE + 'NS_GroundBurningBullet', str(emitter))):
        details = unreal.NiagaraEmitterService.get_renderer_details(BASE + 'NS_GroundBurningBullet', str(emitter), i)
        assert str(details.material_path).startswith(BASE + 'M_GroundBurningBullet'), str(details)
        bindings.append(str(details))
burst = runpy.run_path(str(ROOT / 'Scripts/GroundMech/Rogue/author_burning_burst.py'), run_name='ground_burst')['build']()
report = {'success': True, 'shader_migrated': before != after, 'palette': palette, 'bindings': bindings, 'burst': burst,
          'bullet_diagnostics': unreal.GuLiCombatEffectAuthoringLibrary.get_buff_compile_diagnostics(bullet),
          'visual_acceptance': 'pending_player'}
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'vfx_install.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'report': str(OUT / 'vfx_install.json')}))
