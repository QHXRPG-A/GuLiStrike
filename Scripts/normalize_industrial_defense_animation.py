"""Keep this rotation-only mechanical demo in its imported skeleton's units."""
import unreal

OWNER = 'GuLiStrike.IndustrialDefenseSet.V3'
VERSION = 'reference-transforms-v1'


def normalize_animation(animation, mesh):
    lib = unreal.EditorAssetLibrary
    assert lib.get_metadata_tag(animation, 'GuLi.ModelProduction.Owner') == OWNER
    assert animation.get_editor_property('skeleton') == mesh.skeleton
    bones = list(unreal.SkeletonService.list_bones(mesh.get_path_name()))
    assert [str(b.bone_name) for b in bones] == ['root', 'base_yaw', 'barrel_pitch']
    samples = int(round(animation.get_play_length() * 30)) + 1
    before = {}
    rows = []
    needs_update = lib.get_metadata_tag(animation, 'GuLi.ModelProduction.AnimationUnits') != VERSION
    for bone in bones:
        name = str(bone.bone_name)
        poses = [unreal.AnimationLibrary.get_bone_pose_for_time(animation, name, i / 30, False)
                 for i in range(samples)]
        reference = bone.local_transform
        before[name] = {'animation_translation': list(poses[0].translation.to_tuple()),
                        'animation_scale': list(poses[0].scale3d.to_tuple()),
                        'reference_translation': list(reference.translation.to_tuple()),
                        'reference_scale': list(reference.scale3d.to_tuple())}
        # These authored tracks rotate only. Do not apply this normalization to
        # animation with intended translation, recoil, deformation or scale keys.
        for pose in poses:
            assert (pose.translation - poses[0].translation).length() < .001
            assert (pose.scale3d - poses[0].scale3d).length() < .001
            needs_update |= (pose.translation - reference.translation).length() > .001 or (pose.scale3d - reference.scale3d).length() > .001
        rotations = [reference.rotation] * samples if name == 'root' else [p.rotation for p in poses]
        rows.append((name, [reference.translation] * samples, rotations, [reference.scale3d] * samples))
    if needs_update:
        controller = animation.controller
        controller.open_bracket('Match mechanical animation to skeleton reference units', False)
        try:
            for name, positions, rotations, scales in rows:
                assert controller.set_bone_track_keys(name, positions, rotations, scales, False), name
        finally:
            controller.close_bracket(False)
        lib.set_metadata_tag(animation, 'GuLi.ModelProduction.AnimationUnits', VERSION)
        assert lib.save_loaded_asset(animation, False)
    for bone in bones:
        for t in [0, 2, 5]:
            pose = unreal.AnimationLibrary.get_bone_pose_for_time(animation, bone.bone_name, t, False)
            assert (pose.translation - bone.local_transform.translation).length() < .001
            assert (pose.scale3d - bone.local_transform.scale3d).length() < .001
    return {'version': VERSION, 'success': True, 'samples': samples, 'before': before}
