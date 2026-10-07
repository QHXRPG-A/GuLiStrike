"""Preserve original scripts as evidence and retire superseded production defaults."""
import hashlib,json,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/CommanderLOD_20261005/Reports'
routes={
 'build_lod_review.py':('build_review.py','python'),
 'build_review.py':('build_review.py','python'),
 'capture_art_preview.py':('capture_preview.py','unreal'),
 'convert_native_captures.py':('convert_preview.py','blender'),
 'import_art_assets.py':('stage_bizhimao.py','unreal'),
 'import_vertex_assets.py':('stage_bizhimao.py','unreal'),
 'prepare_acceptance_scene.py':('build_review_scene.py','unreal'),
 'prepare_blender.py':('prepare_bizhimao.py','blender'),
 'prepare_lod_blender_view.py':('prepare_bizhimao.py','blender'),
 'readback_art.py':('inspect_candidates.py','unreal'),
 'readback_vertex_assets.py':('inspect_candidates.py','unreal'),
 'render_lod_review.py':('render_bizhimao.py','blender'),
 'render_review.py':('render_bizhimao.py','blender'),
 'compress_lods.py':('','info'),'check_lod_geometry.py':('','info'),
 'export_static_meshes.py':('','info'),'inspect_lod_topology.py':('','info'),
 'probe_lod_reduction.py':('','info'),'probe_part_lods.py':('','info'),'probe_planar_lods.py':('','info'),
 'refine_lod_candidate.py':('','info'),'report_lod_blender_open.py':('','info'),
 'verify_art_assets.py':('','info'),
 'deploy_runtime_assets.py':('','info'),
 'readback_runtime_assets.py':('inspect_candidates.py','unreal'),
 'update_source_tables.py':('','info'),
}
paths={ROOT/'Scripts/BiZhiMao'/name:route for name,route in routes.items()}
paths.update({ROOT/'Scripts/Pioneer/import_art_assets.py':('stage_native.py','unreal'),
 ROOT/'Scripts/Pioneer/finalize_art_assets.py':('','info'),
 ROOT/'Scripts/Pioneer/capture_art_preview.py':('capture_preview.py','unreal'),
 ROOT/'Scripts/CommanderLOD/migrate_bizhimao_helpers.py':('','info'),
 ROOT/'Scripts/CommanderLOD/fix_preview_visibility.py':('','info'),
 ROOT/'Scripts/import_mass_rigid_animation.py':('','info'),
 ROOT/'Scripts/import_warmachine_production_v6.py':('stage_native.py','unreal'),
 ROOT/'Scripts/verify_warmachine_production_v6.py':('inspect_candidates.py','unreal'),
 ROOT/'Scripts/finalize_cel_model_assets.py':('','info'),
 ROOT/'Scripts/Blender/export_mass_rigid_editable.py':('export_rigid_candidates.py','blender')})
record=[];archive=OUT/'legacy_producer_sources.zip'
known=set()
if archive.exists():
 with zipfile.ZipFile(archive) as stream:known=set(stream.namelist())
with zipfile.ZipFile(archive,'a',zipfile.ZIP_DEFLATED) as stream:
 for path,(entry,mode) in paths.items():
  relative=path.relative_to(ROOT).as_posix();old=path.read_bytes()
  if relative not in known:stream.writestr(relative,old)
  source=('"""Compatibility entry for current Commander three-tier candidates."""\n'
   'import sys\nfrom pathlib import Path\n'
   "sys.path.insert(0,str(Path('D:/UE5.7/test1/Scripts/CommanderLOD')))\n"
   'from legacy_entry import run\n'+f'run({entry!r},{mode!r})\n')
  path.write_text(source,encoding='utf8')
  record.append(dict(path=relative,last_before_sha256=hashlib.sha256(old).hexdigest(),
    after_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),current_entry=entry or 'current source information'))
with zipfile.ZipFile(archive) as stream:
 for row in record:row['original_sha256']=hashlib.sha256(stream.read(row['path'])).hexdigest()
(OUT/'legacy_producer_migration.json').write_text(json.dumps(dict(success=True,lod_count=3,
 archive=str(archive.relative_to(ROOT)),scripts=record),ensure_ascii=False,indent=2),encoding='utf8')
print('RETIRED_PRODUCERS',len(record))
