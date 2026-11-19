"""Author the editable Ranger shot in the barrage range. Run with ue_exec.py."""
import json
import math
import traceback
from pathlib import Path

import unreal as ue


ROOT = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
FOLDER = "/Game/GuLiStrike/GroundMech/Cinematics/NoSurvivors"
NAME = "LS_Ranger_NoSurvivors_Camera_V03"
MAP = "/Game/Maps/LVL_CommanderMassPrototype"
FPS = 60
V = ue.Vector
FRAME = ue.FrameNumber


def row(table, name):
    rows = json.loads((ROOT / "Data/Json" / (table + ".json")).read_text(encoding="utf-8-sig"))
    return next(r for r in rows if r["Name"] == name)


def asset(name, cls, factory):
    path = FOLDER + "/" + name
    if ue.EditorAssetLibrary.does_asset_exist(path):
        return ue.load_asset(path)
    return ue.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, cls, factory)


def bake_preview(mesh, card, shots, end):
    # Sequencer evaluates a skeletal animation without a gameplay pawn. Bake the
    # same two display-only shoulder offsets over the actual authored cast pose.
    source = ue.load_asset(card["CastPose"])
    factory = ue.AnimSequenceFactory()
    factory.target_skeleton = mesh.skeleton
    factory.preview_skeletal_mesh = mesh
    clip = asset("Anim_Ranger_NoSurvivors_Camera_V03", ue.AnimSequence, factory)
    names = [str(n) for n in source.data_model_interface.get_bone_track_names()]
    bone_names = {n.lower(): n for n in names}
    keys = {n: [] for n in names}
    opts = ue.AnimPoseEvaluationOptions()
    opts.optional_skeletal_mesh = mesh
    opts.evaluation_type = ue.AnimDataEvalType.RAW
    for f in range(end + 1):
        pose = ue.AnimPoseExtensions.get_anim_pose_at_frame(source, f, opts)
        for n in names:
            keys[n].append(pose.get_bone_pose(n, ue.AnimPoseSpaces.LOCAL))
        chest = pose.get_bone_pose("cockpit", ue.AnimPoseSpaces.WORLD)
        for i, shot in enumerate(shots):
            p = (f - shot) / (FPS * card["CastShotRecoilSeconds"])
            if 0. < p < 1.:
                peak = 2. / 9.
                a = p / peak if p < peak else (1. - p) / (1. - peak)
                strength = a*a*(3.-2.*a)
                bone = bone_names["gunarm_l" if i % 2 == 0 else "gunarm_r"]
                arm = pose.get_bone_pose(bone, ue.AnimPoseSpaces.WORLD)
                arm.translation += V(0., -.96, .28) * (card["CastShotRecoilCm"] * strength)
                keys[bone][-1] = arm.make_relative(chest)
    ctl = clip.controller
    ctl.open_bracket("Sample production cast and per-shot shoulder recoil", False)
    try:
        ctl.set_frame_rate(ue.FrameRate(FPS,1), False)
        ctl.set_number_of_frames(FRAME(end), False)
        existing = {str(n) for n in clip.data_model_interface.get_bone_track_names()}
        for n, vals in keys.items():
            if n not in existing:
                ctl.add_bone_track(n, False)
            assert ctl.set_bone_track_keys(n, [t.translation for t in vals], [t.rotation for t in vals],
                                          [t.scale3d for t in vals], False), n
        assert ue.GuLiGroundMechAnimationEditorLibrary.notify_animation_keys_populated(clip)
    finally:
        ctl.close_bracket(False)
    assert ue.EditorAssetLibrary.save_loaded_asset(clip, False)
    return clip


def transform_track(binding, samples, end):
    track = binding.add_track(ue.MovieScene3DTransformTrack)
    section = track.add_section()
    section.set_range(0, end)
    channels = list(section.get_channels_by_type(ue.MovieSceneScriptingDoubleChannel))
    assert len(channels) == 9
    last = None
    for frame, pos, rot, scale in samples:
        angles = [rot.roll, rot.pitch, rot.yaw]
        if last:
            angles = [old + (a - old + 180.) % 360. - 180. for a, old in zip(angles, last)]
        last = angles
        values = [*pos.to_tuple(), *angles, *scale.to_tuple()]
        for channel, v in zip(channels, values):
            if len(samples) == 1:
                channel.set_default(v)
            else:
                channel.add_key(FRAME(frame), v)
    return track


