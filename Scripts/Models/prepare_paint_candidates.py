"""Independent color-only working copies; cache original display LODs before paint.

The source art and source UE assets are retained. A canonical per-triangle
display snapshot must agree before formal promotion; vertex splitting for
color seams may change buffer layout but never triangle surfaces/UV/weights.
"""
import json,sys
from pathlib import Path
import unreal
ROOT=Path(r'D:/UE5.7/test1')
sys.path.insert(0,str(ROOT/'Scripts'))
from Models import model_catalog
OUT=ROOT/'ArtSource/ModelInterface_B_20261008'
DIR=OUT/'PaintGeometry'
DIR.mkdir(exist_ok=True)
DEST='/Game/GuLiStrike/Review/ModelInterface'
contract=json.loads((OUT/'region-contract.json').read_text(encoding='utf8'))
entries=[]
derived=dict(next(m for m in contract['models'] if m['name']=='BiZhiMao'))
derived['name']='BiZhiMaoConstruction'
for model in contract['models']+[derived]:
    d=model_catalog.definition(model_catalog.model_id(model['name']))
    parts=[p for p in model_catalog.rows('Parts') if p['ModelId']==d['Id']] if d['ResourceType']=='PresentationClass' else [dict(PartKey='Root',ChildModelId=d['Id'])]
    for part in parts:
        child=model_catalog.definition(part['ChildModelId'])
        source=unreal.load_object(None,child['ResourcePath'])
        folder=DEST+'/'+model['name']+('/'+part['PartKey'] if part['PartKey']!='Root' else '')+'/Meshes'
        path=folder+'/'+source.get_name()
        old=unreal.load_asset(path)
        if old:
            if unreal.EditorAssetLibrary.get_metadata_tag(old,'GuLi.ModelApproved')=='1':raise RuntimeError('Frozen formal-approved copy must not be recreated: '+path)
            if not unreal.EditorAssetLibrary.delete_asset(path):raise RuntimeError('Unable to reset failed independent mesh '+path)
        copy=unreal.EditorAssetLibrary.duplicate_asset(child['ResourcePath'].split('.')[0],path)
        before=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(source))
        count=len(before['render_lods'])
        nanite=isinstance(copy,unreal.StaticMesh) and copy.get_editor_property('nanite_settings').get_editor_property('enabled')
        # Nanite fallback is not the source model. Keep its complete authored
        # source instead of accidentally replacing it with the fallback mesh.
        cached=(isinstance(copy,unreal.StaticMesh) and not nanite) or model['name']=='WM01' or any(l.get('generated_from_source_lod') for l in before['lods'])
        if cached:
            for lod in range(count):
                result=json.loads(unreal.GuLiModelAuthoringLibrary.preserve_display_lod_for_paint(copy,lod))
                if not result['success']:raise RuntimeError(result['error'])
        if isinstance(copy,unreal.StaticMesh):
            editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            for lod in range(count):
                settings=editor.get_lod_build_settings(copy,lod)
                if cached:settings.set_editor_property('build_scale3d',unreal.Vector(1,1,1))
                settings.set_editor_property('generate_lightmap_u_vs',False)
                editor.set_lod_build_settings(copy,lod,settings)
        slots=copy.get_editor_property('materials' if isinstance(copy,unreal.SkeletalMesh) else 'static_materials')
        protected=[]
        for i,s in enumerate(slots):
            mat=s.material_interface
            base=mat
            while isinstance(base,unreal.MaterialInstanceConstant):base=base.get_editor_property('parent')
            if not base or base.get_editor_property('blend_mode')!=unreal.BlendMode.BLEND_OPAQUE or any(t in str(s.material_slot_name).lower() for t in ['outline','contour','display','glass']):protected.append(i)
        files=[]
        for lod in range(count):
            f=DIR/(model['name']+'__'+part['PartKey']+'__LOD'+str(lod)+'__source.json')
            result=json.loads(unreal.GuLiModelAuthoringLibrary.write_mesh_paint_geometry(copy,lod,str(f),False))
            if not result['success']:raise RuntimeError(result['error'])
            files.append(dict(lod=lod,rendered=False,file=str(f.relative_to(OUT)),result=result))
        base=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(copy))
        entries.append(dict(model=model['name'],part=part['PartKey'],id=child['Id'],resource=child['ResourcePath'],candidate=copy.get_path_name(),
            protected_slots=protected,snapshot=before,base_snapshot=base,cached_display_source=cached,nanite_source_preserved=nanite,files=files))
        unreal.EditorAssetLibrary.set_metadata_tag(copy,'GuLi.PaintBaseSnapshot',json.dumps(base,sort_keys=True))
        unreal.EditorAssetLibrary.set_metadata_tag(copy,'GuLi.OriginalDisplaySnapshot',json.dumps(before,sort_keys=True))
        assert unreal.EditorAssetLibrary.save_loaded_asset(copy)
        (OUT/'paint-geometry-index.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf8')
(OUT/'paint-geometry-index.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'meshes':len(entries),'display_LODs':sum(len(e['files']) for e in entries)}))
