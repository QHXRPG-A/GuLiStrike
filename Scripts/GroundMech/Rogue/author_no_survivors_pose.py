"""Author and numerically check the A4B2 barrage on the production skeleton."""
import json
import math
import traceback
from pathlib import Path

import unreal as ue


def author():
    root = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
    card = next(r for r in json.loads((root / "Data/Json/DT_GuLiStrikeRogueCards_Cards.json").read_text(encoding="utf-8")) if r["Id"] == "13.01")
    folder, name = card["CastPose"].split(".")[0].rsplit("/", 1)
    pawn = ue.get_default_object(ue.EditorAssetLibrary.load_blueprint_class("/Game/GuLiStrike/GroundMech/BP_GroundMech_Light"))
    mesh = pawn.mesh.skeletal_mesh_asset
    idle = ue.load_asset(folder + "/Anim_Mecha_Idle")
    assert idle.get_skeleton() == mesh.skeleton
    rig = json.loads((root / "ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/ue_mesh_import.json").read_text())
    parents = {b["name"]: b["parent"] for b in rig["bones"]}
    names = list(parents)
    rest = dict(zip(names, ue.AnimationLibrary.get_bone_poses_for_frame(idle, names, 0, False)))
    def world(local):
        out = {}
        for n in names:
            out[n] = local[n] * out[parents[n]] if parents[n] else local[n].copy()
        return out
    sockets = {}
    for side, socket in (("L", "Muzzle_L"), ("R", "Muzzle")):
        s = mesh.find_socket(socket)
        sockets[side] = ue.Transform(location=s.relative_location, rotation=s.relative_rotation, scale=s.relative_scale) * rest["Gun_" + side]

    def pose(fold):
        out = {}
        for n in names:
            t = rest[n] * out[parents[n]] if parents[n] else rest[n].copy()
            side = n[-1]
            sign = 1 if side == "L" else -1
            if n == "MiddleJoint":
                t.translation += ue.Vector(0, -5 if fold else 0, -30 if fold else 8)
            elif n == "cockpit":
                angle = math.radians(-16 if fold else 8) / 2
                t.rotation = ue.Quat(math.sin(angle), 0, 0, math.cos(angle)) * t.rotation
            elif n.startswith(("GunArm_", "UpperLeg_", "Leg_", "Foot_")):
                if n.startswith("GunArm_"):
                    tip = sockets[side]
                    # Stack the housings vertically so the two barrels cross in front of the chest.
                    direction = (-sign * .88, .48, .23 if side == "L" else -.23) if fold else (sign * .80, .54, -.25)
                elif n.startswith("UpperLeg_"):
                    tip = rest["Leg_" + side]
                    direction = (-sign * .15, .96, -.24) if fold else (sign * .35, .24 if side == "L" else .13, -.91)
                elif n.startswith("Leg_"):
                    tip = rest["Foot_" + side]
                    direction = (-sign * .08, -.95, -.30) if fold else (sign * .22, -.31, -.93)
                else:
                    tip = rest["FootEnd_" + side]
                    direction = (sign * .07, .98, -.17) if fold else (sign * .18, .75, -.63)
                axis = ((tip * t).translation - t.translation).normal()
                t.rotation = axis.find_quat_between_normals(ue.Vector(*direction).normal()) * t.rotation
            out[n] = t
        for side in ("L", "R"):
            out["IK_Foot_" + side] = out["Foot_" + side].copy()
        return {n: out[n].make_relative(out[parents[n]]) if parents[n] else out[n] for n in names}

    rate = 60
    fold_frame = round(card["CastFoldSeconds"] * rate)
    hold_end = fold_frame + round(card["CastCrossedHoldSeconds"] * rate)
    last = hold_end + round(card["CastOpenSeconds"] * rate)
    folded, spread = pose(True), pose(False)
    asset_path = folder + "/" + name
    anim = ue.EditorAssetLibrary.load_asset(asset_path)
    if not anim:
        factory = ue.AnimSequenceFactory()
        factory.target_skeleton = mesh.skeleton
        factory.preview_skeletal_mesh = mesh
        anim = ue.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, ue.AnimSequence, factory)
    assert anim.get_skeleton() == mesh.skeleton

    gait = json.loads((root/"Data/Json/DT_GuLiStrikeMech_Gait.json").read_text(encoding="utf-8"))
    jump = next(row for row in gait if row["Name"] == "JumpLoop")
    hover = ue.load_asset(jump["Asset"])
    assert hover.get_skeleton() == anim.get_skeleton() and jump["BasePlayRate"] > 0
    leg_names = [n for n in names if n.startswith(("UpperLeg_", "Leg_", "Foot_", "FootEnd_"))]
    cycle = round(rate * hover.get_play_length() / jump["BasePlayRate"])
    end = last + cycle
    def hover_keys(frame):
        t = hover.get_play_length() * frame / cycle
        return dict(zip(leg_names, ue.AnimationLibrary.get_bone_poses_for_time(hover, leg_names, t, False)))

    def blend(start, finish, alpha):
        weight = alpha * alpha * (3. - 2. * alpha)
        result = {}
        for bone in names:
            a, b = start[bone], finish[bone]
            t = a.copy()
            t.translation += (b.translation - a.translation) * weight
            t.rotation = a.rotation.slerp_quat(b.rotation, weight)
            t.scale3d += (b.scale3d - a.scale3d) * weight
            result[bone] = t
        return result

    hover_start = hover_keys(0)
    neutral = rest | hover_start
    spread.update(hover_start)
    charge_degrees = card["CastChargeShakeDegrees"]
    release_degrees = card["CastReleaseShakeDegrees"]
    # On A4B2 both gun arms descend from cockpit. Move their common parent so
    # the crossed barrels keep their spacing and the hover legs remain independent.
    def move_chest(local, pitch, sway):
        if abs(pitch) + abs(sway) < 1.e-7:
            return
        cs = world(local)
        chest = cs["cockpit"].copy()
        chest.rotation = ue.Rotator(roll=pitch, pitch=.55*sway, yaw=.32*sway).quaternion() * chest.rotation
        chest.translation += ue.Vector(.85*sway, -1.65*pitch, .8*pitch)
        local["cockpit"] = chest.make_relative(cs[parents["cockpit"]])

    hold_seconds = (hold_end - fold_frame) / rate
    charge_cycles = max(1, round(7 * hold_seconds)), max(1, round(10 * hold_seconds))
    release_cycles = max(1, round(6.5 * cycle / rate)), max(1, round(9.25 * cycle / rate))
    frames = []
    for f in range(end + 1):
        if f < fold_frame:
            local = blend(neutral, folded, f / fold_frame)
        elif f <= hold_end:
            local = folded.copy()
            if hold_end > fold_frame:
                phase = (f - fold_frame) / (hold_end - fold_frame)
                envelope = .20 + .80 * phase * phase * (3. - 2. * phase)
                turns = math.tau * phase
                amplitude = charge_degrees * envelope
                move_chest(local, amplitude * math.sin(charge_cycles[0] * turns),
                           amplitude * math.sin(charge_cycles[1] * turns))
        elif f < last:
            alpha = (f - hold_end) / (last - hold_end)
            local = blend(folded, spread, alpha)
            # A single backward impulse while the shoulders open, outside the looping part.
            move_chest(local, 1.8 * charge_degrees * math.sin(math.pi * alpha)**2, 0.)
        else:
            local = spread.copy()
            local.update(hover_keys(f - last))
            turns = math.tau * (f - last) / cycle
            move_chest(local, release_degrees * math.sin(release_cycles[0] * turns),
                       release_degrees * math.sin(release_cycles[1] * turns))
        cs = world(local)
        for side in ("L", "R"):
            ik = "IK_Foot_" + side
            local[ik] = cs["Foot_" + side].make_relative(cs[parents[ik]])
        frames.append(local)

    ctl = anim.controller
    tracks = {str(n) for n in anim.data_model_interface.get_bone_track_names()}
    ctl.open_bracket("Bake crossed hold, deployment and hover from the Ranger card", False)
    try:
        ctl.set_frame_rate(ue.FrameRate(rate, 1), False)
        ctl.set_number_of_frames(ue.FrameNumber(end), False)
        for n in names:
            if n not in tracks:
                ctl.add_bone_track(n, False)
            keys = [frame[n] for frame in frames]
            assert ctl.set_bone_track_keys(n, [v.translation for v in keys], [v.rotation for v in keys],
                                          [v.scale3d for v in keys], False), n
        assert ue.GuLiGroundMechAnimationEditorLibrary.notify_animation_keys_populated(anim)
    finally:
        ctl.close_bracket(False)
    anim.set_editor_property("enable_root_motion", False)
    anim.set_editor_property("loop", False)

    def metrics(frame):
        keyed = dict(zip(names, ue.AnimationLibrary.get_bone_poses_for_frame(anim, names, frame, False)))
        cs = world(keyed)
        tips = [(sockets[s] * cs["GunArm_" + s]).translation for s in ("L", "R")]
        return dict(muzzle_span=round(tips[0].x-tips[1].x, 2), muzzle_z_separation=round(abs(tips[0].z-tips[1].z), 2),
                    foot_span=round(cs["Foot_L"].translation.x-cs["Foot_R"].translation.x, 2),
                    foot_z=round(cs["Foot_L"].translation.z, 2), root=list(cs["root"].translation.to_tuple()))
    base, cross, open_pose = (metrics(f) for f in (0, fold_frame, last))
    assert cross["muzzle_span"] < 0 and cross["muzzle_z_separation"] > 50 and open_pose["muzzle_span"] > base["muzzle_span"]
    assert open_pose["foot_span"] > cross["foot_span"] and open_pose["foot_z"] < cross["foot_z"]
    assert base["root"] == cross["root"] == open_pose["root"] and not anim.get_editor_property("enable_root_motion")
    assert anim.get_skeleton() == mesh.skeleton and abs(anim.get_play_length()-end/rate) < .0001
    assert anim.data_model_interface.get_number_of_keys() == end + 1
    arms = ["GunArm_L", "Gun_L", "GunArm_R", "Gun_R"]
    crossed = ue.AnimationLibrary.get_bone_poses_for_frame(anim, arms, fold_frame, False)
    def arm_error(frame):
        sample = ue.AnimationLibrary.get_bone_poses_for_frame(anim, arms, frame, False)
        return max(max((a.translation - b.translation).length(), math.degrees(a.rotation.angular_distance(b.rotation)))
                   for a, b in zip(crossed, sample))
    held_error = max(arm_error(f) for f in range(fold_frame, hold_end + 1))
    assert held_error < .1, held_error
    def chest_offsets(start, stop):
        baseline = ue.AnimationLibrary.get_bone_poses_for_frame(anim, ["cockpit"], start, False)[0]
        values = []
        for f in range(start, stop + 1):
            t = ue.AnimationLibrary.get_bone_poses_for_frame(anim, ["cockpit"], f, False)[0]
            values.append((t.translation - baseline.translation).length())
        return values

    charge_offsets = chest_offsets(fold_frame, hold_end)
    release_offsets = chest_offsets(last, end)
    quarter = max(1, len(charge_offsets)//4)
    early, late = max(charge_offsets[:quarter]), max(charge_offsets[-quarter:])
    if charge_degrees > 0 and hold_end-fold_frame >= 12:
        assert late > early * 1.4 and late > .5 * charge_degrees, (early, late)
    assert max(release_offsets) >= .5 * release_degrees and release_offsets[-1] < .1
    held_shapes = [metrics(f) for f in range(fold_frame, hold_end+1)]
    assert all(m["muzzle_span"] < 0 and m["muzzle_z_separation"] > 50 for m in held_shapes)
    error = 0.
    for f in (0,cycle//4,cycle//2,3*cycle//4,cycle):
        actual = ue.AnimationLibrary.get_bone_poses_for_frame(anim, leg_names, last+f, False)
        ref = hover_keys(f)
        for n,t in zip(leg_names,actual):
            error=max(error,(ref[n].translation-t.translation).length(), math.degrees(ref[n].rotation.angular_distance(t.rotation)))
    assert error < .1, error
    assert ue.EditorAssetLibrary.save_loaded_asset(anim, False)
    report = dict(asset=anim.get_path_name(), keys=end+1, fps=rate, folded_frame=fold_frame, hold_end_frame=hold_end,
                  crossed_hold_seconds=(hold_end-fold_frame)/rate, deployed_frame=last, spread_seconds=(last-hold_end)/rate,
                  first_shot_frame=last + round(rate * card["CastFlashSeconds"]), max_held_arm_error_cm_or_degrees=held_error,
                  charge_chest_peak_cm_first_quarter=early, charge_chest_peak_cm_last_quarter=late,
                  sustained_chest_peak_cm=max(release_offsets), loop_seam_cm=release_offsets[-1],
                  hover=hover.get_path_name(), hold_loop_seconds=cycle/rate, max_leg_error_cm_or_degrees=error,
                  neutral=base, folded=cross, deployed=open_pose)
    (root/"Artifacts/GroundRogueVfx_20261119/cross-hold-pose.json").write_text(json.dumps(report, indent=2))
    return report


try:
    result = author()
except Exception:
    result = traceback.format_exc()
ue.MCPythonHelper.submit_result(json.dumps(result))