def author():
    editor = ue.get_editor_subsystem(ue.EditorActorSubsystem)
    world = ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
    assert world.get_outermost().get_name() == MAP
    assert not ue.EditorLevelLibrary.get_pie_worlds(False)
    pawn = ue.get_default_object(ue.EditorAssetLibrary.load_blueprint_class("/Game/GuLiStrike/GroundMech/BP_GroundMech_Light"))
    mesh = pawn.mesh.skeletal_mesh_asset
    assert pawn.model_id == 4001 and mesh.get_path_name() == row("DT_GuLiStrikeModels_Models", "GroundMech")["ResourcePath"]
    start = next(a for a in editor.get_all_level_actors() if a.actor_has_tag("GuLi.NoSurvivors.Start"))
    feet = start.get_actor_location() + pawn.mesh.relative_location + V(0,0,3600.)
    card = row("DT_GuLiStrikeRogueCards_Cards", "GroundMech_NoSurvivors_Lv1")
    ranks = row("DT_GuLiStrikeRogueCards_GroundWeaponEffects","GroundNoSurvivors")
    flight = row("DT_GuLiStrikeVfx_Effects","GroundNoSurvivorsFlight")
    fold = round(FPS * card["CastFoldSeconds"])
    held = fold + round(FPS * card["CastCrossedHoldSeconds"])
    open_frame = held + round(FPS * card["CastOpenSeconds"])
    first = open_frame + round(FPS * card["CastFlashSeconds"])
    cards = ue.load_asset("/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards")
    track_data = json.loads(ue.GuLiGroundMechAnimationEditorLibrary.sample_barrage_camera(cards))
    keys = track_data["frames"]
    handoff = len(keys) - 1
    end = handoff + FPS
    spacing = round(FPS * float(ranks["BarrageIntervalsSeconds"][0]))
    shots = [first+i*spacing for i in range(int(ranks["BarrageShotCounts"][0])) if first+i*spacing < end]
    clip = bake_preview(mesh, card, shots, end)
    seq = asset(NAME, ue.LevelSequence, ue.LevelSequenceFactoryNew())
    for t in list(seq.get_tracks()):
        seq.remove_track(t)
    for b in reversed(list(seq.get_bindings())):
        b.remove()
    seq.set_display_rate(ue.FrameRate(FPS,1))
    seq.set_playback_start(0)
    seq.set_playback_end(end)
    seq.set_view_range_start(0.)
    seq.set_view_range_end(end/FPS)
    seeds = []
    def seed(cls):
        gameplay = ue.get_default_object(ue.GameplayStatics)
        tf = ue.Transform(location=feet)
        mode = ue.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT
        a = gameplay.call_method("BeginDeferredActorSpawnFromClass",
            (world, cls.static_class(), tf, ue.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, mode))
        gameplay.call_method("FinishSpawningActor", (a, tf, mode))
        seeds.append(a)
        a.set_actor_enable_collision(False)
        return a
    def bind(a, label, start_frame=0, end_frame=end):
        b=seq.add_spawnable_from_instance(a)
        assert b.is_valid() and b.get_object_template(), label
        b.set_name(label)
        track=next(t for t in b.get_tracks() if isinstance(t,ue.MovieSceneSpawnTrack))
        sec=track.get_sections()[0]
        sec.set_range(start_frame,end_frame)
        sec.get_all_channels()[0].set_default(True)
        return b
    def child(b, component):
        # Create the component-relative locator from a real instance, then attach
        # it to its spawnable; the editable sequence has no preview-map actor refs.
        c=seq.add_possessable(component)
        parent=c.get_parent()
        assert c.is_valid() and parent.is_valid()
        c.set_parent(b)
        if parent.get_id()!=b.get_id():
            parent.remove()
        return c
    one=V(1,1,1)
    zero=ue.Rotator()
    cameras=[]
    try:
        body=seed(ue.SkeletalMeshActor)
        body.skeletal_mesh_component.set_skeletal_mesh_asset(mesh)
        body.skeletal_mesh_component.set_mobility(ue.ComponentMobility.MOVABLE)
        body.skeletal_mesh_component.set_forced_lod(1)
        for i,m in enumerate(pawn.mesh.get_materials()):
            if m: body.skeletal_mesh_component.set_material(i,m)
        facing=ue.Rotator(pitch=-8.).quaternion()
        mesh_rot=(facing * pawn.mesh.relative_rotation.quaternion()).rotator()
        # The gameplay display tilts around the capsule centre, not the feet.
        pivot=V(0,0,-pawn.mesh.relative_location.z)
        body_pos=feet+pivot-facing.rotate_vector(pivot)
        body.set_actor_location_and_rotation(body_pos,mesh_rot,False,True)
        body.set_actor_scale3d(pawn.mesh.relative_scale3d)
        body_transform=body.get_actor_transform()
        ranger=bind(body,"Ranger | A4B2")
        transform_track(ranger,[(0,body_pos,mesh_rot,pawn.mesh.relative_scale3d)],end)
        a=ranger.add_track(ue.MovieSceneSkeletalAnimationTrack).add_section()
        params=a.params
        params.animation=clip
        a.params=params
        a.set_range(0,end)

        free = row("DT_GuLiStrikeMech_Camera", "Free")
        base_rotation = ue.Rotator(pitch=free["PitchDegrees"], yaw=free["YawDegrees"]).quaternion()
        socket = free["SocketOffset"]
        base_eye = feet + pivot + base_rotation.rotate_vector(V(socket["X"]-free["ArmLength"],socket["Y"],socket["Z"]))
        actor = seed(ue.CineCameraActor)
        cam = actor.get_cine_camera_component()
        fb = cam.filmback
        fb.sensor_width, fb.sensor_height = 36., 20.25
        cam.filmback = fb
        focus = cam.focus_settings
        focus.focus_method = ue.CameraFocusMethod.DISABLE
        cam.focus_settings = focus
        cam.set_constraint_aspect_ratio(True)
        cam.current_focal_length = .5 * fb.sensor_width / math.tan(math.radians(free["FieldOfView"]*.5))
        binding = bind(actor, "Ranger | continuous orbit")
        lens = child(binding, cam).add_track(ue.MovieSceneFloatTrack)
        lens.set_property_name_and_path("CurrentFocalLength", "CurrentFocalLength")
        lens_section = lens.add_section()
        lens_section.set_range(0, end)
        lens_channel = lens_section.get_all_channels()[0]
        samples = []
        for f, k in enumerate(keys):
            weight = k[7]
            orbit_eye = feet + pivot + facing.rotate_vector(V(*k[:3])-pivot)
            orbit_rotation = facing * ue.Rotator(roll=k[3], pitch=k[4], yaw=k[5]).quaternion()
            look = base_rotation.slerp_quat(orbit_rotation, weight).rotator()
            samples.append((f, base_eye + (orbit_eye-base_eye)*weight, look, one))
            fov = free["FieldOfView"] + (k[6]-free["FieldOfView"])*weight
            lens_channel.add_key(FRAME(f), .5*fb.sensor_width/math.tan(math.radians(fov*.5)),
                                 interpolation=ue.MovieSceneKeyInterpolation.LINEAR)
        transform_track(binding, samples, end)
        cuts=seq.add_track(ue.MovieSceneCameraCutTrack)
        cut=cuts.add_section()
        cut.set_range(0,end)
        cut.set_camera_binding_id(seq.get_binding_id(binding))
        cameras.append(dict(binding=binding.get_name(),frames=[0,end],handoff=handoff))

        def fx(label,path,f0,f1,samples,user_scale=None):
            actor=seed(ue.NiagaraActor)
            comp=actor.niagara_component
            comp.set_asset(ue.load_asset(path),True)
            comp.set_auto_activate(False)
            comp.set_can_render_while_seeking(True)
            comp.set_seek_delta(1./FPS)
            if user_scale is not None: comp.set_variable_float("User.Scale",user_scale)
            b=bind(actor,label,f0,f1)
            transform_track(b,samples,end)
            c=child(b,comp)
            life=c.add_track(ue.MovieSceneNiagaraSystemTrack).add_section()
            life.set_range(f0,f1)
            life.set_age_update_mode(ue.NiagaraAgeUpdateMode.DESIRED_AGE)
            return b
        chest=feet+pivot+V(0,0,pawn.capsule_component.get_scaled_capsule_half_height()*.28)
        power=card["CastBodyScale"]
        lightning_power=power*card["CastLightningScaleMultiplier"]
        charge=[(f,chest,zero,one*(round(k[9]*32.)/32.)) for f,k in enumerate(keys[:open_frame]) if k[9]>=1./64.]
        if charge:
            fx("Charge | gathering arcs",card["CastLightningVfx"],charge[0][0],open_frame,charge,lightning_power)
        fx("Burst | chest flash",card["CastFlashVfx"],open_frame,first,[(0,chest,zero,one)],power)
        fx("Release | body lightning",card["CastLightningVfx"],first,end,[(0,chest,zero,one)],lightning_power)
        fade=seq.add_track(ue.MovieSceneFadeTrack).add_section()
        fade.set_range(0,end)
        fade.set_editor_property("fade_color",ue.LinearColor(1,1,1,1))
        fade_keys=[(f,k[8]) for f,k in enumerate(keys) if f in (0,handoff) or
                   any(s[8]>0 for s in keys[max(0,f-1):f+2])]
        for f,v in fade_keys:
            fade.get_all_channels()[0].add_key(FRAME(f),v,interpolation=ue.MovieSceneKeyInterpolation.LINEAR)

        opts=ue.AnimPoseEvaluationOptions();opts.optional_skeletal_mesh=mesh
        rocket_scale=V(*(flight["Scale"][k] for k in ("X","Y","Z")))
        for i,shot in enumerate(shots):
            side=-1 if i%2==0 else 1
            p=ue.AnimPoseExtensions.get_anim_pose_at_frame(clip,shot,opts)
            port=p.get_socket_pose("Jet_Outlet_L" if side<0 else "Jet_Outlet_R",ue.AnimPoseSpaces.WORLD).translation
            port_world=body_transform.transform_location(port+V(-side*62,0,151))
            def flight_pos(age):
                return port_world+V(4250*age,side*(1320*age*math.exp(-1.9*age)+230*math.sin(3.6*age)),
                                    960*(1-math.exp(-3.5*age))-910*age)
            frames=sorted(set([shot,*range(shot+3,end,3),end]))
            samples=[]
            for frame in frames:
                age=(frame-shot)/FPS
                pos=flight_pos(age)
                direction=ue.MathLibrary.find_look_at_rotation(pos,flight_pos(age+.0005))
                samples.append((frame,pos,direction,rocket_scale))
            fx("Salvo %02d | %s"%(i+1,"left" if side<0 else "right"),flight["ResourcePath"],shot,end,samples)
        assert len(seq.get_tracks())==2 and len(cuts.get_sections())==1
        assert ue.EditorAssetLibrary.save_loaded_asset(seq,False)
        return dict(sequence=seq.get_path_name(),clip=clip.get_path_name(),map=MAP,fps=FPS,
                    frames=end,opening=open_frame,first_shot=first,missiles=shots,cameras=cameras,
                    spawnables=sum(bool(b.get_object_template()) for b in seq.get_bindings()),
                    bindings=len(seq.get_bindings()),stage=feet.to_tuple(), flash_keys=fade_keys)
    finally:
        for a in reversed(seeds):
            a.destroy_actor()


try:
    result=author()
except Exception:
    result=dict(error=traceback.format_exc())
ue.MCPythonHelper.submit_result(json.dumps(result))
