"""Prepare reference-only native views for two existing static models. No source asset is edited."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
source=(ROOT/'Scripts/SceneUI/capture_color_review.py').read_text(encoding='utf8')
source=source.replace("OUT=ROOT/'ArtSource/LocalTeamColorReview_20261008'","OUT=ROOT/'ArtSource/LocalTeamColorReference_A_v2_20261008'\nOLD=ROOT/'ArtSource/LocalTeamColorReview_20261008'")
source=source.replace("report=json.loads((OUT/'asset-authoring.json').read_text(encoding='utf8'))","report=json.loads((OLD/'asset-authoring.json').read_text(encoding='utf8'))\nreport['models']=[m for m in report['models'] if m['name'] in ('ShieldGenerator','ManualOutpost')]")
source=source.replace("len(report['models'])==18","len(report['models'])==2")
source=source.replace("runpy.run_path(str(ROOT/'Scripts/SceneUI/readback_review_materials.py'))","")
source=source.replace("'Renders'","'Sources'").replace("'Renders/","'Sources/")
source=source.replace("render-readback.json","source-capture.json")
source=source.replace("world,768,768","world,1280,1280")
source=source.replace("for variant in ('blue','red','regions'):","for variant in ('Hero','Front','Left','Back'):")
source=source.replace("path=slot.get(variant) if not slot['fixed'] else slot.get('source')","path='/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial'")
start="        if model['name']=='ShieldGenerator':eye=center+unreal.Vector(-diameter*.9,diameter*1.1,diameter*.65)"
source=source.replace(start,"        if variant=='Front':eye=center+unreal.Vector(diameter*2,0,0)\n        elif variant=='Left':eye=center+unreal.Vector(0,-diameter*2,0)\n        elif variant=='Back':eye=center+unreal.Vector(-diameter*2,0,0)")
source=source.replace("camera_pose_identical_for_variants=True","source_geometry_views_only=True")
assert 'save_loaded_asset' not in source and 'save_current_level' not in source
target=Path(__file__).with_name('capture_original_views.py')
target.write_text(source,encoding='utf8')
print(target)
