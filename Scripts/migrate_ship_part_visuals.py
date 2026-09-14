"""Snapshot and migrate ship part visuals without editing any mesh/socket assets."""
import json
from pathlib import Path
import unreal

PROJECT = Path('D:/UE5.7/test1')
OUT = PROJECT / 'outputs/ship-part-visual-migration'
BLUEPRINT = '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01'
PART_NATIVE = ('GuLiStrikeShipPartComponent', 'GuLiStrikeWeaponPart', 'GuLiStrikeEnginePart')
OWNER = 'GuLi.ShipPartVisuals.20260911'


def write(name, data):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')


def xyz(v):
    return [float(v.x), float(v.y), float(v.z)]


def rot(v):
    return [float(v.pitch), float(v.yaw), float(v.roll)]


def asset_path(obj):
    return obj.get_path_name() if obj else None


def transform(t):
    return {'location': xyz(t.translation), 'rotation': [t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],
            'scale': xyz(t.scale3d)}


def part_assets():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    return sorted([a for a in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'Blueprint'), True)
                   if any("." + name + "'" in str(a.get_tag_value('NativeParentClass')) for name in PART_NATIVE)],
                  key=lambda a: str(a.package_name))


def mesh_snapshot(mesh):
    if isinstance(mesh, unreal.StaticMesh):
        component = unreal.new_object(unreal.StaticMeshComponent)
        component.set_static_mesh(mesh)
    elif isinstance(mesh, unreal.SkeletalMesh):
        component = unreal.new_object(unreal.SkeletalMeshComponent)
        component.set_skeletal_mesh_asset(mesh)
    else:
        raise TypeError(str(mesh))
    sockets = []
    for name in component.get_all_socket_names():
        socket = mesh.find_socket(name)
        if not socket:continue  # Bone names are also returned by skeletal components.
        row = {'name': str(name), 'location': xyz(socket.relative_location),
               'rotation': rot(socket.relative_rotation), 'scale': xyz(socket.relative_scale)}
        if isinstance(mesh, unreal.StaticMesh):
            row['tag'] = str(socket.tag)
            row['preview_static_mesh'] = asset_path(socket.get_editor_property('preview_static_mesh'))
        else:
            row['bone'] = str(socket.bone_name)
        sockets.append(row)
    disk = PROJECT / 'Content' / (mesh.get_path_name().split('.')[0].removeprefix('/Game/') + '.uasset')
    stat = disk.stat()
    return {'class': mesh.get_class().get_name(), 'sockets': sorted(sockets, key=lambda x:x['name']),
            'file_stat': {'size':stat.st_size, 'mtime_ns':stat.st_mtime_ns}}


