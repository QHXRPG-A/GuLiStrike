"""Numerically validate the generated WM02 simple walk animation."""

import json
import math
import traceback

import unreal


ANIM_PATH = "/Game/GuLiStrike/Robots/WM02/Animations/AS_WM02_Walk_Simple"
EXPECTED_TRACKS = {
    "Body",
    *{
        f"Leg_{code}_{joint}"
        for code in ("LF", "RF", "LM", "RM", "LR", "RR")
        for joint in ("Hip", "Knee", "Ankle")
    },
}


def components(transform):
    rotation = transform.rotation
    translation = transform.translation
    scale = transform.scale3d
    return (
        rotation.x,
        rotation.y,
        rotation.z,
        rotation.w,
        translation.x,
        translation.y,
        translation.z,
        scale.x,
        scale.y,
        scale.z,
    )


def step_metrics(first, second):
    first_rotation = first.rotation
    second_rotation = second.rotation
    dot = abs(
        first_rotation.x * second_rotation.x
        + first_rotation.y * second_rotation.y
        + first_rotation.z * second_rotation.z
        + first_rotation.w * second_rotation.w
    )
    angular_degrees = math.degrees(2.0 * math.acos(max(-1.0, min(1.0, dot))))
    position_distance = math.sqrt(
        (first.translation.x - second.translation.x) ** 2
        + (first.translation.y - second.translation.y) ** 2
        + (first.translation.z - second.translation.z) ** 2
    )
    scale_distance = max(
        abs(first.scale3d.x - second.scale3d.x),
        abs(first.scale3d.y - second.scale3d.y),
        abs(first.scale3d.z - second.scale3d.z),
    )
    return position_distance, angular_degrees, scale_distance


def euler_at(bone_name, time):
    transform = unreal.AnimSequenceService.get_bone_transform_at_time(
        ANIM_PATH, bone_name, time, False
    )
    rotator = unreal.AnimSequenceService.quat_to_euler(transform.rotation)
    return {
        "roll": round(float(rotator.roll), 3),
        "pitch": round(float(rotator.pitch), 3),
        "yaw": round(float(rotator.yaw), 3),
    }


def main():
    report = {"success": False, "checks": {}, "errors": []}
    try:
        info = unreal.AnimSequenceService.get_anim_sequence_info(ANIM_PATH)
        animated_bones = {
            str(name) for name in unreal.AnimSequenceService.get_animated_bones(ANIM_PATH)
        }
        actual_by_lower = {name.lower(): name for name in animated_bones}
        expected_lower = {name.lower() for name in EXPECTED_TRACKS}
        report["checks"]["metadata"] = {
            "duration": float(info.duration),
            "frame_rate": float(info.frame_rate),
            "frame_count": int(info.frame_count),
            "bone_track_count": int(info.bone_track_count),
            "valid": (
                abs(float(info.duration) - 2.0) <= 1.0e-6
                and abs(float(info.frame_rate) - 30.0) <= 1.0e-6
                and int(info.frame_count) == 61
                and int(info.bone_track_count) == 19
            ),
        }
        report["checks"]["track_set"] = {
            "valid": set(actual_by_lower) == expected_lower,
            "actual": sorted(animated_bones),
        }

        # UE's decompressed sampler treats the terminal sample as the last
        # playable frame, not as a duplicate of frame zero. A good loop should
        # therefore have a boundary step no larger than its normal frame steps.
        loop_mismatches = []
        loop_boundary = {}
        sample_times = [frame / 30.0 for frame in range(60)]
        for bone_name in sorted(EXPECTED_TRACKS):
            samples = [
                unreal.AnimSequenceService.get_bone_transform_at_time(
                    ANIM_PATH, bone_name, time, False
                )
                for time in sample_times
            ]
            interior = [
                step_metrics(samples[index - 1], samples[index])
                for index in range(1, len(samples))
            ]
            boundary = step_metrics(samples[-1], samples[0])
            maxima = tuple(max(values) for values in zip(*interior))
            valid_boundary = (
                boundary[0] <= maxima[0] * 1.1 + 0.02
                and boundary[1] <= maxima[1] * 1.1 + 0.05
                and boundary[2] <= maxima[2] * 1.1 + 1.0e-5
            )
            loop_boundary[bone_name] = {
                "position": round(boundary[0], 4),
                "angle_degrees": round(boundary[1], 4),
            }
            if not valid_boundary:
                loop_mismatches.append(bone_name)
        report["checks"]["loop"] = {
            "valid": not loop_mismatches,
            "mismatches": loop_mismatches,
            "boundary_steps": loop_boundary,
        }

        invalid_pose_values = []
        pose_sizes = {}
        for time in (0.0, 0.5, 1.0, 1.5, 2.0):
            pose = unreal.AnimSequenceService.get_pose_at_time(ANIM_PATH, time, False)
            pose_sizes[str(time)] = len(pose)
            for bone_pose in pose:
                if not all(math.isfinite(value) for value in components(bone_pose.transform)):
                    invalid_pose_values.append(f"{bone_pose.bone_name}@{time}")
        report["checks"]["poses"] = {
            "valid": all(size == 26 for size in pose_sizes.values()) and not invalid_pose_values,
            "pose_sizes": pose_sizes,
            "invalid_values": invalid_pose_values,
        }

        phase_samples = {
            "LF_hip_0.5": euler_at("Leg_LF_Hip", 0.5),
            "RF_hip_0.5": euler_at("Leg_RF_Hip", 0.5),
            "LF_hip_1.5": euler_at("Leg_LF_Hip", 1.5),
            "RF_hip_1.5": euler_at("Leg_RF_Hip", 1.5),
            "LF_knee_0.5": euler_at("Leg_LF_Knee", 0.5),
            "RF_knee_0.5": euler_at("Leg_RF_Knee", 0.5),
            "LF_knee_1.5": euler_at("Leg_LF_Knee", 1.5),
            "RF_knee_1.5": euler_at("Leg_RF_Knee", 1.5),
        }
        phase_valid = (
            phase_samples["LF_hip_0.5"]["roll"] > 10.0
            and phase_samples["RF_hip_0.5"]["roll"] < -10.0
            and phase_samples["LF_hip_1.5"]["roll"] < -10.0
            and phase_samples["RF_hip_1.5"]["roll"] > 10.0
            and phase_samples["LF_knee_0.5"]["roll"] < -15.0
            and phase_samples["RF_knee_1.5"]["roll"] < -15.0
        )
        report["checks"]["alternating_tripods"] = {
            "valid": phase_valid,
            "samples": phase_samples,
        }

        body_start = unreal.AnimSequenceService.get_bone_transform_at_time(
            ANIM_PATH, "Body", 0.0, False
        )
        body_mid = unreal.AnimSequenceService.get_bone_transform_at_time(
            ANIM_PATH, "Body", 0.5, False
        )
        body_bounce = float(body_mid.translation.z - body_start.translation.z)
        report["checks"]["body_bounce"] = {
            "valid": abs(body_bounce - 35.0) <= 0.01,
            "delta_z": round(body_bounce, 3),
        }

        report["success"] = all(
            check["valid"] for check in report["checks"].values()
        )
    except Exception:
        report["errors"].append(traceback.format_exc())
    print(json.dumps(report, ensure_ascii=False, default=str))


if __name__ == "__main__":
    main()
