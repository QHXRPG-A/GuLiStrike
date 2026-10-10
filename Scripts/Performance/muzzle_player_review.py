"""Optional saved muzzle view helpers; normal production PIE needs no script."""
import unreal

guli_muzzle_review_state = {'original_views': {}}


def _guli_muzzle_clients():
    clients = []
    for w in unreal.EditorLevelLibrary.get_pie_worlds(True):
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        if pc and pc.is_local_controller():
            clients.append((w, pc))
    assert clients, 'Start PIE first'
    return clients


def guli_muzzle_review_stop():
    """Stop new review cues. Existing particles keep their normal lifetime."""
    for w, pc in _guli_muzzle_clients():
        unreal.SystemLibrary.execute_console_command(w, 'gs.Perf.Review stop MuzzleReview_')


def guli_muzzle_review_view(view='near'):
    """near/middle/far/offscreen/game; per-actor mode 0/2, no registry override."""
    assert view in ('near', 'middle', 'far', 'offscreen', 'game')
    for w, pc in _guli_muzzle_clients():
        key = w.get_path_name()
        state = guli_muzzle_review_state['original_views']
        if key not in state:
            state[key] = (pc.get_view_target(), pc.get_editor_property('bAutoManageActiveCameraTarget'))
        if view == 'game':
            target, automatic = state[key]
            unreal.SystemLibrary.execute_console_command(w, 'gs.Perf.Review stop MuzzleReview_')
            pc.set_editor_property('bAutoManageActiveCameraTarget', automatic)
            pc.set_view_target_with_blend(unreal.GameplayStatics.get_player_pawn(w, 0) or target, 0)
            continue
        tier = {'near': 'Near', 'middle': 'Middle', 'far': 'Far', 'offscreen': 'Far'}[view]
        cam = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CameraActor)
                   if a.get_actor_label() == 'MuzzleReview_Camera_' + tier)
        target = (unreal.Vector(50000, -33000, 7000) if view == 'offscreen'
                  else unreal.Vector(-19750, -16250, 2000))
        cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(), target), True)
        pc.set_editor_property('bAutoManageActiveCameraTarget', False)
        pc.set_view_target_with_blend(cam, 0)
        if view == 'near':
            unreal.SystemLibrary.execute_console_command(w, 'gs.Perf.Review start MuzzleReview_')


def guli_muzzle_review_start():
    """Explicit optional one-server/two-client launch in the existing review map."""
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert w.get_name() == 'LVL_CommanderMassPrototype'
    nav = unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(w, True)
    resources = unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    assert nav.success and resources.success, (nav.message, resources.message)
    assert unreal.GuLiComponentSkillQALibrary.start_performance_pie(False, 1280, 720)


unreal.log('Muzzle review helpers ready. Saved production defaults and resources remain unchanged.')
