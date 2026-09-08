"""Author approved Commander rigs on project-owned copies, never game assets.

WM01 binding-scale and local weapon-weight repair explicitly approved 2026-09-06.
Run selected stages in UE Python through Scripts/ue_exec.py. No test-suite files.
"""
import json
import math
import re
from pathlib import Path
import unreal
import prepare_commander_rig_sources as sources

OUT = Path('D:/UE5.7/test1/outputs/commander-rigs')


def save_report(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def check_owned(unit):
    paths = sources.paths(unit)
    mesh = unreal.load_asset(paths['mesh'])
    if unreal.EditorAssetLibrary.get_metadata_tag(mesh, 'CommanderRigOwner') != '20260906':
        raise RuntimeError('Not a task-owned mesh: ' + paths['mesh'])
    if mesh.skeleton.get_path_name().split('.')[0] != paths['skeleton']:
        raise RuntimeError('Refusing to modify a shared skeleton')
    return mesh, paths


def repair_wm01_reference():
    mesh, paths = check_owned('WM01')
    if unreal.EditorAssetLibrary.get_metadata_tag(mesh, 'CommanderRigScaleRepair') == '1000-cm-unit-scale':
        print('WM01 reference already repaired')
        return
    original = unreal.SkeletonService.list_bones(paths['mesh'])
    desired = {}
    for bone in original:
        t = bone.global_transform
        desired[bone.bone_name] = unreal.Transform(location=t.translation * 1000.0,
                                                   scale=unreal.Vector(1, 1, 1))
        desired[bone.bone_name].rotation = t.rotation
    for bone in original:
        global_t = desired[bone.bone_name]
        local_t = global_t.make_relative(desired[bone.parent_bone_name]) if bone.parent_bone_name else global_t
        if not unreal.SkeletonService.set_bone_transform(paths['mesh'], bone.bone_name, local_t, True):
            raise RuntimeError('Could not repair reference transform: ' + bone.bone_name)
    if not unreal.SkeletonService.commit_bone_changes(paths['mesh'], False):
        raise RuntimeError('WM01 reference commit failed')
    after = unreal.SkeletonService.list_bones(paths['mesh'])
    if len(after) != 99:
        raise RuntimeError('WM01 bone count changed unexpectedly')
    unreal.EditorAssetLibrary.set_metadata_tag(mesh, 'CommanderRigScaleRepair', '1000-cm-unit-scale')
    for path in (paths['mesh'], paths['skeleton']):
        if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
            raise RuntimeError('Could not save ' + path)
    save_report('wm01-reference-repair.json', {
        'mesh': paths['mesh'], 'bone_count': len(after),
        'positions': {b.bone_name: sources.serial_transform(b.global_transform) for b in after},
        'policy': 'Original hierarchy/names/vertices/weights retained; global joint translations x1000 and bind scales normalized to 1; inverse bind matrices rebuilt by SkeletonModifier.'})
    print('WM01_REFERENCE_REPAIRED')


def transform_text(t):
    q, p, s = t.rotation, t.translation, t.scale3d
    return (f'(Rotation=(X={q.x:.9f},Y={q.y:.9f},Z={q.z:.9f},W={q.w:.9f}),'
            f'Translation=(X={p.x:.9f},Y={p.y:.9f},Z={p.z:.9f}),'
            f'Scale3D=(X={s.x:.9f},Y={s.y:.9f},Z={s.z:.9f}))')


def vector_text(v):
    return f'(X={v.x:.9f},Y={v.y:.9f},Z={v.z:.9f})'


def key_text(key):
    kind = {unreal.RigElementType.BONE:'Bone',unreal.RigElementType.CONTROL:'Control',
            unreal.RigElementType.NULL:'Null'}.get(key.type,'None')
    return f'(Type={kind},Name="{key.name}")'


def clean(name):
    return re.sub(r'[^A-Za-z0-9_]', '_', str(name))


class RigAuthor:
    """Deterministic graph authoring; modifications restricted to owned assets."""

    def __init__(self, unit):
        self.unit = unit
        self.mesh, self.paths = check_owned(unit)
        path = self.paths['rig']
        self.rig = unreal.load_asset(path)
        if self.rig:
            if unreal.EditorAssetLibrary.get_metadata_tag(self.rig, 'CommanderRigOwner') != '20260906':
                raise RuntimeError('Refusing to rebuild unowned rig ' + path)
        else:
            self.rig = unreal.ControlRigBlueprintFactory.create_new_control_rig_asset(path)
            unreal.EditorAssetLibrary.set_metadata_tag(self.rig, 'CommanderRigOwner', '20260906')
        if not unreal.EditorAssetLibrary.save_loaded_asset(self.rig, only_if_is_dirty=False):
            raise RuntimeError('Rig must be registered and saved before graph authoring')
        self.rig.set_auto_vm_recompile(False)
        self.rig.set_preview_mesh(self.mesh)
        self.hc = self.rig.get_hierarchy_controller()
        self.h = self.hc.get_hierarchy()
        self.graph = self.rig.get_or_create_controller()
        for node in list(self.graph.get_graph().get_nodes()):
            self.graph.remove_node(node, setup_undo_redo=False)
        self.h.reset()
        self.bones = list(unreal.SkeletonService.list_bones(self.paths['mesh']))
        self.info = {b.bone_name: b for b in self.bones}
        self.keys = {}
        self.controls = {}
        self.mapping = {}
        self.legs = []
        self.row = 0
        for b in self.bones:
            parent = self.keys.get(b.parent_bone_name, unreal.RigElementKey())
            key = self.hc.add_bone(clean(b.bone_name), parent, b.local_transform,
                                  transform_in_global=False, bone_type=unreal.RigBoneType.IMPORTED)
            self.keys[b.bone_name] = key
            self.mapping[str(key.name)] = b.bone_name
        if len(self.keys) != (21 if unit == 'FourFRobot' else 99):
            raise RuntimeError('Incomplete deform hierarchy')
        begin = self.node('RigUnit_BeginExecution', 'ForwardsSolve', x=600)
        self.last = begin + '.ExecutePin'

    def node(self, struct, name, x=0):
        node = self.graph.add_unit_node_from_struct_path('/Script/ControlRig.' + struct,
                  'Execute', unreal.Vector2D(x, self.row * 220), name, setup_undo_redo=False)
        if not node:
            raise RuntimeError('Node creation failed: ' + name)
        return node.get_name()

    def value(self, pin, value):
        if not self.graph.set_pin_default_value(pin, str(value), resize_arrays=True, setup_undo_redo=False):
            raise RuntimeError('Pin rejected: ' + pin + ' = ' + str(value))

    def link(self, a, b):
        if not self.graph.add_link(a, b, setup_undo_redo=False):
            raise RuntimeError('Link rejected: ' + a + ' -> ' + b)

    def execute(self, name):
        self.link(self.last, name + '.ExecutePin')
        self.last = name + '.ExecutePin'
        self.row += 1

    def control(self, name, parent, global_t, shape='Circle_Thick', size=100,
                color=(1, .65, .08), rotation_only=False):
        settings = unreal.RigControlSettings()
        settings.control_type = unreal.RigControlType.EULER_TRANSFORM
        settings.display_name = name
        settings.shape_name = shape
        settings.shape_color = unreal.LinearColor(*color, 1)
        settings.shape_visible = True
        settings.limit_enabled = [unreal.RigControlLimitEnabled(minimum=rotation_only, maximum=rotation_only) for _ in range(3)] + [unreal.RigControlLimitEnabled(minimum=True,maximum=True) for _ in range(6)]
        settings.minimum_value = unreal.RigHierarchy.make_control_value_from_euler_transform(
            unreal.EulerTransform(rotation=unreal.Rotator(-45,-60,-45),scale=unreal.Vector(1,1,1)))
        settings.maximum_value = unreal.RigHierarchy.make_control_value_from_euler_transform(
            unreal.EulerTransform(rotation=unreal.Rotator(45,60,45),scale=unreal.Vector(1,1,1)))
        val = unreal.RigHierarchy.make_control_value_from_euler_transform(unreal.EulerTransform(scale=unreal.Vector(1,1,1)))
        parent_key = self.controls.get(parent, unreal.RigElementKey())
        key = self.hc.add_control(name, parent_key, settings, val, setup_undo=False)
        local_t = global_t.make_relative(self.h.get_global_transform(parent_key, initial=True)) if parent else global_t
        for initial in (True, False):
            self.h.set_control_offset_transform(key, local_t, initial=initial)
            self.h.set_control_shape_transform(key, unreal.Transform(scale=unreal.Vector(size, size, size)), initial=initial)
        self.controls[name] = key
        return key

    def float_control(self, name, parent, value=1.):
        settings = unreal.RigControlSettings(control_type=unreal.RigControlType.FLOAT,
                  display_name=name, shape_visible=False,
                  minimum_value=unreal.RigHierarchy.make_control_value_from_float(0.),
                  maximum_value=unreal.RigHierarchy.make_control_value_from_float(1.),
                  limit_enabled=[unreal.RigControlLimitEnabled(minimum=True, maximum=True)])
        key = self.hc.add_control(name, self.controls[parent], settings,
                 unreal.RigHierarchy.make_control_value_from_float(value), setup_undo=False)
        self.controls[name] = key

    def get(self, key, label, initial=False, local=False, x=0):
        node = self.node('RigUnit_GetTransform', label, x)
        self.value(node + '.Item', key_text(key))
        self.value(node + '.Space', 'LocalSpace' if local else 'GlobalSpace')
        self.value(node + '.bInitial', 'True' if initial else 'False')
        return node + '.Transform'

    def set_bone(self, bone, source, local=False):
        node = self.node('RigUnit_SetTransform', 'Set_' + clean(bone), x=600)
        self.value(node + '.Item', key_text(self.keys[bone]))
        self.value(node + '.Space', 'LocalSpace' if local else 'GlobalSpace')
        self.link(source, node + '.Value')
        self.execute(node)

    def drive(self, bone, control):
        self.set_bone(bone, self.get(self.controls[control], 'Read_' + control))

    def set_item(self, key, source, label):
        node = self.node('RigUnit_SetTransform',label,x=600)
        self.value(node+'.Item',key_text(key))
        self.link(source,node+'.Value')
        self.execute(node)

    def multiply_offset(self, offset, source, label, x=0):
        node = self.node('RigUnit_MathTransformMul',label,x=x)
        self.value(node+'.A',transform_text(offset))
        self.link(source,node+'.B')
        return node+'.Result'

    def build_controls(self):
        four = self.unit == 'FourFRobot'
        root = self.bones[0].bone_name
        self.control('CTRL_Global', None, unreal.Transform(), size=800 if four else 2600)
        body = root if four else 'Mainbody'
        self.control('CTRL_Body', 'CTRL_Global', self.info[body].global_transform,
                     shape='Cube_Thin', size=650 if four else 800)
        bone_controls = {body: 'CTRL_Body'}
        if not four:
            bone_controls[root] = 'CTRL_Global'
            self.control('CTRL_Weapon_UpperBody', 'CTRL_Body', self.info['Mainbody'].global_transform,
                         size=1000, color=(1,.2,.04), rotation_only=True)
            bone_controls['Mainbody'] = 'CTRL_Weapon_UpperBody'
            self.control('CTRL_Chassis', 'CTRL_Body', self.info['Pelvis'].global_transform,
                         shape='Cube_Thin', size=700)
            bone_controls['Pelvis'] = 'CTRL_Chassis'
        if four:
            chains = [[b.bone_name for b in self.bones[start:start+3]] for start in (1,4,7,10)]
        else:
            chains = [[f'Leg_{i:02d}{s}' for s in 'edcba'] for i in range(1,7)]
        in_legs = {b: (i,j) for i, chain in enumerate(chains, 1) for j,b in enumerate(chain, 1)}
        # FK controls mirror the original mechanical hierarchy, with the sole
        # deliberate exception of Pelvis: it follows Body, not UpperBody.
        for b in self.bones:
            name = b.bone_name
            if name in bone_controls or name.endswith('_end') or '_lever' in name or name == 'root':
                continue
            if name in in_legs:
                i,j = in_legs[name]
                ctrl = f'CTRL_Leg_{i:02d}_FK_{j:02d}'
                color = (.1,.65,1)
            else:
                ctrl = 'CTRL_Weapon_' + clean(name)
                color = (1,.25,.05)
            parent = bone_controls.get(b.parent_bone_name, 'CTRL_Body')
            self.control(ctrl, parent, b.global_transform, size=140 if four else 160,
                         color=color, rotation_only=True)
            bone_controls[name] = ctrl
        # Recompute all deform transforms each solve. Helpers reset LOCAL to
        # reference (never reset/delete the hierarchy); IK cannot accumulate.
        for b in self.bones:
            if b.bone_name in bone_controls:
                self.drive(b.bone_name, bone_controls[b.bone_name])
            else:
                self.set_bone(b.bone_name, self.get(self.keys[b.bone_name],
                              'Bind_' + clean(b.bone_name), initial=True, local=True), local=True)
        contact_data = None
        if not four:
            positions = json.loads((OUT/'wm01-positions.json').read_text(encoding='utf-8'))
            weights = json.loads((OUT/'WM01-weights.json').read_text(encoding='utf-8'))
            contact_data = {}
            for chain in chains:
                points = [p for p,w in zip(positions,weights) if w.get(chain[-1],0) > .5]
                floor = min(p[2] for p in points)
                low = [p for p in points if p[2] < floor + 15]
                contact_data[chain[-1]] = unreal.Vector(sum(p[0] for p in low)/len(low),
                                     sum(p[1] for p in low)/len(low), floor)
        for i, chain in enumerate(chains,1):
            end = self.info[chain[-1]].global_transform
            contact = unreal.Transform(location=contact_data[chain[-1]] if contact_data else
                                  unreal.Vector(end.translation.x,end.translation.y,0))
            contact.rotation = end.rotation
            ctrl = f'CTRL_Foot_{i:02d}_IK'
            self.control(ctrl, 'CTRL_Global', contact, shape='Cube_Thick',
                         size=150 if four else 180, color=(.12,1,.38))
            weight = f'CTRL_Leg_{i:02d}_IK'
            self.float_control(weight, ctrl)
            self.legs.append({'chain':chain, 'control':ctrl, 'weight':weight,
                              'offset':end.make_relative(contact), 'contact':sources.serial_transform(contact)})
        self.bone_controls = bone_controls

    def build_ik(self):
        four = self.unit == 'FourFRobot'
        for i, leg in enumerate(self.legs,1):
            chain = leg['chain']
            goal = self.get(self.controls[leg['control']], f'Foot_{i:02d}', x=-600)
            mul = self.node('RigUnit_MathTransformMul', f'FootToAnkle_{i:02d}', x=-250)
            self.value(mul+'.A',transform_text(leg['offset']))
            self.link(goal,mul+'.B')
            reach = sum((self.info[b].global_transform.translation-self.info[a].global_transform.translation).length()
                        for a,b in zip(chain,chain[1:]))
            clamp = self.node('RigUnit_MathTransformClampSpatially', f'NoStretch_{i:02d}', x=100)
            self.value(clamp+'.Type','Sphere')
            self.value(clamp+'.Minimum','0.01')
            self.value(clamp+'.Maximum',str(reach))
            self.link(mul+'.Result',clamp+'.Value')
            hip = self.get(self.keys[chain[0]],f'Hip_{i:02d}',x=-250)
            self.link(hip+'.Translation',clamp+'.Space.Translation')
            weight = self.node('RigUnit_GetControlFloat',f'IKWeight_{i:02d}',x=100)
            self.value(weight+'.Control',leg['weight'])
            ik = self.node('RigUnit_TwoBoneIKSimplePerItem' if four else 'RigUnit_CCDIKItemArray', f'LegIK_{i:02d}',x=600)
            if four:
                a,b,c = [self.info[n].global_transform for n in chain]
                axis = (c.translation-a.translation).normal()
                bend = b.translation-a.translation-axis*(b.translation-a.translation).dot(axis)
                if bend.length()<.01:
                    bend = unreal.Vector(0,0,1)
                solver_refs=[]
                solver_keys=[]
                for j,(bone,bt) in enumerate(zip(chain,(a,b,c))):
                    rotation = unreal.MathLibrary.make_rot_from_xy(
                        ((b if j==0 else c).translation-bt.translation).normal(),bend.normal()) if j<2 else bt.rotation.rotator()
                    st=unreal.Transform(location=bt.translation,rotation=rotation)
                    sk=self.hc.add_null(f'IK_Frame_{i:02d}_{j:02d}',solver_keys[-1] if solver_keys else self.controls['CTRL_Global'],st,transform_in_global=True)
                    solver_refs.append(st);solver_keys.append(sk)
                    source=self.get(self.keys[bone],f'FK_Frame_{i:02d}_{j:02d}',x=-600)
                    mapped=self.multiply_offset(st.make_relative(bt),source,f'BindToIK_{i:02d}_{j:02d}',x=0)
                    self.set_item(sk,mapped,f'InitIK_{i:02d}_{j:02d}')
                self.value(ik+'.ItemA',key_text(solver_keys[0]))
                self.value(ik+'.ItemB',key_text(solver_keys[1]))
                self.value(ik+'.EffectorItem',key_text(solver_keys[2]))
                self.value(ik+'.PrimaryAxis','(X=1,Y=0,Z=0)')
                self.value(ik+'.SecondaryAxis','(X=0,Y=1,Z=0)')
                pole = b.translation + bend.normal()*300
                # Pole is in Global-control space, so moving the whole unit
                # does not change its preferred bend plane.
                self.value(ik+'.PoleVector',vector_text(pole))
                self.value(ik+'.PoleVectorKind','Location')
                self.value(ik+'.PoleVectorSpace',key_text(self.controls['CTRL_Global']))
                self.value(ik+'.bEnableStretch','False')
                target=self.multiply_offset(solver_refs[2].make_relative(c),clamp+'.Result',f'AnkleToIK_{i:02d}',x=100)
                self.link(target,ik+'.Effector')
            else:
                # UE5.7 CCDIK includes Items[0]'s parent in its solve and forces
                # the final effector onto the goal. Solve on isolated Nulls so
                # it cannot rotate Pelvis or stretch the visible final segment.
                hip_ref=self.info[chain[0]].global_transform
                base=self.hc.add_null(f'CCD_Base_{i:02d}',self.controls['CTRL_Global'],hip_ref,transform_in_global=True)
                self.set_item(base,self.get(self.keys[chain[0]],f'CCD_Hip_{i:02d}'),f'InitCCDBase_{i:02d}')
                solver_keys=[]
                for j,bone in enumerate(chain):
                    key=self.hc.add_null(f'CCD_Frame_{i:02d}_{j:02d}',solver_keys[-1] if solver_keys else base,
                                        self.info[bone].global_transform,transform_in_global=True)
                    solver_keys.append(key)
                    self.set_item(key,self.get(self.keys[bone],f'CCD_FK_{i:02d}_{j:02d}'),f'InitCCD_{i:02d}_{j:02d}')
                self.value(ik+'.Items','('+','.join(key_text(k) for k in solver_keys)+')')
                self.value(ik+'.Precision','0.05')
                self.value(ik+'.MaxIterations','96')
                self.value(ik+'.BaseRotationLimit','22')
                self.value(ik+'.RotationLimits','('+','.join('(Item='+key_text(k)+',Limit='+str(0 if k==base else 35)+')' for k in [base]+solver_keys)+')')
                self.link(clamp+'.Result',ik+'.EffectorTransform')
            self.link(weight+'.FloatValue',ik+'.Weight')
            self.execute(ik)
            if four:
                for j,(bone,st) in enumerate(zip(chain,solver_refs)):
                    source=self.get(solver_keys[j],f'Solved_{i:02d}_{j:02d}',x=-600)
                    mapped=self.multiply_offset(self.info[bone].global_transform.make_relative(st),source,f'IKToBind_{i:02d}_{j:02d}',x=0)
                    self.set_bone(bone,mapped)
            else:
                for j,bone in enumerate(chain):
                    if j==0:
                        self.set_bone(bone,self.get(solver_keys[j],f'CCD_Solved_{i:02d}_{j:02d}'))
                    else:
                        read=self.get(solver_keys[j],f'CCD_SolvedLocal_{i:02d}_{j:02d}',local=True)
                        set_node=self.node('RigUnit_SetTransform',f'RigidSegment_{i:02d}_{j:02d}',x=600)
                        self.value(set_node+'.Item',key_text(self.keys[bone]))
                        self.value(set_node+'.Space','LocalSpace')
                        self.value(set_node+'.Value',transform_text(self.info[bone].local_transform))
                        self.link(read+'.Rotation',set_node+'.Value.Rotation')
                        self.execute(set_node)

    def build_helpers(self):
        if self.unit != 'WM01':
            return
        for i in range(1,7):
            for suffix,target_suffix in [('b','c'),('c','b'),('d','c')]:
                bone=f'Leg_{i:02d}{suffix}_lever'
                end=self.info[bone+'_end'].global_transform
                ref=self.info[bone].global_transform
                target_bone=f'Leg_{i:02d}{target_suffix}'
                anchor=self.hc.add_null(f'Hydraulic_{i:02d}_{suffix}_Anchor',self.keys[target_bone],end,transform_in_global=True)
                target=self.get(anchor,f'Anchor_{i:02d}_{suffix}',x=0)
                aim=self.node('RigUnit_AimBone',f'HydraulicAim_{i:02d}_{suffix}',x=600)
                axis=ref.inverse_transform_direction((end.translation-ref.translation).normal()).normal()
                self.value(aim+'.Bone',str(self.keys[bone].name))
                self.value(aim+'.Primary.Axis',vector_text(axis))
                self.value(aim+'.Primary.Kind','Location')
                self.value(aim+'.Secondary.Weight','0')
                self.link(target+'.Translation',aim+'.Primary.Target')
                self.execute(aim)

    def finish(self):
        if self.unit == 'FourFRobot':
            existing = self.mesh.get_node_mapping_container(self.rig)
            mapping = existing or unreal.NodeMappingContainer(outer=self.mesh, name='CommanderRigBoneMapping')
            mapping.set_editor_property('source_asset',self.rig)
            mapping.set_editor_property('target_asset',self.mesh)
            mapping.set_editor_property('source_to_target',self.mapping)
            self.mesh.set_editor_property('node_mapping_data',[mapping])
        self.rig.recompile_vm()
        self.rig.set_auto_vm_recompile(True)
        self.rig.recompile_vm()
        instance = self.rig.create_control_rig()
        instance.request_init()
        events = [str(e) for e in instance.get_supported_events()]
        okay = instance.execute('Forwards Solve')
        h = instance.get_hierarchy()
        pose = {name:sources.serial_transform(h.get_global_transform(key)) for name,key in self.keys.items()}
        report = {'unit':self.unit,'paths':self.paths,'events':events,'execute':okay,
                  'status':'recompiled; inspect pose report and editor compile log', 'bone_map':self.mapping,
                  'bone_controls':self.bone_controls,'controls':list(self.controls), 'pose':pose,
                  'legs':[{k:(sources.serial_transform(v) if k=='offset' else v) for k,v in leg.items()} for leg in self.legs]}
        save_report(self.unit+'-rig-authoring.json',report)
        for path in (self.paths['mesh'],self.paths['skeleton'],self.paths['rig']):
            unreal.EditorAssetLibrary.save_asset(path,only_if_is_dirty=False)
        print(json.dumps({'unit':self.unit,'bones':len(self.keys),'controls':len(self.controls),'execute':okay,'events':events,'status':report['status']}))
        return instance


def build_rig(unit):
    author = RigAuthor(unit)
    globals()[unit + '_author'] = author
    author.build_controls()
    author.build_ik()
    author.build_helpers()
    instance = author.finish()
    # Keep instances alive for interactive pose inspection through the bridge.
    globals()[unit + '_author'] = author
    globals()[unit + '_instance'] = instance


def inspect_pose_states():
    """Read evaluated authoring poses. This is not visual acceptance or a test suite.

    Only transient Control Rig instances are posed; no runtime actor or asset
    control default is changed. Every instance is returned to reference values.
    """
    result = {'visual_acceptance':False, 'units':{}}
    for unit in ('FourFRobot','WM01'):
        author = globals()[unit+'_author']
        instance = author.rig.create_control_rig()
        instance.request_init()
        hierarchy = instance.get_hierarchy()
        facts = {'samples':[], 'bone_count':len(author.keys), 'control_count':len(author.controls)}
        samples = [('reference',None,unreal.Vector())]
        amount = 60. if unit=='FourFRobot' else 160.
        for leg in author.legs:
            for label,delta in [('lift',unreal.Vector(0,0,amount)),
                                ('forward',unreal.Vector(amount,0,0)),
                                ('side',unreal.Vector(0,amount,0))]:
                samples.append((leg['control']+'_'+label,leg['control'],delta))
        samples += [('body_raise','CTRL_Body',unreal.Vector(0,0,amount*.5))]
        for label,control,delta in samples:
            hierarchy.reset_pose_to_initial(unreal.RigElementType.ALL)
            instance.execute('Forwards Solve')
            if control:
                key=author.controls[control]
                t=hierarchy.get_global_transform(key)
                t.translation += delta
                hierarchy.set_global_transform(key,t,initial=False)
            executed=instance.execute('Forwards Solve')
            positions={name:sources.serial_transform(hierarchy.get_global_transform(key))
                       for name,key in author.keys.items()}
            lengths=[]
            endpoint_errors=[]
            for leg in author.legs:
                chain=leg['chain']
                for parent,child in zip(chain,chain[1:]):
                    initial=(author.info[child].global_transform.translation-author.info[parent].global_transform.translation).length()
                    current=(hierarchy.get_global_transform(author.keys[child]).translation-hierarchy.get_global_transform(author.keys[parent]).translation).length()
                    lengths.append(abs(current-initial))
                target=leg['offset'].multiply(hierarchy.get_global_transform(author.controls[leg['control']]))
                ankle=hierarchy.get_global_transform(author.keys[chain[-1]])
                endpoint_errors.append((ankle.translation-target.translation).length())
            facts['samples'].append({'name':label,'execute':executed,
                        'max_segment_length_change_cm':max(lengths),
                        'ankle_goal_errors_cm':endpoint_errors,'pose':positions})
        hierarchy.reset_pose_to_initial(unreal.RigElementType.ALL)
        instance.execute('Forwards Solve')
        facts['max_segment_length_change_cm']=max(x['max_segment_length_change_cm'] for x in facts['samples'])
        result['units'][unit]=facts
    result['dirty_content_packages']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    save_report('evaluated-pose-readback.json',result)
    print(json.dumps({unit:{'samples':len(facts['samples']),
                           'max_segment_length_change_cm':facts['max_segment_length_change_cm'],
                           'maximum_unclamped_goal_error_cm':max(max(s['ankle_goal_errors_cm']) for s in facts['samples'])}
                      for unit,facts in result['units'].items()}))


def add_adjustable_sockets():
    """Add missing mesh sockets for user placement, without changing runtime mounts.

    Existing socket transforms (including user-adjusted FX sockets) are never
    overwritten. Runtime Crowd positions are only approximate placement seeds;
    this function does NOT certify or perform a Skeleton-to-Crowd conversion.
    """
    lib = unreal.EditorAssetLibrary
    catalog = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')
    if not catalog:
        raise RuntimeError('Missing runtime mount catalog for initial reference positions')
    mounts = {(m.unit_type_id, str(m.slot_id)):m for m in catalog.get_editor_property('mounts')}

    def socket_data(info):
        local = unreal.Transform(location=info.relative_location, rotation=info.relative_rotation,
                                 scale=info.relative_scale)
        return {'name':str(info.socket_name), 'bone':str(info.bone_name),
                'local':sources.serial_transform(local)}

    def catalog_snapshot():
        return [{'unit':m.unit_type_id, 'slot':str(m.slot_id),
                 'muzzles':[[v.x,v.y,v.z] for v in m.muzzles],
                 'aim_offset':[m.aim_offset.x,m.aim_offset.y,m.aim_offset.z]}
                for m in catalog.get_editor_property('mounts')]

    report = {'scope':'create missing mesh sockets; user calibrates; runtime unchanged',
              'coordinate_space':'Socket Local is relative to its parent bone; reference_mesh is the SKM reference component space in centimeters.',
              'visual_calibration':'pending_user', 'units':{}, 'saved':[]}
    runtime_before = catalog_snapshot()
    plans = []
    # Preflight both targets before any mutation.
    for unit,unit_id in [('FourFRobot',1),('WM01',2)]:
        mesh,paths = check_owned(unit)
        bones = {b.bone_name:b for b in unreal.SkeletonService.list_bones(paths['mesh'])}
        gun = mounts[(unit_id,'BasicAttack')]
        if not gun.muzzles:
            raise RuntimeError('Missing basic attack placement reference')
        if unit == 'FourFRobot':
            # Preserve the actual original FName, including U+FFFD characters.
            candidates = [name for name in bones if name.endswith('3_019')]
            if len(candidates) != 1:
                raise RuntimeError('Could not uniquely resolve the FourF cannon deform bone')
            specs = [('FX_Muzzle_Basic_01',candidates[0],gun.muzzles[0],unreal.Rotator(yaw=180)),
                     ('FX_AimTarget','centro_00',gun.aim_offset,unreal.Rotator())]
        else:
            missiles = mounts[(unit_id,'MissileLauncher')]
            if len(missiles.muzzles) != 2:
                raise RuntimeError('Expected two WM01 missile placement references')
            specs = [('FX_Muzzle_Basic_01','Front_minigun',gun.muzzles[0],unreal.Rotator()),
                     ('FX_AimTarget','Mainbody',gun.aim_offset,unreal.Rotator()),
                     ('FX_Missile_01','Missile_launcher_01',missiles.muzzles[0],unreal.Rotator(pitch=90)),
                     ('FX_Missile_02','Missile_launcher_02',missiles.muzzles[1],unreal.Rotator(pitch=90))]
        for _,bone,_,_ in specs:
            if bone not in bones:
                raise RuntimeError('Missing parent bone: '+bone)
        before = {str(s.socket_name):socket_data(s) for s in unreal.SkeletonService.list_sockets(paths['mesh'])}
        plans.append((unit,mesh,paths,bones,specs,before))

    for unit,mesh,paths,bones,specs,before in plans:
        created,retained = [],[]
        expected = {}
        for name,bone,point,rotation in specs:
            if name in before:
                retained.append(name)
                continue
            parent = bones[bone].global_transform
            seed = unreal.Transform(location=point,rotation=rotation,scale=parent.scale3d)
            relative = seed.make_relative(parent)
            if not unreal.SkeletonService.add_socket(paths['mesh'],name,bone,relative.translation,
                   relative.rotation.rotator(),unreal.Vector(1,1,1),add_to_skeleton=False):
                raise RuntimeError('Socket creation failed: '+unit+'/'+name)
            created.append(name)
            expected[name] = seed
        after = {str(s.socket_name):s for s in unreal.SkeletonService.list_sockets(paths['mesh'])}
        if len(after) != len(before)+len(created):
            raise RuntimeError('Unexpected socket count: '+unit)
        for name,data in before.items():
            if socket_data(after[name]) != data:
                raise RuntimeError('Existing socket changed unexpectedly: '+name)
        rows=[]
        for name,bone,_,_ in specs:
            info=after[name]
            data=socket_data(info)
            local=unreal.Transform(location=info.relative_location,rotation=info.relative_rotation,scale=info.relative_scale)
            component=local.multiply(bones[str(info.bone_name)].global_transform)
            data['reference_mesh']=sources.serial_transform(component)
            forward=component.transform_direction(unreal.Vector(1,0,0)).normal()
            data['reference_mesh_x_axis']=[forward.x,forward.y,forward.z]
            if name in expected:
                target=expected[name]
                target_forward=target.transform_direction(unreal.Vector(1,0,0)).normal()
                if str(info.bone_name)!=bone or (component.translation-target.translation).length()>.01 or forward.dot(target_forward)<.99999:
                    raise RuntimeError('Socket parent/transform readback mismatch: '+name)
            rows.append(data)
        if created:
            lib.set_metadata_tag(mesh,'CommanderRigSocketCalibration','PendingUserAdjustment')
            if not lib.save_asset(paths['mesh'],only_if_is_dirty=False):
                raise RuntimeError('Socket asset save failed: '+paths['mesh'])
            report['saved'].append(paths['mesh'])
            persisted={str(s.socket_name):socket_data(s) for s in unreal.SkeletonService.list_sockets(paths['mesh'])}
            if any(persisted[name] != socket_data(after[name]) for name,_,_,_ in specs):
                raise RuntimeError('Post-save socket readback mismatch: '+unit)
        report['units'][unit]={'mesh':paths['mesh'],'socket_storage':'mesh_only',
                 'created':created,'retained':retained,'socket_count_before':len(before),
                 'socket_count_after':len(after),'sockets':rows,
                 'bones_unchanged':list(bones)==[b.bone_name for b in unreal.SkeletonService.list_bones(paths['mesh'])]}
    report['runtime_mounts_unchanged'] = runtime_before==catalog_snapshot()
    if not report['runtime_mounts_unchanged']:
        raise RuntimeError('Runtime mount catalog unexpectedly changed')
    report['dirty_content_packages']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    save_report('adjustable-sockets-readback.json',report)
    print(json.dumps({'saved':report['saved'], 'created':{u:d['created'] for u,d in report['units'].items()},
                      'runtime_mounts_unchanged':report['runtime_mounts_unchanged'],
                      'dirty_content_packages':report['dirty_content_packages']}))


def wm01_preview_snapshot():
    """Capture editable invariants before the explicitly approved preview repair."""
    import hashlib
    mesh, paths = check_owned('WM01')
    bones = unreal.SkeletonService.list_bones(paths['mesh'])
    sk_bones = unreal.SkeletonService.list_bones(paths['skeleton'])
    modifier = unreal.SkinWeightModifier()
    if not modifier.set_skeletal_mesh(mesh):
        raise RuntimeError('Cannot inspect WM01 skin weights')
    digest = hashlib.sha256()
    sync_leaf_influences = 0
    for index in range(modifier.get_num_vertices()):
        weights = sorted((str(n), float(w)) for n, w in modifier.get_vertex_weights(index).items())
        digest.update(json.dumps(weights, ensure_ascii=True, separators=(',', ':')).encode('utf-8'))
        sync_leaf_influences += sum(1 for n, w in weights if n == 'root_end' and w > 0)
    def bone_rows(items):
        return [{'name':str(b.bone_name), 'parent':str(b.parent_bone_name),
                 'local':sources.serial_transform(b.local_transform)} for b in items]
    def vector(v):
        return [v.x, v.y, v.z]
    socket_rows = []
    for s in unreal.SkeletonService.list_sockets(paths['mesh']):
        socket_rows.append({'name':str(s.socket_name), 'bone':str(s.bone_name),
                            'location':vector(s.relative_location),
                            'rotation':[s.relative_rotation.pitch, s.relative_rotation.yaw, s.relative_rotation.roll],
                            'scale':vector(s.relative_scale)})
    return {'mesh':paths['mesh'], 'skeleton':mesh.skeleton.get_path_name(),
            'bones':bone_rows(bones), 'skeleton_bones':bone_rows(sk_bones),
            'vertex_count':modifier.get_num_vertices(), 'skin_weight_sha256':digest.hexdigest(),
            'sync_leaf_influences':sync_leaf_influences,
            'sockets':sorted(socket_rows, key=lambda s:s['name']),
            'materials':[m.material_interface.get_path_name() if m.material_interface else None
                         for m in mesh.get_editor_property('materials')],
            'physics_asset':mesh.get_editor_property('physics_asset').get_path_name(),
            'socket_calibration':unreal.EditorAssetLibrary.get_metadata_tag(mesh, 'CommanderRigSocketCalibration'),
            'dirty':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}


def repair_wm01_preview_assets(resume_unsaved=False):
    """Synchronize the private Skeleton and replace incompatible physics bounds.

    UE5.7 does not expose UpdateReferencePoseFromMesh to Python. Its supported
    SkeletonModifier commit updates USkeleton only for topology changes, not for
    transform-only edits. On this exclusively referenced private Skeleton, the
    unused helper leaf root_end is temporarily renamed and then restored. Both
    supported commits request USkeleton synchronization; a removal-only commit
    would not do so in UE5.7. No deform bone is renamed, no bone is added/removed,
    and neither intermediate state is saved. Final names, hierarchy, transforms,
    weights and sockets must exactly match the original snapshot.
    """
    mesh, paths = check_owned('WM01')
    lib = unreal.EditorAssetLibrary
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    folder = paths['mesh'].rsplit('/', 1)[0]
    physics_path = folder+'/PA_WM01_Rig'
    generated_physics_path = paths['mesh']+'_PhysicsAsset'
    temporary_bone = 'WM01_ReferenceSync_Temporary_20260907'
    sync_leaf = 'root_end'
    before_file = OUT/'wm01-preview-repair-before-20260907.json'
    if before_file.exists() and not resume_unsaved:
        raise RuntimeError('Repair baseline already exists; inspect the previous run before retrying')
    if any(lib.does_asset_exist(p) for p in [physics_path, generated_physics_path]):
        raise RuntimeError('Physics destination already exists; refusing to replace it')
    refs = [str(r) for r in ar.get_referencers(paths['skeleton'], unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True, include_hard_package_references=True))]
    if set(refs) != {paths['mesh']}:
        raise RuntimeError('Private skeleton has additional references: '+str(refs))
    dirty = {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if dirty.intersection(paths.values()):
        raise RuntimeError('Target assets have unsaved user changes; preserve them before repair')
    current = wm01_preview_snapshot()
    if before_file.exists():
        before = json.loads(before_file.read_text(encoding='utf-8'))
        # Resume is allowed only after a failed, unsaved commandlet has exited;
        # the on-disk asset must still match every original protected invariant.
        for key in ['bones', 'skeleton_bones', 'vertex_count', 'skin_weight_sha256',
                    'sockets', 'materials', 'physics_asset', 'skeleton', 'socket_calibration']:
            if current[key] != before[key]:
                raise RuntimeError('Cannot resume: original snapshot differs in '+key)
    else:
        before = current
        save_report(before_file.name, before)
    if len(before['bones']) != 99 or len(before['sockets']) != 13:
        raise RuntimeError('WM01 baseline changed; inspect before proceeding')
    bone_names = {r['name'] for r in before['bones']}
    if temporary_bone in bone_names:
        raise RuntimeError('Temporary repair bone already exists')
    if sync_leaf not in bone_names or current['sync_leaf_influences'] != 0:
        raise RuntimeError('Synchronization helper is absent or has skin influences')
    if any(r['parent'] == sync_leaf for r in before['bones']) or any(s['bone'] == sync_leaf for s in before['sockets']):
        raise RuntimeError('Synchronization helper has children or socket attachments')
    report = {'before_report':before_file.name, 'stages':[], 'saved':[]}
    try:
        modifier = unreal.SkeletonModifier()
        if not modifier.set_skeletal_mesh(mesh):
            raise RuntimeError('Cannot load mesh into SkeletonModifier')
        if not modifier.rename_bone(sync_leaf, temporary_bone):
            raise RuntimeError('Could not stage reference synchronization')
        try:
            if not modifier.commit_skeleton_to_skeletal_mesh():
                raise RuntimeError('Skeleton reference synchronization commit failed')
            report['stages'].append('private_skeleton_reference_synchronized')
        finally:
            # Restore only the verified unused helper name; never delete bones.
            committed = {str(b.bone_name) for b in unreal.SkeletonService.list_bones(paths['mesh'])}
            if temporary_bone in committed:
                cleanup = unreal.SkeletonModifier()
                if not cleanup.set_skeletal_mesh(mesh) or not cleanup.rename_bone(temporary_bone, sync_leaf):
                    raise RuntimeError('Synchronization helper name restoration failed')
                if not cleanup.commit_skeleton_to_skeletal_mesh():
                    raise RuntimeError('Synchronization helper restoration commit failed')
                report['stages'].append('helper_name_restored_99_original_bones_retained')

        mesh_bones = unreal.SkeletonService.list_bones(paths['mesh'])
        skeleton_bones = unreal.SkeletonService.list_bones(paths['skeleton'])
        if len(mesh_bones) != 99 or len(skeleton_bones) != 99:
            raise RuntimeError('Unexpected final bone count')
        for mb, sb in zip(mesh_bones, skeleton_bones):
            if mb.bone_name != sb.bone_name or mb.parent_bone_name != sb.parent_bone_name:
                raise RuntimeError('Mesh/Skeleton hierarchy mismatch')
            mt, st = mb.local_transform, sb.local_transform
            if mt.translation.distance(st.translation)>0.0001 or mt.scale3d.distance(st.scale3d)>0.000001:
                raise RuntimeError('Mesh/Skeleton reference translation/scale mismatch')
            qdot = abs(sum(getattr(mt.rotation, c)*getattr(st.rotation, c) for c in ['x','y','z','w']))
            if qdot < .99999999:
                raise RuntimeError('Mesh/Skeleton reference rotation mismatch')

        editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
        physics = editor.create_physics_asset(mesh, set_to_mesh=False, lod_index=0)
        if not physics or physics.get_path_name().split('.')[0] != generated_physics_path:
            raise RuntimeError('Project physics generation failed or returned an unexpected asset')
        lib.set_metadata_tag(physics, 'CommanderRigOwner', '20260906')
        if not lib.rename_loaded_asset(physics, physics_path):
            raise RuntimeError('Could not name the project PhysicsAsset')
        physics = unreal.load_asset(physics_path)
        if not editor.assign_physics_asset(mesh, physics):
            raise RuntimeError('New physics asset is incompatible with the WM01 mesh')
        report['stages'].append('project_physics_asset_generated_and_assigned')

        after = wm01_preview_snapshot()
        for key in ['bones', 'vertex_count', 'skin_weight_sha256', 'sockets', 'materials', 'socket_calibration']:
            if before[key] != after[key]:
                raise RuntimeError('Repair changed a protected invariant: '+key)
        if after['skeleton'] != before['skeleton']:
            raise RuntimeError('Skeleton object/path was unexpectedly replaced')
        if after['physics_asset'].split('.')[0] != physics_path:
            raise RuntimeError('Physics assignment readback failed')
        report['invariants'] = {'original_bones':99, 'sockets_unchanged':13,
                                'skin_weight_sha256':after['skin_weight_sha256'],
                                'vertex_count':after['vertex_count'], 'materials_unchanged':True,
                                'same_skeleton_asset':True}
        lib.set_metadata_tag(mesh, 'CommanderRigPreviewRepair', '20260907_ReferenceAndPhysicsSynchronized')
        for path in [paths['skeleton'], physics_path, paths['mesh']]:
            if not lib.save_asset(path, only_if_is_dirty=False):
                raise RuntimeError('Explicit save failed: '+path)
            report['saved'].append(path)
        report['after'] = wm01_preview_snapshot()
        report['status'] = 'saved_requires_reopen_visual_verification'
    except Exception as exc:
        report['status'] = 'stopped_inspect_before_retry'
        report['error'] = str(exc)
        raise
    finally:
        report['dirty'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
        save_report('wm01-preview-repair-20260907.json', report)
    print(json.dumps({'status':report['status'], 'saved':report['saved'], 'invariants':report['invariants'], 'dirty':report['dirty']}))


def verify_wm01_preview_repair():
    """Read saved assets and the open preview; do not change or save any asset."""
    mesh, paths = check_owned('WM01')
    before = json.loads((OUT/'wm01-preview-repair-before-20260907.json').read_text(encoding='utf-8'))
    current = wm01_preview_snapshot()
    unchanged = {key:before[key] == current[key] for key in
                 ['bones', 'vertex_count', 'skin_weight_sha256', 'sockets', 'materials', 'socket_calibration']}
    if not all(unchanged.values()):
        raise RuntimeError('Saved repair invariant mismatch: '+str(unchanged))
    mesh_bones = unreal.SkeletonService.list_bones(paths['mesh'])
    sk_bones = unreal.SkeletonService.list_bones(paths['skeleton'])
    if len(mesh_bones) != 99 or len(sk_bones) != 99:
        raise RuntimeError('Saved bone count mismatch')
    position_deltas, scale_deltas, rotation_deltas = [], [], []
    for mb, sb in zip(mesh_bones, sk_bones):
        if mb.bone_name != sb.bone_name or mb.parent_bone_name != sb.parent_bone_name:
            raise RuntimeError('Saved skeleton hierarchy mismatch')
        a, b = mb.local_transform, sb.local_transform
        position_deltas.append(a.translation.distance(b.translation))
        scale_deltas.append(a.scale3d.distance(b.scale3d))
        dot = abs(sum(getattr(a.rotation, c)*getattr(b.rotation, c) for c in ['x','y','z','w']))
        rotation_deltas.append(math.degrees(2*math.acos(min(1.0, dot))))
    if max(position_deltas)>.0001 or max(scale_deltas)>.000001 or max(rotation_deltas)>.0001:
        raise RuntimeError('Saved Skeleton reference pose mismatch')
    physics_path = paths['mesh'].rsplit('/',1)[0]+'/PA_WM01_Rig'
    if current['physics_asset'].split('.')[0] != physics_path:
        raise RuntimeError('Saved project physics assignment mismatch')
    rig = unreal.load_asset(paths['rig'])
    instance = rig.create_control_rig()
    instance.request_init()
    executed = instance.execute('Forwards Solve')
    if not executed:
        raise RuntimeError('Saved Control Rig default evaluation failed')
    h = instance.get_hierarchy()
    rig_deltas = [h.get_global_transform(k, initial=True).translation.distance(
                  h.get_global_transform(k, initial=False).translation)
                  for k in h.get_all_keys(traverse=True) if k.type == unreal.RigElementType.BONE]
    previews = []
    for component in unreal.ObjectIterator(unreal.SkeletalMeshComponent):
        if component.get_skinned_asset() != mesh:
            continue
        origin, extent, radius = unreal.SystemLibrary.get_component_bounds(component)
        fixed = component.get_editor_property('component_use_fixed_skel_bounds')
        if fixed or max(extent.x, extent.y, extent.z)>10000:
            raise RuntimeError('Preview still has oversized or overridden bounds')
        previews.append({'component':component.get_path_name(),
                         'dimensions_cm':[2*extent.x,2*extent.y,2*extent.z],
                         'origin_cm':[origin.x,origin.y,origin.z],
                         'sphere_radius_cm':radius, 'fixed_bounds_override':fixed,
                         'bounds_scale':component.get_editor_property('bounds_scale')})
    report = {'status':'saved_reload_and_preview_data_verified_visual_pending',
              'engine':unreal.SystemLibrary.get_engine_version(), 'protected_invariants':unchanged,
              'bone_counts':[len(mesh_bones),len(sk_bones)], 'socket_count':len(current['sockets']),
              'reference_position_delta_cm':max(position_deltas), 'reference_scale_delta':max(scale_deltas),
              'reference_rotation_delta_deg':max(rotation_deltas), 'physics_asset':current['physics_asset'],
              'rig_default_execute':executed, 'rig_bone_count':len(rig_deltas),
              'rig_default_max_position_delta_cm':max(rig_deltas), 'previews':previews,
              'dirty':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
    save_report('wm01-preview-repair-reloaded-20260907.json', report)
    print(json.dumps(report))
