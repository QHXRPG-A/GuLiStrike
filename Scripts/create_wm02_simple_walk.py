"""Create a small in-place tripod-gait AnimSequence for the simplified WM02 rig."""

import json
import math
import traceback

import unreal


SKELETON_PATH = "/Game/GuLiStrike/Robots/WM02/SK_WM02_Hexapod_Simple"
SAVE_PATH = "/Game/GuLiStrike/Robots/WM02/Animations"
ANIM_NAME = "AS_WM02_Walk_Simple"
ANIM_PATH = f"{SAVE_PATH}/{ANIM_NAME}"
DURATION = 2.0
FRAME_RATE = 30.0
TIMES = [index * 0.25 for index in range(9)]

# Alternating tripod gait: three legs support while the other three swing.
TRIPOD_A = {"LF", "RM", "LR"}
LEG_CODES = ("LF", "RF", "LM", "RM", "LR", "RR")


def reference_keyframe(bone_name):
    return unreal.AnimSequenceService.get_reference_pose_keyframe(
        SKELETON_PATH, bone_name, 0.0
    )


def make_keyframe(reference, time, rotation_delta=(0.0, 0.0, 0.0), position_delta=None):
    keyframe = unreal.AnimKeyframe()
    keyframe.time = time
    if position_delta is None:
        keyframe.position = reference.position
    else:
        keyframe.position = unreal.Vector(
            reference.position.x + position_delta[0],
            reference.position.y + position_delta[1],
            reference.position.z + position_delta[2],
        )
    delta = unreal.AnimSequenceService.euler_to_quat(*rotation_delta)
    keyframe.rotation = unreal.AnimSequenceService.multiply_quats(
        reference.rotation, delta
    )
    keyframe.scale = reference.scale
    return keyframe


def make_rotation_track(bone_name, rotation_samples):
    reference = reference_keyframe(bone_name)
    track = unreal.BoneTrackData()
    track.bone_name = bone_name
    track.keyframes = [
        make_keyframe(reference, time, rotation)
        for time, rotation in zip(TIMES, rotation_samples)
    ]
    return track


def build_tracks():
    tracks = []

    body_reference = reference_keyframe("Body")
    body_track = unreal.BoneTrackData()
    body_track.bone_name = "Body"
    body_track.keyframes = []
    for time in TIMES:
        cycle = 2.0 * math.pi * time / DURATION
        bounce = 35.0 * abs(math.sin(cycle))
        body_track.keyframes.append(
            make_keyframe(body_reference, time, (0.0, 0.0, 0.0), (0.0, 0.0, bounce))
        )
    tracks.append(body_track)

    for code in LEG_CODES:
        phase = 0.0 if code in TRIPOD_A else math.pi
        side_sign = -1.0 if code.startswith("L") else 1.0
        hip_samples = []
        knee_samples = []
        ankle_samples = []
        for time in TIMES:
            angle = 2.0 * math.pi * time / DURATION + phase
            swing = math.sin(angle)
            lift = max(0.0, swing)
            stance = max(0.0, -swing)

            # Bones were authored with identity local axes. Roll swings in the
            # asset's Y/Z plane; mirrored Pitch adds a small clearance lift.
            hip_samples.append((12.0 * swing, side_sign * 5.0 * lift, 0.0))
            knee_samples.append((-18.0 * lift + 5.0 * stance, 0.0, 0.0))
            ankle_samples.append((11.0 * lift - 3.0 * stance, 0.0, 0.0))

        tracks.append(make_rotation_track(f"Leg_{code}_Hip", hip_samples))
        tracks.append(make_rotation_track(f"Leg_{code}_Knee", knee_samples))
        tracks.append(make_rotation_track(f"Leg_{code}_Ankle", ankle_samples))

    return tracks


def main():
    report = {"success": False, "errors": []}
    try:
        if unreal.EditorAssetLibrary.does_asset_exist(ANIM_PATH):
            raise RuntimeError(f"Animation already exists; refusing to overwrite: {ANIM_PATH}")

        profile = unreal.SkeletonService.create_skeleton_profile(SKELETON_PATH)
        if not profile.is_valid or profile.bone_count != 26:
            raise RuntimeError(
                f"Unexpected skeleton profile: valid={profile.is_valid}, bones={profile.bone_count}"
            )

        tracks = build_tracks()
        created_path = unreal.AnimSequenceService.create_anim_sequence(
            SKELETON_PATH,
            ANIM_NAME,
            SAVE_PATH,
            DURATION,
            FRAME_RATE,
            tracks,
        )
        if not created_path:
            raise RuntimeError("create_anim_sequence returned an empty path")
        if not unreal.EditorAssetLibrary.save_asset(created_path, only_if_is_dirty=False):
            raise RuntimeError("Failed to save the new animation")

        info = unreal.AnimSequenceService.get_anim_sequence_info(created_path)
        animated_bones = [
            str(name)
            for name in unreal.AnimSequenceService.get_animated_bones(created_path)
        ]
        unreal.AnimSequenceService.open_animation_editor(created_path)
        report.update(
            {
                "success": True,
                "anim_path": str(created_path),
                "duration": float(info.duration),
                "frame_rate": float(info.frame_rate),
                "frame_count": int(info.frame_count),
                "bone_track_count": int(info.bone_track_count),
                "animated_bones": animated_bones,
            }
        )
    except Exception:
        report["errors"].append(traceback.format_exc())
    print(json.dumps(report, ensure_ascii=False, default=str))


if __name__ == "__main__":
    main()
