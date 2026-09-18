"""ShipComponent rigid turret authoring. Invoke named stages through UE Python.

Source assets are read-only. The immutable baseline records sockets and geometry;
only task-owned assets under ShipComponent/Rigged may be imported or updated.
"""
import hashlib
import json
from pathlib import Path

import unreal

PROJECT = Path('D:/UE5.7/test1')
OUT = PROJECT / 'outputs/ship-component-rigs'
ART = PROJECT / 'ArtSource/Ships/ShipComponentRigs'
SOURCE = '/Game/Assets/Ships/ShipComponent'
DEST = SOURCE + '/Rigged'
OWNER = 'GuLi.ShipComponentRigs.20260910'
COUNTS = {
    'Autocannon': 3, 'Bottom_Twin_Barrel_Turret': 2, 'CIWS': 3,
    'High_Rate_Fire_Cannon': 2, 'Single_Barrel_Turret': 1,
    'Triple_Barrel_Turret': 3, 'Twin_Barrel_Turret': 2,
}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


def transform(t):
    return {'translation': vec(t.translation),
            'rotation': [t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w],
            'scale': vec(t.scale3d)}


def inspect_source(asset):
    comp = unreal.new_object(unreal.StaticMeshComponent)
    comp.set_static_mesh(asset)
    sockets = []
    for name in comp.get_all_socket_names():
        s = asset.find_socket(name)
        sockets.append({'name': str(name), 'location': vec(s.relative_location),
                        'rotation': [s.relative_rotation.pitch, s.relative_rotation.yaw, s.relative_rotation.roll],
                        'scale': vec(s.relative_scale), 'tag': str(s.tag)})
    md = asset.get_static_mesh_description(0)
    points = [vec(md.get_vertex_position(unreal.VertexID(i))) for i in range(md.get_vertex_count())]
    triangles = []
    for i in range(md.get_triangle_count()):
        t = unreal.TriangleID(i)
        if md.is_triangle_valid(t):
            # UE5.7 GetTriangleVertices appends to an uninitialized array. The
            # vertex-instance route returns exactly three valid vertex IDs.
            triangles.append([int(md.get_vertex_instance_vertex(v).id_value)
                              for v in md.get_triangle_vertex_instances(t)])
    unseen = set(range(len(points)))
    components = []
    while unseen:
        seed = min(unseen)
        unseen.remove(seed)
        stack, ids = [seed], [seed]
        while stack:
            for neighbor in md.get_vertex_adjacent_vertices(unreal.VertexID(stack.pop())):
                j = int(neighbor.id_value)
                if j in unseen:
                    unseen.remove(j)
                    stack.append(j)
                    ids.append(j)
        components.append({'seed': seed, 'ids': sorted(ids),
                           'min': [min(points[i][a] for i in ids) for a in range(3)],
                           'max': [max(points[i][a] for i in ids) for a in range(3)]})
    geo = {'points': points, 'triangles': triangles}
    package = asset.get_path_name().split('.')[0]
    disk = PROJECT / 'Content' / (package.removeprefix('/Game/') + '.uasset')
    stat = disk.stat()
    bounds = asset.get_bounding_box()
    return {'path': package, 'sockets': sorted(sockets, key=lambda s: s['name']),
            'vertices': len(points), 'triangles': len(triangles), 'lods': asset.get_num_lods(),
            'bounds': [vec(bounds.min), vec(bounds.max)],
            'materials': [{'slot': str(s.material_slot_name),
                           'path': s.material_interface.get_path_name() if s.material_interface else None}
                          for s in asset.static_materials],
            'geometry_sha256': hashlib.sha256(json.dumps(geo, separators=(',', ':')).encode()).hexdigest(),
            'file_stat': {'size': stat.st_size, 'mtime_ns': stat.st_mtime_ns},
            'geometry': geo, 'components': components}


def find_uses():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                include_hard_package_references=True)
    paths = {SOURCE + '/SM_SC_' + n for n in COUNTS}
    refs = {p: [str(x) for x in ar.get_referencers(p, opts)] for p in sorted(paths)}
    components = []
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            if c.static_mesh and c.static_mesh.get_path_name().split('.')[0] in paths:
                components.append({'actor': a.get_path_name(), 'component': c.get_name(),
                                   'mesh': c.static_mesh.get_path_name(), 'transform': transform(c.get_world_transform())})
    return {'asset_referencers': refs, 'level_components': components}


