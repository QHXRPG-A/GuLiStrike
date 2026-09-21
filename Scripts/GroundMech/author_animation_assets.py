"""Project-owned mech animation graphs and production weapon wiring.

Run in the source editor through Scripts/ue_exec.py after the native build.
Existing authored graphs are preserved unless REBUILD_ANIMATION_GRAPH=True is
explicitly supplied. Marketplace assets are only read and duplicated.
"""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
OUT = ROOT / 'TestResults/GroundMech/Animation'
OUT.mkdir(parents=True, exist_ok=True)
BASE = '/Game/GuLiStrike/GroundMech'
ANIM = BASE + '/Animations'
ABP = ANIM + '/ABP_GroundMech'
SOURCE = '/Game/Assets/MechaController/Mech_Constructor_Lt_Med/Animations'
BS_SOURCE = '/Game/Assets/MechaController/Blueprints/SalvaMeshIntegration/Examples/Mech_Legs_Lt_IdleToRunWithTurnRate'
BS_PATH = ANIM + '/BS_GroundMech_Locomotion'
MACHINE = 'GroundMechLocomotion'
VERSION = '20260921.1'
LIB, B, A = unreal.EditorAssetLibrary, unreal.BlueprintService, unreal.AnimGraphService
REPORT = {'success': False, 'graphs': {}, 'copied': []}


def load(path):
    return unreal.load_object(None, path if '.' in path.rsplit('/', 1)[-1] else path + '.' + path.rsplit('/', 1)[-1])


def save(asset):
    assert asset.get_path_name().startswith('/Game/GuLiStrike/')
    assert LIB.save_loaded_asset(asset, False)


def duplicate(src, dst):
    result = load(dst) if LIB.does_asset_exist(dst) else LIB.duplicate_asset(src, dst)
    assert result, (src, dst)
    return result


def copy_sources():
    mapping = {}
    assets = unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(SOURCE, True)
    assert len(assets) == 22, len(assets)
    for data in assets:
        src, name = str(data.package_name), str(data.asset_name)
        folder = 'Weapon' if name.startswith('Weapons_') else 'Legs'
        dst = ANIM + '/Source/' + folder + '/' + name
        mapping[src] = duplicate(src, dst)
        REPORT['copied'].append(dst)
        save(mapping[src])
    bs = duplicate(BS_SOURCE, BS_PATH)
    samples = list(bs.get_editor_property('sample_data'))
    for sample in samples:
        old = sample.get_editor_property('animation').get_path_name().split('.')[0]
        # Reruns can already point at the project copies.
        if old in mapping:
            sample.set_editor_property('animation', mapping[old])
    bs.set_editor_property('sample_data', samples)
    save(bs)
    REPORT['blend_samples'] = [s.get_editor_property('animation').get_path_name() for s in samples]
    assert len(samples) == 9 and all(p.startswith(ANIM + '/Source/') for p in REPORT['blend_samples'])
    return bs


class Graph:
    """Small declarative wrapper around VibeUE's schema-checked graph builder."""
    def __init__(self, name):
        self.name, self.nodes, self.links, self.defaults, self.getters = name, [], [], [], {}

    def add(self, kind, params=None):
        ref = 'n' + str(len(self.nodes))
        self.nodes.append({'ref': ref, 'type': kind, 'params': params or {}})
        return ref

    def value(self, node, pin, value):
        if isinstance(value, tuple):
            self.connect(value, (node, pin))
        else:
            self.defaults.append({'node_ref': node, 'pin_name': pin, 'value': str(value)})

    def connect(self, source, dest):
        self.links.append({'from_': '.'.join(source), 'to': '.'.join(dest)})

    def get(self, variable):
        if variable not in self.getters:
            self.getters[variable] = self.add('variable_get', {'variable': variable})
        return self.getters[variable], variable

    def math(self, name, **inputs):
        node = self.add('function_call', {'class': 'KismetMathLibrary', 'function': name})
        for pin, value in inputs.items():
            self.value(node, pin, value)
        return node, 'ReturnValue'

    def set(self, variable, value, execution=None):
        node = self.add('variable_set', {'variable': variable})
        self.value(node, variable, value)
        if execution:
            self.connect(execution, (node, 'execute'))
        return node, 'then'

    def branch(self, condition, execution):
        node = self.add('branch')
        self.value(node, 'Condition', condition)
        self.connect(execution, (node, 'execute'))
        return (node, 'then'), (node, 'else')

    def entry(self):
        return next((n.node_id, 'then') for n in B.get_nodes_in_graph(ABP, self.name) if 'FunctionEntry' in n.node_type)

    def finish(self):
        result = B.build_graph(ABP, self.name, self.nodes, self.links, self.defaults, True, False)
        assert result is not None, self.name
        REPORT['graphs'][self.name] = {k: getattr(result, k) for k in ['nodes_created', 'connections_made', 'connections_failed', 'defaults_failed']}
        REPORT['graphs'][self.name]['errors'] = list(result.errors)
        assert result.success, (self.name, list(result.errors))
        return dict(result.ref_to_node_id)


