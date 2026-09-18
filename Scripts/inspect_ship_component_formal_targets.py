"""Read-only formal Ship targets and renderer/import API inspection."""
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1/ArtSource/Ships/ShipComponentStyle_20260917')
OUT=ROOT/'UE_Integration'
def main():
    rows={}
    for batch in ('','Batch02','Batch03','Batch04'):
        snap=json.loads((ROOT/batch/'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
        for key,p in snap['parts'].items():
            mesh=unreal.load_asset(p['visual_mesh']);assert mesh,key
            bp=unreal.load_asset(p['blueprint']);cdo=unreal.get_default_object(bp.generated_class())
            sk=isinstance(mesh,unreal.SkeletalMesh)
            bounds=mesh.get_imported_bounds() if sk else mesh.get_bounds()
            slots=mesh.materials if sk else mesh.static_materials
            row={'path':mesh.get_path_name(),'class':mesh.get_class().get_name(),
                'bounds':{'origin':list(bounds.origin.to_tuple()),'extent':list(bounds.box_extent.to_tuple())},
                'materials':[{'name':str(s.material_slot_name),'path':s.material_interface.get_path_name() if s.material_interface else None} for s in slots],
                'blueprint':p['blueprint'],'overrides':[m.get_path_name() if m else None for m in cdo.get_editor_property('override_materials')],
                'overlay_slot_available': 'overlay_material_interface' in dir(slots[0]) if slots else None,
                'overlay_asset_available': 'overlay_material' in dir(mesh),
                'UV_APIs':[n for n in dir(mesh) if 'uv' in n.lower()],
                'negative_bounds_extension':list(mesh.get_editor_property('negative_bounds_extension').to_tuple()),
                'positive_bounds_extension':list(mesh.get_editor_property('positive_bounds_extension').to_tuple())}
            if sk:
                row['skeleton']=mesh.skeleton.get_path_name()
                row['physics']=mesh.physics_asset.get_path_name() if mesh.physics_asset else None
                row['mesh_bones']=[str(b.bone_name) for b in unreal.SkeletonService.list_bones(mesh.get_path_name())]
                row['skeleton_bones']=[str(b.bone_name) for b in unreal.SkeletonService.list_bones(mesh.skeleton.get_path_name())]
            else:
                row['triangles']=mesh.get_num_triangles(0)
            rows[key]=row
    result={'success':True,'parts':rows,'skeletal_editor_api':[n for n in dir(unreal.SkeletalMeshEditorSubsystem) if any(w in n.lower() for w in ('uv','lod','build'))],
        'material_errors_API':[n for n in dir(unreal.MaterialEditingLibrary) if any(w in n for w in ('error','stat'))]}
    filename='formal_targets_probe.json' if '-AfterImportProbe' in unreal.SystemLibrary.get_command_line() else 'formal_targets_before.json'
    (OUT/filename).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__':
    try:main()
    except Exception:(OUT/'formal_targets_error.txt').write_text(traceback.format_exc(),encoding='utf-8')
    finally:unreal.SystemLibrary.quit_editor()
