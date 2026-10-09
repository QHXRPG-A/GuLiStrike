"""Read original/candidate mesh details through Unreal for a color-only migration."""
import json
import sys
from pathlib import Path
import unreal

ROOT=Path(r'D:/UE5.7/test1')
sys.path.insert(0,str(ROOT/'Scripts'))
from Models import model_catalog
OUT=ROOT/'ArtSource/ModelInterface_B_20261008'
names=['BiZhiMao','DefaultSoldier','WM01','ManualOutpost','SentryTurret','ShieldGenerator','SSF_AirBase','MissileTurret']
report=[]
for name in names:
    d=model_catalog.definition(model_catalog.model_id(name))
    source=unreal.load_object(None,d['ResourcePath'])
    candidate=unreal.load_asset('/Game/GuLiStrike/Review/ModelInterface/'+name+'/Meshes/'+source.get_name())
    slots=source.get_editor_property('materials' if isinstance(source,unreal.SkeletalMesh) else 'static_materials')
    materials=[]
    for s in slots:
        m=s.material_interface
        base=m
        while isinstance(base,unreal.MaterialInstanceConstant):base=base.get_editor_property('parent')
        materials.append(dict(slot=str(s.material_slot_name),material=m.get_path_name() if m else '',base=base.get_path_name() if base else '',blend=str(base.get_editor_property('blend_mode')) if base else ''))
    first=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(source))
    second=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(source))
    other=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(candidate)) if candidate else {}
    files=[]
    if name in ['SSF_AirBase','ShieldGenerator','MissileTurret','ManualOutpost','SentryTurret']:
        for rendered in ([False,True] if isinstance(source,unreal.StaticMesh) else [False]):
            file=OUT/('diagnostic-'+name+('-render' if rendered else '-source')+'.json')
            result=json.loads(unreal.GuLiModelAuthoringLibrary.write_mesh_paint_geometry(source,0,str(file),rendered))
            files.append(dict(file=str(file),result=result))
    report.append(dict(model=name,source=d['ResourcePath'],source_snapshot=first,repeat_equal=first==second,candidate_snapshot=other,materials=materials,files=files))
(OUT/'paint-correspondence-diagnostic.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'models':len(report),'unstable_snapshots':[r['model'] for r in report if not r['repeat_equal']]}))
