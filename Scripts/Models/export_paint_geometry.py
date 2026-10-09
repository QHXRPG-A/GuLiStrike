"""Read-only original source/display geometry and material-slot inventory for B paint transfer."""
import json,re,sys
from pathlib import Path
import unreal
ROOT=Path(r'D:/UE5.7/test1')
sys.path.insert(0,str(ROOT/'Scripts'))
from Models import model_catalog
OUT=ROOT/'ArtSource/ModelInterface_B_20261008'
DIR=OUT/'OriginalGeometry'
DIR.mkdir(exist_ok=True)
contract=json.loads((OUT/'region-contract.json').read_text(encoding='utf8'))
report=[]
for model in contract['models']:
    d=model_catalog.definition(model_catalog.model_id(model['name']))
    children=[p for p in model_catalog.rows('Parts') if p['ModelId']==d['Id']] if d['ResourceType']=='PresentationClass' else [dict(PartKey='Root',ChildModelId=d['Id'])]
    for part in children:
        child=model_catalog.definition(part['ChildModelId'])
        source=unreal.load_object(None,child['ResourcePath'])
        slots=source.get_editor_property('materials' if isinstance(source,unreal.SkeletalMesh) else 'static_materials')
        protected=[]
        for i,s in enumerate(slots):
            m=s.material_interface
            base=m
            while isinstance(base,unreal.MaterialInstanceConstant):base=base.get_editor_property('parent')
            if not base or base.get_editor_property('blend_mode')!=unreal.BlendMode.BLEND_OPAQUE or any(t in str(s.material_slot_name).lower() for t in ['outline','contour','display','glass']):protected.append(i)
        snapshot=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(source))
        lods=list(range(len(snapshot['render_lods'])))
        exports=[]
        for lod in lods:
            for rendered in [False,True]:
                # High-poly AA editor source is inspected separately. Its runtime reduction must be preserved.
                if model['name']=='MissileTurret' and not rendered:continue
                f=DIR/(model['name']+'__'+part['PartKey']+'__LOD'+str(lod)+('__render' if rendered else '__source')+'.json')
                result=json.loads(unreal.GuLiModelAuthoringLibrary.write_mesh_paint_geometry(source,lod,str(f),rendered))
                exports.append(dict(lod=lod,rendered=rendered,file=str(f.relative_to(OUT)),result=result))
        report.append(dict(model=model['name'],part=part['PartKey'],id=child['Id'],resource=child['ResourcePath'],protected_slots=protected,snapshot=snapshot,files=exports))
(OUT/'original-geometry-index.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'meshes':len(report),'exports':sum(len(r['files']) for r in report)}))
