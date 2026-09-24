"""Commandlet entry point; author assets without interrupting the main editor."""
import json
from pathlib import Path
import unreal

source = Path('D:/UE5.7/test1/Scripts/Cards/author_card_reveal_demo.py').read_text(encoding='utf-8')
requested = unreal.SystemLibrary.get_command_line().split('-CardRevealPhase=')
phases = requested[1].split()[0].split(',') if len(requested) > 1 else ['foundation', 'card', 'hud', 'director', 'controller', 'validate']
results = {}
for phase in phases:
    context = {'CARD_REVEAL_PHASE': phase}
    exec(compile(source, 'author_card_reveal_demo.py', 'exec'), context)
    results[phase] = context.get('REPORT', {})
    if not results[phase].get('success'):
        break
Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo/authoring-result.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'phases': list(results), 'success': all(r.get('success') for r in results.values())}))