def build_update_graphs():
    variables = {
        'FlightLeanAlpha': ('float', '0'), 'LeanStartAlpha': ('float', '0'),
        'LeanTargetAlpha': ('float', '0'), 'LeanElapsed': ('float', '1'),
        'FlightLeanSeconds': ('float', '1'), 'FlightLeanDegrees': ('float', '15'),
        'AnimationDeltaSeconds': ('float', '0'), 'bHorizontalFlight': ('bool', 'false'),
        'LastFlightDirection': ('FVector', '(X=0,Y=1,Z=0)'),
        'FlightLeanRotation': ('FRotator', '(Pitch=0,Yaw=0,Roll=0)'),
        'SeenAnimationRevision': ('int', '-1'), 'bResetPoseThisFrame': ('bool', 'false'),
        'bRequestTakeoff': ('bool', 'false'), 'bDirectAirLoop': ('bool', 'false'),
        'bRequestLanding': ('bool', 'false'), 'bResetToGround': ('bool', 'false'),
        'LocomotionSpeedAxis': ('float', '0'), 'LocomotionTurnAxis': ('float', '0'),
    }
    for name, (kind, default) in variables.items():
        if not B.variable_exists(ABP, name):
            assert B.add_variable(ABP, name, kind, default), name
    functions = ['UpdateFlightLean', 'UpdateAnimationTransitions', 'UpdateLocomotionBlend']
    for name in functions:
        if not B.function_exists(ABP, name):
            assert B.create_function(ABP, name, False)
        for node in B.get_nodes_in_graph(ABP, name):
            if 'FunctionEntry' not in node.node_type:
                assert B.delete_node(ABP, name, node.node_id)
    assert B.compile_blueprint(ABP)

    g = Graph('UpdateFlightLean')
    revision_changed = g.math('NotEqual_IntInt', A=g.get('AnimationStateRevision'), B=g.get('SeenAnimationRevision'))
    current = g.set('bResetPoseThisFrame', revision_changed, g.entry())
    reset, keep = g.branch(g.get('bResetPoseThisFrame'), current)
    for var, val in [('SeenAnimationRevision', g.get('AnimationStateRevision')), ('FlightLeanAlpha', 0), ('LeanStartAlpha', 0), ('LeanTargetAlpha', 0), ('LeanElapsed', 1), ('bHorizontalFlight', 'false')]:
        reset = g.set(var, val, reset)
    over_enter = g.math('Greater_DoubleDouble', A=g.get('Speed'), B=10)
    over_exit = g.math('GreaterEqual_DoubleDouble', A=g.get('Speed'), B=5)
    hysteresis = g.math('BooleanOR', A=over_enter, B=g.math('BooleanAND', A=g.get('bHorizontalFlight'), B=over_exit))
    horizontal = g.set('bHorizontalFlight', g.math('BooleanAND', A=g.get('bIsAirborne'), B=hysteresis), reset)
    g.connect(keep, (horizontal[0], 'execute'))
    desired = g.math('SelectFloat', A=1, B=0, bPickA=g.get('bHorizontalFlight'))
    changed, same = g.branch(g.math('NotEqual_DoubleDouble', A=desired, B=g.get('LeanTargetAlpha')), horizontal)
    changed = g.set('LeanStartAlpha', g.get('FlightLeanAlpha'), changed)
    changed = g.set('LeanTargetAlpha', desired, changed)
    changed = g.set('LeanElapsed', 0, changed)
    duration = g.math('FMax', A=g.get('FlightLeanSeconds'), B=0.001)
    advance = g.set('LeanElapsed', g.math('FMin', A=g.math('Add_DoubleDouble', A=g.get('LeanElapsed'), B=g.get('AnimationDeltaSeconds')), B=duration), changed)
    g.connect(same, (advance[0], 'execute'))
    fraction = g.math('FClamp', Value=g.math('Divide_DoubleDouble', A=g.get('LeanElapsed'), B=duration), Min=0, Max=1)
    advance = g.set('FlightLeanAlpha', g.math('FInterpEaseInOut', A=g.get('LeanStartAlpha'), B=g.get('LeanTargetAlpha'), Alpha=fraction, Exponent=2), advance)
    moving, still = g.branch(over_exit, advance)
    direction = g.set('LastFlightDirection', g.math('Normal', A=g.get('LocalHorizontalVelocity')), moving)
    angle = g.math('Multiply_DoubleDouble', A=g.math('FClamp', Value=g.get('FlightLeanDegrees'), Min=0, Max=15), B=g.get('FlightLeanAlpha'))
    rotation = g.math('RotatorFromAxisAndAngle', Axis=g.math('Cross_VectorVector', A=g.get('LastFlightDirection'), B='(X=0,Y=0,Z=-1)'), Angle=angle)
    finish = g.set('FlightLeanRotation', rotation, direction)
    g.connect(still, (finish[0], 'execute'))
    g.finish()
    B.add_comment_node(ABP, g.name, '1 秒后倾 / 保持 / 1 秒回正；10/5 cm/s 阈值；中断从当前权重继续', -300, -350, 1350, 220)

    g = Graph('UpdateAnimationTransitions')
    no_reset = g.math('Not_PreBool', A=g.get('bResetPoseThisFrame'))
    takeoff = g.math('BooleanAND', A=g.get('bIsAirborne'), B=g.math('BooleanAND', A=g.get('bIsThrusting'), B=no_reset))
    current = g.set('bRequestTakeoff', takeoff, g.entry())
    current = g.set('bDirectAirLoop', g.math('BooleanAND', A=g.get('bIsAirborne'), B=g.math('Not_PreBool', A=g.get('bRequestTakeoff'))), current)
    current = g.set('bRequestLanding', g.math('BooleanAND', A=g.get('bIsGrounded'), B=no_reset), current)
    g.set('bResetToGround', g.math('BooleanAND', A=g.get('bIsGrounded'), B=g.get('bResetPoseThisFrame')), current)
    g.finish()

    bs = load(BS_PATH)
    axes = bs.get_editor_property('blend_parameters')
    g = Graph('UpdateLocomotionBlend')
    current = g.entry()
    for var, value, lower, upper, axis in [('LocomotionSpeedAxis', g.get('Speed'), 0, g.get('RunSpeed'), axes[0]), ('LocomotionTurnAxis', g.get('TurnRate'), -180, 180, axes[1])]:
        mapped = g.math('MapRangeClamped', Value=value, InRangeA=lower, InRangeB=upper, OutRangeA=axis.get_editor_property('min'), OutRangeB=axis.get_editor_property('max'))
        current = g.set(var, mapped, current)
    g.finish()

    for node in B.get_nodes_in_graph(ABP, 'EventGraph'):
        assert B.delete_node(ABP, 'EventGraph', node.node_id)
    g = Graph('EventGraph')
    event = g.add('event', {'event': 'BlueprintUpdateAnimation'})
    current = g.set('AnimationDeltaSeconds', (event, 'DeltaTimeX'), (event, 'then'))
    for name in functions:
        call = g.add('function_call', {'class': ABP + '.ABP_GroundMech_C', 'function': name})
        g.connect(current, (call, 'execute'))
        current = (call, 'then')
    g.finish()
    B.add_comment_node(ABP, 'EventGraph', 'C++ 只读运动快照 → 蓝图飞行姿态 / 状态转换 / 地面混合', -250, -250, 1400, 180)


