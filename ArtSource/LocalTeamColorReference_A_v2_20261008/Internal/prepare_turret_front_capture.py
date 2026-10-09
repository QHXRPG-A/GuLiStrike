"""Add the +Y facing view for existing turrets whose bore axis is +Y."""
from pathlib import Path
folder=Path(__file__).resolve().parent
text=(folder/'capture_added_original_views.py').read_text(encoding='utf8')
text=text.replace("('MissileTurret','SentryTurret','ResourceFactory')", "('MissileTurret','SentryTurret')")
text=text.replace("len(report['models'])==3", "len(report['models'])==2")
text=text.replace("source-capture-added-buildings.json", "source-capture-turret-front.json")
text=text.replace("for variant in ('Hero','Front','Left','Back'):", "for variant in ('Right',):")
text=text.replace("        if variant=='Front':", "        if variant=='Right':eye=center+unreal.Vector(0,diameter*2,0)\n        elif variant=='Front':")
assert 'save_loaded_asset' not in text and 'save_current_level' not in text
target=folder/'capture_turret_front.py'
target.write_text(text,encoding='utf8')
print(target)