def snapshot():
    if (OUT/'baseline.json').exists():
        return verify_sockets()
    ship = unreal.get_default_object(unreal.load_asset(BLUEPRINT).generated_class())
    hull = ship.get_editor_property('hull_mesh')
    mesh_paths = {asset_path(hull.static_mesh)}
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in registry.get_assets_by_path('/Game/Assets/Ships/ShipComponent', recursive=True):
        if str(a.asset_class_path.asset_name) in ['StaticMesh', 'SkeletalMesh']:
            mesh_paths.add(asset_path(a.get_asset()))
    parts = {}
    for asset in part_assets():
        bp = asset.get_asset()
        cdo = unreal.get_default_object(bp.generated_class())
        fields = {}
        for key in ['part_id','compatible_sockets','part_relative_transform','part_mass','part_display_name',
                    'damage','fire_rate','projectile_class','muzzle_offset','thrust']:
            try:
                value = cdo.get_editor_property(key)
            except Exception:
                continue
            if key == 'part_relative_transform':value=transform(value)
            elif key == 'muzzle_offset':value=xyz(value)
            elif key == 'compatible_sockets':value=[str(v) for v in value]
            elif key == 'projectile_class':value=asset_path(value)
            elif not isinstance(value,(str,float,int,bool)):value=str(value)
            fields[key]=value
        visual = {'static_mesh':asset_path(cdo.get_editor_property('static_mesh')),
                  'override_materials':[asset_path(m) for m in cdo.get_editor_property('override_materials')]}
        for key in ['cast_shadow','receives_decals','render_custom_depth','custom_depth_stencil_value',
                    'visible','hidden_in_game']:
            visual[key]=cdo.get_editor_property(key)
        parts[str(asset.package_name)]={'class':asset_path(bp.generated_class()), 'visual':visual, 'fields':fields}
        if visual['static_mesh']:mesh_paths.add(visual['static_mesh'])
    data={'owner':OWNER, 'world':unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name(),
          'ship_blueprint':BLUEPRINT, 'hull_mesh':asset_path(hull.static_mesh),
          'hull_relative_transform':transform(hull.get_relative_transform()),
          'default_parts':[{'socket':str(p.socket_name),'class':asset_path(p.part_class)} for p in ship.get_editor_property('default_parts')],
          'part_catalogue':[asset_path(p) for p in ship.get_editor_property('part_catalogue')],
          'parts':parts,
          'meshes':{path:mesh_snapshot(unreal.load_asset(path)) for path in sorted(mesh_paths) if path},
          'dirty_content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
          'dirty_maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
    write('baseline.json',data)
    return {'part_blueprints':list(parts),'hull_mesh':data['hull_mesh'],
            'socket_counts':{p:len(v['sockets']) for p,v in data['meshes'].items()},
            'dirty_content':data['dirty_content'],'dirty_maps':data['dirty_maps']}


def verify_sockets():
    baseline=json.loads((OUT/'baseline.json').read_text(encoding='utf-8'))
    result={}
    for path, before in baseline['meshes'].items():
        after=mesh_snapshot(unreal.load_asset(path))
        if before != after:raise RuntimeError('Protected mesh/socket changed: '+path)
        result[path]=len(after['sockets'])
    ship=unreal.get_default_object(unreal.load_asset(BLUEPRINT).generated_class())
    hull=ship.get_editor_property('hull_mesh')
    assert asset_path(hull.static_mesh)==baseline['hull_mesh']
    assert transform(hull.get_relative_transform())==baseline['hull_relative_transform']
    write('socket-verification.json',{'all_preserved':True,'meshes':result})
    return {'all_preserved':True,'mesh_count':len(result),'sockets':sum(result.values()),'socket_counts':result}


def decoded_field(name, value):
    if name == 'part_relative_transform':
        return unreal.Transform(location=unreal.Vector(*value['location']),
                                rotation=unreal.Quat(*value['rotation']).rotator(),
                                scale=unreal.Vector(*value['scale']))
    if name == 'muzzle_offset':return unreal.Vector(*value)
    if name == 'projectile_class':return unreal.load_class(None, value) if value else None
    return value


def compile_checked(path):
    result=unreal.BlueprintService.compile_blueprint(path)
    report={'success':bool(result.success),'errors':list(result.errors),'warnings':list(result.warnings)}
    if not result.success:raise RuntimeError('Blueprint compile failed: '+path+' '+json.dumps(report))
    return report


def migrate():
    """Run only after the cold native rebuild, then compile/resave the four parts."""
    baseline=json.loads((OUT/'baseline.json').read_text(encoding='utf-8'))
    verify_sockets()
    results={}
    for path, before in baseline['parts'].items():
        bp=unreal.load_asset(path)
        cdo=unreal.get_default_object(bp.generated_class())
        if isinstance(cdo, unreal.StaticMeshComponent):
            raise RuntimeError('Editor still has the old native superclass; restart after building')
        cdo.set_editor_property('visual_type',unreal.GuLiStrikeShipPartVisualType.STATIC_MESH)
        for key,value in before['visual'].items():
            if key=='static_mesh':value=unreal.load_asset(value) if value else None
            elif key=='override_materials':value=[unreal.load_asset(v) if v else None for v in value]
            cdo.set_editor_property(key,value)
        for key,value in before['fields'].items():
            cdo.set_editor_property(key,decoded_field(key,value))
        compiled=compile_checked(path)
        if not unreal.EditorAssetLibrary.save_loaded_asset(bp,False):raise RuntimeError('Save failed: '+path)
        results[path]={'compile':compiled,'visual_type':'StaticMesh','static_mesh':before['visual']['static_mesh']}
    # These actors keep their native HullMesh and inherited socket installation API.
    for path in ['/Game/GuLiStrike/Ship/BP_GuLiStrikeShip',BLUEPRINT]:
        bp=unreal.load_asset(path)
        results[path]={'compile':compile_checked(path)}
        if not unreal.EditorAssetLibrary.save_loaded_asset(bp,False):raise RuntimeError('Save failed: '+path)
    write('migration.json',results)
    return validate(compile_blueprints=False)


def validate(compile_blueprints=True):
    baseline=json.loads((OUT/'baseline.json').read_text(encoding='utf-8'))
    sockets=verify_sockets()
    parts={}
    for path,before in baseline['parts'].items():
        bp=unreal.load_asset(path)
        cdo=unreal.get_default_object(bp.generated_class())
        assert isinstance(cdo,unreal.SceneComponent) and not isinstance(cdo,unreal.StaticMeshComponent),path
        assert asset_path(cdo.get_editor_property('static_mesh'))==before['visual']['static_mesh'],path
        assert [asset_path(m) for m in cdo.get_editor_property('override_materials')]==before['visual']['override_materials'],path
        for key,expected in before['fields'].items():
            actual=cdo.get_editor_property(key)
            if key=='part_relative_transform':actual=transform(actual)
            elif key=='muzzle_offset':actual=xyz(actual)
            elif key=='compatible_sockets':actual=[str(v) for v in actual]
            elif key=='projectile_class':actual=asset_path(actual)
            elif not isinstance(actual,(float,int,bool,str)):actual=str(actual)
            assert actual==expected,(path,key,actual,expected)
        for key,expected in before['visual'].items():
            if key in ['static_mesh','override_materials']:continue
            assert cdo.get_editor_property(key)==expected,(path,key)
        compiled=compile_checked(path) if compile_blueprints else json.loads((OUT/'migration.json').read_text(encoding='utf-8'))[path]['compile']
        parts[path]={'compile':compiled,'old_visual_and_gameplay_defaults_preserved':True}
    ship=unreal.get_default_object(unreal.load_asset(BLUEPRINT).generated_class())
    assert [asset_path(p) for p in ship.get_editor_property('part_catalogue')]==baseline['part_catalogue']
    assert [{'socket':str(p.socket_name),'class':asset_path(p.part_class)} for p in ship.get_editor_property('default_parts')]==baseline['default_parts']
    result={'sockets':sockets,'parts':parts,'ship_catalogue_and_defaults_preserved':True}
    write('validation.json',result)
    return result
