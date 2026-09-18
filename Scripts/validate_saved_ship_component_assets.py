"""Read back saved formal assets against their preserved pre-import copies."""
import collections
import hashlib
import itertools
import json
import math
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1/ArtSource/Ships/ShipComponentStyle_20260917')
OUT=ROOT/'UE_Integration'
def dump(data):(OUT/'saved_asset_readback.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
def points(mesh):
    md=mesh.get_static_mesh_description(0)
    return [tuple(md.get_vertex_position(unreal.VertexID(i)).to_tuple()) for i in range(md.get_vertex_count())]
def nearest_error(a,b):
    buckets=collections.defaultdict(list);cell=.01
    for p in b:buckets[tuple(math.floor(v/cell) for v in p)].append(p)
    maximum=0.
    for p in a:
        base=tuple(math.floor(v/cell) for v in p)
        neighbors=[q for d in itertools.product((-1,0,1),repeat=3) for q in buckets.get(tuple(x+y for x,y in zip(base,d)),())]
        assert neighbors,('Missing preserved vertex',p)
        error=min(math.dist(p,q) for q in neighbors);maximum=max(maximum,error)
    return maximum
def collision(mesh):
    setup=mesh.get_editor_property('body_setup')
    if not setup:return None
    geo=setup.get_editor_property('agg_geom')
    arrays={name:len(geo.get_editor_property(name)) for name in ('box_elems','sphere_elems','sphyl_elems','convex_elems','tapered_capsule_elems')}
    return {'trace_flag':str(setup.get_editor_property('collision_trace_flag')),'simple_shapes':arrays,
        'aggregate_geometry_sha256':hashlib.sha256(geo.export_text().encode('utf-8')).hexdigest()}
def run():
    imported=json.loads((OUT/'formal_import_report.json').read_text(encoding='utf-8'));assert imported['success']
    report={'passed':False,'parts':{},'scope':'Saved assets only; exact formal paths, original sockets/rigs, original static positions/collision, texture configuration.'}
    for key,row in imported['parts'].items():
        mesh=unreal.load_asset(row['mesh']);assert mesh
        sk=isinstance(mesh,unreal.SkeletalMesh)
        backup=unreal.load_asset(imported['rollback'][row['mesh']]);assert backup
        fbx=ROOT/row['source_fbx']
        assert unreal.EditorAssetLibrary.get_metadata_tag(mesh,'GuLi.SurfaceSourceSHA256')==hashlib.sha256(fbx.read_bytes()).hexdigest()
        materials=mesh.materials if sk else mesh.static_materials
        assert all(m.material_interface.get_path_name()==row['material'] for m in materials)
        assert all(m.overlay_material_interface.get_path_name()==row['overlay_material'] for m in materials)
        rec={'material_slots':len(materials),'all_materials_styled':True,'outline_overlay_retained':True,'source_hash_matches':True}
        subsystem=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem) if sk else None
        lod_count=subsystem.get_lod_count(mesh) if sk else mesh.get_num_lods()
        assert lod_count==(subsystem.get_lod_count(backup) if sk else backup.get_num_lods())
        rec['lod_count']=lod_count
        if sk:
            current=unreal.SkeletonService.list_bones(mesh.get_path_name())
            assert [str(b.bone_name) for b in current]==['Root','BarrelPitch']
            assert mesh.skeleton.get_path_name()==row['skeleton']
            assert not mesh.physics_asset
            rec.update(bones=['Root','BarrelPitch'],physics_asset=None)
        else:
            before,after=points(backup),points(mesh)
            error=max(nearest_error(before,after),nearest_error(after,before));assert error<.01,(key,error)
            old_collision=collision(backup);new_collision=collision(mesh)
            assert old_collision==new_collision,(key,old_collision,new_collision)
            rec.update(max_original_geometry_error_cm=error,collision_preserved=True,collision=new_collision)
        sockets=[]
        for i in range(1,65):
            name='Socket_'+str(i);a=backup.find_socket(name);b=mesh.find_socket(name)
            assert bool(a)==bool(b),(key,name)
            if a:
                assert (a.relative_location-b.relative_location).length()<.00001
                assert (a.relative_scale-b.relative_scale).length()<.00001
                qa,qb=a.relative_rotation.quaternion(),b.relative_rotation.quaternion()
                assert abs(sum(getattr(qa,c)*getattr(qb,c) for c in ('x','y','z','w')))>.999999
                if sk:assert str(a.bone_name)==str(b.bone_name)
                sockets.append(name)
        assert len(sockets)==row['socket_count'];rec['sockets']=sockets
        textures=[]
        for role in ('BaseColor','ORM','LineMask'):
            path=f'/Game/GuLiStrike/Ship/StylizedComponents/Textures/T_SC_{key}_{role}_2K'
            texture=unreal.load_asset(path);assert texture
            size=[texture.blueprint_get_size_x(),texture.blueprint_get_size_y()]
            assert size==[2048,2048] and texture.get_editor_property('srgb')==(role=='BaseColor')
            if role!='BaseColor':assert texture.get_editor_property('compression_settings')==unreal.TextureCompressionSettings.TC_MASKS
            textures.append({'role':role,'size':size,'sRGB':texture.get_editor_property('srgb')})
        rec['textures']=textures;rec['passed']=True;report['parts'][key]=rec
    report['passed']=True;dump(report)

if __name__=='__main__':
    try:run()
    except Exception:dump({'passed':False,'error':traceback.format_exc()})
    finally:unreal.SystemLibrary.quit_editor()
