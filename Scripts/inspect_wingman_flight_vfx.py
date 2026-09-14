"""Read-only discovery for the Wingman flight effect."""
import json
from pathlib import Path
import unreal

report = {}
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
report['editor_world'] = str(editor.get_editor_world())
report['game_world'] = str(editor.get_game_world())
report['dirty_packages'] = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report['systems'] = {}
for token in ('Wingman', 'Missile', 'Exhaust', 'Trail'):
    report['systems'][token] = [str(p) for p in unreal.NiagaraService.search_systems('/Game/GuLiStrike', token)]
mesh = unreal.load_asset('/Game/GuLiStrike/Wingman/SM_Wingman_Mass')
report['mesh'] = {'bounds': str(mesh.get_bounds())} if mesh else None
report['template'] = str(unreal.NiagaraService.summarize('/Game/GuLiStrike/FX/CommanderWeapons/NS_WM01_MissileFlight'))
report['api'] = {}
for cls, methods in ((unreal.NiagaraEmitterService, ('set_emitter_property', 'set_renderer_property', 'get_emitter_info', 'get_module_input', 'set_module_input')), (unreal.NiagaraService, ('summarize','get_system_info','compile_with_results')), (unreal.ScreenshotService, ('capture_editor_window','capture_asset_editor'))):
    for name in methods:
        method = getattr(cls, name, None)
        report['api'][cls.__name__+'.'+name] = method.__doc__ if method else None
out = Path('D:/UE5.7/test1/TestResults/WingmanFlightVFX')
out.mkdir(parents=True, exist_ok=True)
(out/'discovery.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