def snapshot_export():
    OUT.mkdir(parents=True, exist_ok=True)
    (ART / 'SourceFBX').mkdir(parents=True, exist_ok=True)
    lib = unreal.EditorAssetLibrary
    assets = unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(SOURCE, recursive=False)
    current = {str(a.asset_name): inspect_source(a.get_asset()) for a in assets
               if str(a.asset_class_path.asset_name) == 'StaticMesh'}
    baseline_path = OUT / 'source-baseline.json'
    if baseline_path.exists():
        baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
        for name, row in baseline['assets'].items():
            for key in ['sockets', 'geometry_sha256', 'materials', 'file_stat']:
                if current[name][key] != row[key]:
                    raise RuntimeError('Source changed since baseline: ' + name + '/' + key)
    else:
        baseline = {'owner': OWNER, 'assets': current, 'uses': find_uses(),
                    'world': unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name(),
                    'dirty_content_before': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
        write_json(baseline_path, baseline)
    for name in COUNTS:
        for prefix in ['SKM_SC_', 'SK_SC_']:
            path = DEST + '/' + prefix + name
            if lib.does_asset_exist(path):
                obj = unreal.load_asset(path)
                if lib.get_metadata_tag(obj, 'GuLi.RigOwner') != OWNER:
                    raise RuntimeError('Unowned destination: ' + path)
        target = ART / 'SourceFBX' / ('SM_SC_' + name + '.fbx')
        if not target.exists():
            task = unreal.AssetExportTask()
            task.object = unreal.load_asset(SOURCE + '/SM_SC_' + name)
            task.filename = str(target)
            task.automated = True
            task.prompt = False
            task.replace_identical = False
            task.exporter = unreal.StaticMeshExporterFBX()
            options = unreal.FbxExportOption()
            options.set_editor_property('ascii', False)
            options.set_editor_property('collision', False)
            options.set_editor_property('level_of_detail', False)
            task.options = options
            if not unreal.Exporter.run_asset_export_task(task):
                raise RuntimeError('FBX export failed: ' + name)
    return {'success': True, 'source_assets': len(current), 'turrets': len(COUNTS),
            'existing_sockets': {n: len(r['sockets']) for n, r in current.items() if r['sockets']},
            'uses': find_uses(), 'export_folder': str(ART / 'SourceFBX')}


def save_owned(obj):
    unreal.EditorAssetLibrary.set_metadata_tag(obj, 'GuLi.RigOwner', OWNER)
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError('Cannot save ' + obj.get_path_name())


def normalize_reference(mesh, pivot, sign):
    """Bake FBX unit/axis offsets into a clean, two-bone reference skeleton.

Only these newly authored meshes are supported. A temporary leaf rename forces
UE5.7 to synchronize the Skeleton reference pose as well as the mesh pose.
"""
    mod = unreal.SkeletonModifier()
    if not mod.set_skeletal_mesh(mesh):
        raise RuntimeError('Cannot edit authored reference skeleton')
    root_t = unreal.Transform()
    barrel_t = unreal.Transform(location=pivot, rotation=unreal.Rotator(yaw=90, roll=180 if sign < 0 else 0))
    if not mod.set_bone_transform('Root', root_t, True):
        raise RuntimeError('Cannot normalize root')
    if not mod.set_bone_transform('BarrelPitch', barrel_t, True):
        raise RuntimeError('Cannot normalize barrel frame')
    temporary = 'BarrelPitch_BindSync'
    if not mod.rename_bone('BarrelPitch', temporary) or not mod.commit_skeleton_to_skeletal_mesh():
        raise RuntimeError('Cannot synchronize clean reference')
    mod2 = unreal.SkeletonModifier()
    if not mod2.set_skeletal_mesh(mesh) or not mod2.rename_bone(temporary, 'BarrelPitch'):
        raise RuntimeError('Cannot restore authored bone name')
    if not mod2.commit_skeleton_to_skeletal_mesh():
        raise RuntimeError('Cannot commit restored bone name')
    actual = list(unreal.SkeletonService.list_bones(mesh.get_path_name()))
    sk = list(unreal.SkeletonService.list_bones(mesh.skeleton.get_path_name()))
    if len(actual) != 2 or len(sk) != 2:
        raise RuntimeError('Reference synchronization added unexpected bones')
    for a, b in zip(actual, sk):
        if a.bone_name != b.bone_name or (a.local_transform.translation-b.local_transform.translation).length() > .0001:
            raise RuntimeError('Mesh and Skeleton reference mismatch')
        if (a.global_transform.scale3d-unreal.Vector(1,1,1)).length() > .0001:
            raise RuntimeError('Bone still has an FBX unit scale')
        if abs(sum(getattr(a.local_transform.rotation,k)*getattr(b.local_transform.rotation,k) for k in ['x','y','z','w'])) < .999999:
            raise RuntimeError('Mesh and Skeleton rotation mismatch')


def import_one(name):
    if name not in COUNTS:
        raise ValueError(name)
    lib = unreal.EditorAssetLibrary
    manifest = json.loads((OUT / 'authoring-manifest.json').read_text(encoding='utf-8'))
    baseline = json.loads((OUT / 'source-baseline.json').read_text(encoding='utf-8'))
    source_info = baseline['assets']['SM_SC_' + name]
    source = unreal.load_asset(source_info['path'])
    current = inspect_source(source)
    if any(current[k] != source_info[k] for k in ['file_stat', 'sockets', 'geometry_sha256', 'materials']):
        raise RuntimeError('Source changed during authoring: ' + name)
    mesh_path = DEST + '/SKM_SC_' + name
    skeleton_path = DEST + '/SK_SC_' + name
    for path in [mesh_path, skeleton_path]:
        if lib.does_asset_exist(path) and lib.get_metadata_tag(unreal.load_asset(path), 'GuLi.RigOwner') != OWNER:
            raise RuntimeError('Unowned asset ' + path)
    lib.make_directory(DEST)
    skeleton = unreal.load_asset(skeleton_path) if lib.does_asset_exist(skeleton_path) else None
    opts = unreal.FbxImportUI()
    for k, v in dict(import_materials=False, import_textures=False, import_as_skeletal=True,
                     import_mesh=True, import_animations=False, create_physics_asset=False,
                     automated_import_should_detect_type=False, skeleton=skeleton,
                     mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH).items():
        opts.set_editor_property(k, v)
    data = opts.skeletal_mesh_import_data
    for k, v in dict(convert_scene=True, convert_scene_unit=True, force_front_x_axis=False,
                     import_uniform_scale=1.0, import_mesh_lo_ds=False,
                     normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
                     vertex_color_import_option=unreal.VertexColorImportOption.REPLACE,
                     update_skeleton_reference_pose=False, use_t0_as_ref_pose=False,
                     preserve_smoothing_groups=True).items():
        data.set_editor_property(k, v)
    filename = ART / 'Exports' / ('SKM_SC_' + name + '.fbx')
    task = unreal.AssetImportTask()
    for k, v in dict(filename=str(filename), destination_path=DEST, destination_name='SKM_SC_' + name,
                     automated=True, async_=False, replace_existing=True,
                     replace_existing_settings=True, save=False, factory=unreal.FbxFactory(), options=opts).items():
        task.set_editor_property(k, v)
    # Explicitly use the legacy FBX factory. Restore the editor's setting afterwards.
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cvar = 'Interchange.FeatureFlags.Import.Enable'
    previous = unreal.SystemLibrary.get_console_variable_int_value(cvar)
    unreal.SystemLibrary.execute_console_command(world, cvar + ' 0')
    try:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    finally:
        unreal.SystemLibrary.execute_console_command(world, cvar + ' ' + str(previous))
    mesh = unreal.load_asset(mesh_path)
    if mesh is None:
        raise RuntimeError('Mesh import failed: ' + str(task.imported_object_paths))
    lib.set_metadata_tag(mesh, 'GuLi.RigOwner', OWNER)
    if skeleton is None:
        skeleton = mesh.skeleton
        if not skeleton:
            raise RuntimeError('FBX importer did not create a skeleton')
        lib.set_metadata_tag(skeleton, 'GuLi.RigOwner', OWNER)
        if skeleton.get_path_name().split('.')[0] != skeleton_path:
            if not lib.rename_asset(skeleton.get_path_name(), skeleton_path):
                raise RuntimeError('Cannot name imported skeleton')
    slots = list(mesh.materials)
    if len(slots) != len(source_info['materials']):
        raise RuntimeError('Unexpected material slots')
    for slot, original in zip(slots, source_info['materials']):
        slot.set_editor_property('material_interface', unreal.load_asset(original['path']))
        slot.set_editor_property('material_slot_name', original['slot'])
    mesh.set_editor_property('materials', slots)
    if mesh.skeleton != skeleton:
        raise RuntimeError('Wrong skeleton assigned')
    if mesh.get_editor_property('physics_asset'):
        raise RuntimeError('Unexpected generated PhysicsAsset')
    bones = list(unreal.SkeletonService.list_bones(mesh_path))
    if [str(b.bone_name) for b in bones] != ['Root', 'BarrelPitch']:
        raise RuntimeError('Unexpected hierarchy: ' + str([b.bone_name for b in bones]))
    root, barrel = bones
    if root.global_transform.translation.length() > .01:
        raise RuntimeError('Root moved away from original asset origin')
    pivot = unreal.Vector(*manifest[name]['pivot_cm'])
    if (barrel.global_transform.translation - pivot).length() > .02:
        raise RuntimeError('Imported pivot/unit mismatch: ' + str(barrel.global_transform.translation))
    normalize_reference(mesh, pivot, manifest[name]['positive_elevation_mesh_z_sign'])
    bones = list(unreal.SkeletonService.list_bones(mesh_path))
    lib.set_metadata_tag(mesh, 'GuLi.RigSource', source_info['path'])
    lib.set_metadata_tag(mesh, 'GuLi.RigElevationRange', '-15..75 degrees; local Y; reference-relative')
    lib.set_metadata_tag(mesh, 'GuLi.RigPositiveElevation', 'Outward -Z' if name.startswith('Bottom_') else 'Outward +Z')
    save_owned(skeleton)
    save_owned(mesh)
    result = {'mesh': mesh_path, 'skeleton': skeleton_path,
              'bones': [{'name': str(b.bone_name), 'parent': str(b.parent_bone_name),
                         'local': transform(b.local_transform), 'global': transform(b.global_transform)} for b in bones],
              'material_slots': [str(s.material_slot_name) for s in mesh.materials],
              'bounds': str(mesh.get_bounds()), 'physics_asset': None}
    write_json(OUT / (name + '-ue-import.json'), result)
    return result


def finish_one(name):
    """Attach muzzle sockets and extend render bounds for the allowed arc."""
    mesh_path = DEST + '/SKM_SC_' + name
    mesh = unreal.load_asset(mesh_path)
    if unreal.EditorAssetLibrary.get_metadata_tag(mesh, 'GuLi.RigOwner') != OWNER:
        raise RuntimeError('Unowned destination ' + mesh_path)
    baseline = json.loads((OUT / 'source-baseline.json').read_text(encoding='utf-8'))
    original = baseline['assets']['SM_SC_' + name]
    current = inspect_source(unreal.load_asset(original['path']))
    if current['sockets'] != original['sockets']:
        raise RuntimeError('Source sockets changed; refresh preservation manifest before continuing')
    meta = json.loads((OUT / 'authoring-manifest.json').read_text(encoding='utf-8'))[name]
    bones = {str(b.bone_name): b.global_transform for b in unreal.SkeletonService.list_bones(mesh_path)}
    forward = unreal.Rotator(yaw=90, roll=180 if meta['positive_elevation_mesh_z_sign'] < 0 else 0)
    desired = []
    # The execution snapshot contains no sockets on these seven source meshes.
    # Keep the guard explicit: a later socket addition must be classified first.
    if original['sockets']:
        raise RuntimeError('Existing sockets need explicit fixed/moving classification')
    for i, position in enumerate(meta['muzzles_cm'], 1):
        name_socket = 'Socket_' + str(i)
        reference = unreal.Transform(location=unreal.Vector(*position), rotation=forward)
        relative = reference.make_relative(bones['BarrelPitch'])
        socket = mesh.find_socket(name_socket)
        if socket:
            actual = unreal.Transform(location=socket.relative_location, rotation=socket.relative_rotation,
                                      scale=socket.relative_scale)
            if str(socket.bone_name) != 'BarrelPitch' or not transforms_match(actual, relative):
                raise RuntimeError('Existing socket has been adjusted: ' + mesh_path + '/' + name_socket)
        elif not unreal.SkeletonService.add_socket(mesh_path, name_socket, 'BarrelPitch',
                    relative.translation, relative.rotation.rotator(), relative.scale3d, False):
            raise RuntimeError('Could not create ' + name_socket)
        desired.append({'name': name_socket, 'bone': 'BarrelPitch',
                        'relative': transform(relative), 'reference': transform(reference)})
    bounds = mesh.get_imported_bounds()
    reference_min, reference_max = bounds.origin - bounds.box_extent, bounds.origin + bounds.box_extent
    motion_min, motion_max = meta['motion_bounds_cm']
    # One centimetre of numerical padding; geometry and installation origin do not move.
    mesh.set_editor_property('negative_bounds_extension', unreal.Vector(*[
        max(0, x - y + 1) for x, y in zip(vec(reference_min), motion_min)]))
    mesh.set_editor_property('positive_bounds_extension', unreal.Vector(*[
        max(0, y - x + 1) for x, y in zip(vec(reference_max), motion_max)]))
    unreal.EditorAssetLibrary.set_metadata_tag(mesh, 'GuLi.RigSocketPreservation',
                                              json.dumps({'source_sockets': original['sockets'], 'muzzles': desired}))
    save_owned(mesh)
    write_json(OUT / (name + '-sockets.json'), desired)
    return {'name': name, 'sockets': len(desired), 'bounds': str(mesh.get_bounds())}


def transforms_match(a, b, tolerance=.01):
    return ((a.translation - b.translation).length() < tolerance
            and (a.scale3d - b.scale3d).length() < .00001
            and abs(sum(getattr(a.rotation, k) * getattr(b.rotation, k) for k in ['x', 'y', 'z', 'w'])) > .999999)


def validate_assets():
    manifest = json.loads((OUT / 'authoring-manifest.json').read_text(encoding='utf-8'))
    sources = json.loads((OUT / 'source-baseline.json').read_text(encoding='utf-8'))
    geometry = json.loads((OUT / 'final-source-geometry.json').read_text(encoding='utf-8'))
    result = {}
    for name, count in COUNTS.items():
        path = DEST + '/SKM_SC_' + name
        mesh = unreal.load_asset(path)
        bones = list(unreal.SkeletonService.list_bones(path))
        assert [str(b.bone_name) for b in bones] == ['Root', 'BarrelPitch']
        assert str(bones[1].parent_bone_name) == 'Root'
        assert transforms_match(bones[0].global_transform, unreal.Transform())
        assert mesh.skeleton.get_name() == 'SK_SC_' + name
        assert not mesh.physics_asset and not mesh.post_process_anim_blueprint
        lods = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_lod_count(mesh)
        assert lods == 1
        modifier = unreal.SkinWeightModifier()
        assert modifier.set_skeletal_mesh(mesh)
        weight_counts = {'Root': 0, 'BarrelPitch': 0}
        for i in range(modifier.get_num_vertices()):
            w = {str(k): v for k, v in modifier.get_vertex_weights(i).items() if v > .000001}
            assert len(w) == 1 and abs(next(iter(w.values())) - 1) < .000001, (name, i, w)
            weight_counts[next(iter(w))] += 1
        assert all(weight_counts.values())
        # FBX welds coincident points. Each exported vertex still has one rigid influence.
        sockets = list(unreal.SkeletonService.list_sockets(path))
        assert len(sockets) == count
        source = sources['assets']['SM_SC_' + name]
        assert len(mesh.materials) == len(source['materials'])
        assert [s.material_interface.get_path_name() for s in mesh.materials] == [s['path'] for s in source['materials']]
        expected_bounds = geometry[name]['reference_bounds_cm']
        b = mesh.get_imported_bounds()
        bound_error = max(abs(x-y) for actual, expected in zip([vec(b.origin-b.box_extent), vec(b.origin+b.box_extent)], expected_bounds)
                          for x,y in zip(actual, expected))
        assert bound_error < .05, (name, bound_error)
        for i, p in enumerate(manifest[name]['muzzles_cm'], 1):
            s = mesh.find_socket('Socket_' + str(i))
            assert str(s.bone_name) == 'BarrelPitch'
            actual = unreal.Transform(location=s.relative_location, rotation=s.relative_rotation,
                                      scale=s.relative_scale).multiply(bones[1].global_transform)
            assert (actual.translation - unreal.Vector(*p)).length() < .01
            assert (actual.rotation.rotate_vector(unreal.Vector(1,0,0))-unreal.Vector(0,1,0)).length() < .0001
        result[name] = {'bones': 2, 'lods': lods, 'rigid_weighted_vertices': weight_counts,
                        'sockets': len(sockets), 'material_matches': True,
                        'reference_bounds_max_error_cm': bound_error, 'physics_asset': None}
    untouched = {}
    for name, before in sources['assets'].items():
        after = inspect_source(unreal.load_asset(before['path']))
        for k in ['geometry_sha256', 'materials', 'sockets', 'file_stat']:
            assert before[k] == after[k], (name, k)
        untouched[name] = len(after['sockets'])
    report = {'assets': result, 'source_assets_unchanged': untouched, 'uses': find_uses()}
    write_json(OUT / 'ue-asset-validation.json', report)
    return report
