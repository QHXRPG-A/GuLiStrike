"""Record actual prior/final effect sequences, without altering formal references."""
import json
from pathlib import Path
import run_four_stage_review as review
import capture_review_visuals as visuals

OUT = review.ROOT / 'outputs/performance/20261009-all-optimizations'
review.OUT = OUT
directory = OUT / 'visual-review'
directory.mkdir(exist_ok=True)
original = json.loads((review.ROOT/'outputs/performance/20261009-implementation/benchmark-original-cvars.json').read_text())
takes = []
try:
    review.launch('width','Source')
    for role, variants in [('Mining',('Opaque','All')),('Construction',('Opaque','All')),
                           ('Flash',('Muzzle','AllMuzzle','Impact','AllImpact'))]:
        for variant in variants:
            takes.append(visuals.record(role,variant,directory))
finally:
    (directory/'takes.json').write_text(json.dumps(takes,ensure_ascii=False,indent=2),encoding='utf-8')
    review.stop()
    review.result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+
                  '\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{f"{k} {v:g}"!r})' for k,v in original.items()))
