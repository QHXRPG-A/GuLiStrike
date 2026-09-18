"""Render the four frozen originals for reference A; no production materials."""
import bpy
import hashlib
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Ships/ShipComponentStyle_20260917/Batch04')
OUT = ROOT / 'Source/Previews'
KEYS = ('Bottom_Twin_Barrel_Turret', 'High_Rate_Fire_Cannon', 'Incendiary_Bomb_LaunchBay', 'Missile_Bay')
VIEWS = {'hero': (1.1,-1.7,1.65), 'front': (0,-1,0), 'right': (1,0,0), 'rear': (0,1,0), 'top': (0,0,1)}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    target = ROOT / 'Source/prototype_render_manifest_v1.json'
    assert not target.exists(), 'Original inspection set already frozen'
    snapshot = json.loads((ROOT / 'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
    assert snapshot['success'] and snapshot['source_packages_unchanged']
    OUT.mkdir(parents=True, exist_ok=True)
    report = {'purpose': 'Unchanged original sources for reference A; neutral material only', 'parts': {}}
    for key in KEYS:
        part = snapshot['parts'][key]
        bpy.ops.wm.read_factory_settings(use_empty=True)
        scene = bpy.context.scene
        scene.name = key + '_Original_Inspection'
        scene.unit_settings.system = 'METRIC'
        scene.unit_settings.scale_length = 1
        if part.get('bones'):
            source_record = next(r for r in part['preserved_authoring_sources'] if r['path'].endswith('.blend'))
            source = ROOT / source_record['path']
            assert sha(source) == source_record['sha256']
            with bpy.data.libraries.load(str(source), link=False) as (available, loaded):
                loaded.objects = [n for n in available.objects if n in ('SKM_SC_' + key, 'Armature')]
            for obj in loaded.objects:
                scene.collection.objects.link(obj)
            arm = next(o for o in loaded.objects if o.type == 'ARMATURE')
            arm.animation_data_clear()
            for bone in arm.pose.bones:
                bone.matrix_basis.identity()
            body = next(o for o in loaded.objects if o.type == 'MESH')
        else:
            source_record = part['exports'][0]
            source = ROOT / source_record['path']
            assert sha(source) == source_record['sha256']
            bpy.ops.import_scene.fbx(filepath=str(source), use_anim=False)
            bodies = [o for o in scene.objects if o.type == 'MESH']
            assert len(bodies) == 1
            body, arm = bodies[0], None
        scene.frame_set(0)
        body['purpose'] = 'Frozen original geometry; inspection reference only'
        body['original_source_sha256'] = sha(source)
        body.data.materials.clear()
        mat = bpy.data.materials.new('Inspection_Only_Neutral')
        mat.diffuse_color = (.53,.58,.61,1)
        body.data.materials.append(mat)
        for poly in body.data.polygons:
            poly.material_index = 0
        scene.render.engine = 'BLENDER_WORKBENCH'
        scene.render.resolution_x, scene.render.resolution_y = 1440, 1080
        scene.render.resolution_percentage = 100
        scene.render.image_settings.file_format = 'PNG'
        sh = scene.display.shading
        sh.light, sh.studiolight_rotate_z = 'STUDIO', .6
        sh.color_type, sh.show_shadows = 'MATERIAL', True
        sh.show_cavity, sh.cavity_type = True, 'BOTH'
        sh.show_object_outline, sh.background_type = True, 'WORLD'
        scene.world = bpy.data.worlds.new('InspectionBackground')
        scene.world.color = (.16,.20,.23)
        scene.view_settings.view_transform = 'Standard'
        camera = bpy.data.objects.new('OriginalReferenceCamera', bpy.data.cameras.new('OriginalReferenceCamera'))
        scene.collection.objects.link(camera)
        scene.camera, camera.data.type = camera, 'ORTHO'
        points = [body.matrix_world @ v.co for v in body.data.vertices]
        lo = Vector([min(p[i] for p in points) for i in range(3)])
        hi = Vector([max(p[i] for p in points) for i in range(3)])
        center, span = (lo+hi)*.5, max(hi-lo)
        camera.data.clip_start, camera.data.clip_end = .01, span*30
        views = {}
        for name, direction in VIEWS.items():
            direction = Vector(direction).normalized()
            camera.location = center + direction*span*3
            camera.rotation_euler = (-direction).to_track_quat('-Z','Y').to_euler()
            bpy.context.view_layer.update()
            pts = [camera.matrix_world.inverted() @ p for p in points]
            width = max(p.x for p in pts) - min(p.x for p in pts)
            height = max(p.y for p in pts) - min(p.y for p in pts)
            shift = Vector(((max(p.x for p in pts)+min(p.x for p in pts))*.5,
                            (max(p.y for p in pts)+min(p.y for p in pts))*.5,0))
            camera.location += camera.rotation_euler.to_quaternion() @ shift
            camera.data.ortho_scale = max(width,height*4/3)*1.18
            if name == 'hero':
                hero = (camera.location.copy(), camera.rotation_euler.copy(), camera.data.ortho_scale)
            image = OUT / f'{key}_original_{name}_v1.png'
            scene.render.filepath = str(image)
            bpy.ops.render.render(write_still=True)
            views[name] = {'path': image.relative_to(ROOT).as_posix(), 'sha256': sha(image)}
        camera.location, camera.rotation_euler, camera.data.ortho_scale = hero
        body.data.calc_loop_triangles()
        bpy.context.preferences.filepaths.save_version = 0
        inspection = ROOT / 'Source' / f'{key}_Original_Inspection.blend'
        bpy.ops.wm.save_as_mainfile(filepath=str(inspection), compress=True)
        report['parts'][key] = {'original_source': source.relative_to(ROOT).as_posix(), 'original_source_sha256': sha(source),
            'inspection_blend': inspection.relative_to(ROOT).as_posix(), 'inspection_blend_sha256': sha(inspection),
            'dimensions_m': list(hi-lo), 'vertices': len(body.data.vertices), 'triangles': len(body.data.loop_triangles),
            'source_matrix_world': [list(row) for row in body.matrix_world],
            'mechanical_type': 'skeletal' if arm else 'static', 'bones': [b.name for b in arm.data.bones] if arm else [],
            'views': views}
    target.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('BATCH04_ORIGINAL_REFERENCE_RENDERS_COMPLETE',flush=True)


if __name__ == '__main__':
    main()