def add_bone_node(graph, bone, x):
    before = set(unreal.ObjectIterator(unreal.AnimGraphNode_ModifyBone))
    node_id = A.add_modify_bone_node(ABP, graph, bone, x, 0)
    obj = next(n for n in unreal.ObjectIterator(unreal.AnimGraphNode_ModifyBone) if n not in before and n.get_path_name().startswith(ABP + '.'))
    return node_id, obj


def add_space_node(graph, name):
    # These standard AnimGraph conversion classes are not indexed by the generic
    # K2 discovery search; use their verified native spawner keys.
    return B.create_node_by_key(ABP, graph, 'NODE ' + name)


def build_pose_graph():
    bp = load(ABP)
    while True:
        graph = unreal.BlueprintEditorLibrary.find_graph(bp, MACHINE)
        if not graph:
            break
        unreal.BlueprintEditorLibrary.remove_graph(bp, graph)
    for node in B.get_nodes_in_graph(ABP, 'AnimGraph'):
        if 'Root' not in node.node_type:
            assert B.delete_node(ABP, 'AnimGraph', node.node_id)
    machine = A.add_state_machine(ABP, MACHINE, -450, 0)
    assert machine and A.connect_to_output_pose(ABP, 'AnimGraph', machine)
    states = ['Grounded', 'Takeoff', 'AirLoop', 'Landing']
    for i, name in enumerate(states):
        assert A.add_state(ABP, MACHINE, name, (i % 2) * 450, (i // 2) * 300)
    assert A.set_entry_state(ABP, MACHINE, 'Grounded')
    ground = A.add_blend_space_player(ABP, 'Grounded', BS_PATH, -600, 0)
    assert A.connect_to_output_pose(ABP, 'Grounded', ground)
    for var, pin, y in [('LocomotionSpeedAxis', 'X', -250), ('LocomotionTurnAxis', 'Y', 100)]:
        getter = B.add_get_variable_node(ABP, 'Grounded', var, -1000, y)
        assert B.connect_nodes(ABP, 'Grounded', getter, var, ground, pin)
    for state, suffix, loop in [('Takeoff', 'Start', False), ('AirLoop', 'Idle', True), ('Landing', 'Land', False)]:
        path = ANIM + '/Source/Legs/Mech_Legs_Lt_Jump_Jetpack_' + suffix
        player = A.set_state_animation(ABP, MACHINE, state, path, loop, 1.0)
        assert player
        if state == 'Landing':
            continue
        local_to_component = add_space_node(state, 'AnimGraphNode_LocalToComponentSpace')
        component_to_local = add_space_node(state, 'AnimGraphNode_ComponentToLocalSpace')
        left, left_obj = add_bone_node(state, 'Hip_L', 0)
        right, right_obj = add_bone_node(state, 'Hip_R', 300)
        for node_id, obj in [(left, left_obj), (right, right_obj)]:
            data = obj.get_editor_property('node')
            data.set_editor_property('rotation_mode', unreal.BoneModificationMode.BMM_ADDITIVE)
            data.set_editor_property('rotation_space', unreal.BoneControlSpace.BCS_COMPONENT_SPACE)
            obj.set_editor_property('node', data)
            getter = B.add_get_variable_node(ABP, state, 'FlightLeanRotation', -500, 500 if node_id == left else 750)
            assert B.connect_nodes(ABP, state, getter, 'FlightLeanRotation', node_id, 'Rotation')
        assert A.disconnect_anim_node(ABP, state, player, 'Pose')
        for src, out_pin, dst, in_pin in [(player, 'Pose', local_to_component, 'LocalPose'), (local_to_component, 'ComponentPose', left, 'ComponentPose'), (left, 'Pose', right, 'ComponentPose'), (right, 'Pose', component_to_local, 'ComponentPose')]:
            assert A.connect_anim_nodes(ABP, state, src, out_pin, dst, in_pin)
        assert A.connect_to_output_pose(ABP, state, component_to_local)
        B.auto_layout_graph(ABP, state)
        B.add_comment_node(ABP, state, '双髋骨组件空间附加旋转：只倾斜双腿，上身挂点不参与', -400, -300, 1500, 180)

    def transition(src, dst, var=None, priority=2, blend=0.1):
        assert A.add_transition(ABP, MACHINE, src, dst, blend)
        assert A.set_transition_priority(ABP, MACHINE, src, dst, priority)
        if var:
            assert A.set_transition_rule_from_bool(ABP, MACHINE, src, dst, var)
        else:
            assert A.set_transition_rule_automatic(ABP, MACHINE, src, dst, -1.0)

    for state in ['Grounded', 'Landing']:
        transition(state, 'Takeoff', 'bRequestTakeoff', 1)
        transition(state, 'AirLoop', 'bDirectAirLoop', 2)
    transition('Takeoff', 'AirLoop', None, 3)
    for state in ['Takeoff', 'AirLoop']:
        transition(state, 'Landing', 'bRequestLanding', 1)
        transition(state, 'Grounded', 'bResetToGround', 0, 0.0)
    transition('Landing', 'Grounded', None, 3)
    B.add_comment_node(ABP, 'AnimGraph', '地面 / 起飞 / 空中循环 / 落地；动画不锁定移动、喷气或开火', -650, -240, 1450, 150)


def wire_weapon():
    gun_abp = load(ANIM + '/ABP_GroundMech_Machinegun')
    assert gun_abp
    for path in [BASE + '/BP_GroundMech_Light', BASE + '/Review/BP_GroundMech_FireReview']:
        bp = load(path)
        assert bp
        bp.modify()
        cdo = unreal.get_default_object(bp.generated_class())
        weapon = cdo.get_editor_property('weapon')
        weapon.modify()
        weapon.set_editor_property('weapon_enabled', True)
        weapon.set_editor_property('upgrade_table', load('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Upgrades'))
        weapon.set_editor_property('skill_table', load('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Skills'))
        weapon.set_editor_property('current_upgrade_id', '1.1')
        cdo.get_editor_property('machinegun').set_anim_instance_class(gun_abp.generated_class())
        cdo.get_editor_property('mesh').set_anim_instance_class(load(ABP).generated_class())
        assert B.compile_blueprint(path)
        save(bp)


def run():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    bs = copy_sources()
    bp = load(ABP)
    assert bp
    if LIB.get_metadata_tag(bp, 'GuLi.AnimationVersion') != VERSION or globals().get('REBUILD_ANIMATION_GRAPH', False):
        build_update_graphs()
        build_pose_graph()
        assert B.set_property(ABP, 'RootMotionMode', 'IgnoreRootMotion')
        assert B.set_property(ABP, 'LocomotionBlendSpace', bs.get_path_name())
        assert B.compile_blueprint(ABP)
        validation = A.validate_state_machine(ABP, MACHINE)
        REPORT['state_validation'] = {'valid': validation.is_valid, 'errors': list(validation.errors), 'warnings': list(validation.warnings)}
        assert validation.is_valid, REPORT['state_validation']
        LIB.set_metadata_tag(bp, 'GuLi.AnimationVersion', VERSION)
        save(bp)
    wire_weapon()
    table = load('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Skills')
    assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_file(table, str(ROOT / 'Data/Json/DT_GuLiStrikeMech_Skills.json'))
    save(table)
    REPORT['success'] = True


if __name__ == '__main__':
    try:
        run()
    except Exception:
        REPORT['error'] = traceback.format_exc()
    (OUT / 'asset-authoring.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in REPORT.items() if k not in ['graphs', 'copied', 'blend_samples']}, ensure_ascii=True))
