"""Supplemental read-only ControlRig hierarchy and source animation pose samples."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
BASE = '/Game/Assets/ControlRig/Characters/Mech'
FLAG = '-ControlRigMechSourceReadWorker'


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


def transform(t):
    q = t.rotation
    return {'translation_cm': vec(t.translation),
            'rotation_xyzw': [float(q.x), float(q.y), float(q.z), float(q.w)],
            'scale': vec(t.scale3d)}


def main():
    assert FLAG in unreal.SystemLibrary.get_command_line()
    cr = unreal.load_asset(BASE + '/Rigs/CR_Mech')
    hierarchy = cr.get_editor_property('hierarchy')
    report = {'purpose': 'read-only source interface and pose baseline; no B validation',
              'rig': cr.get_path_name(), 'elements': [], 'graphs': [], 'animations': [], 'ue_assets_saved': False}
    for k in hierarchy.get_all_keys():
        entry = {'name': str(k.name), 'type': str(k.type)}
        try:
            entry['parents'] = [{'name': str(p.name), 'type': str(p.type)} for p in hierarchy.get_parents(k)]
            entry['initial_global'] = transform(hierarchy.get_global_transform(k, initial=True))
        except Exception as e:
            entry['optional_note'] = str(e)
        report['elements'].append(entry)
    for graph in cr.get_all_models():
        item = {'path': graph.get_path_name(), 'nodes': [], 'links': []}
        for node in graph.get_nodes():
            entry = {'name': node.get_name(), 'class': node.get_class().get_name(), 'pins': []}
            try:
                entry['title'] = node.get_node_title()
                if hasattr(node, 'get_script_struct'):
                    script_struct = node.get_script_struct()
                    if script_struct:
                        entry['unit_struct'] = script_struct.get_path_name()
                for pin in node.get_pins():
                    entry['pins'].append({'name': pin.get_name(), 'type': pin.get_cpp_type(),
                                          'default_value': pin.get_default_value()})
            except Exception as e:
                entry['optional_read_note'] = str(e)
            item['nodes'].append(entry)
        for link in graph.get_links():
            try:
                item['links'].append({'source': link.get_source_pin().get_pin_path(),
                                      'target': link.get_target_pin().get_pin_path()})
            except Exception as e:
                item['links'].append({'optional_read_note': str(e)})
        report['graphs'].append(item)
    for name in ('Mech_Deploy', 'Mech_Idle', 'Mech_Walk'):
        p = BASE + '/Animations/' + name
        info = unreal.AnimSequenceService.get_anim_sequence_info(p)
        anim = {'name': name, 'path': p, 'duration_s': float(info.duration),
                'frame_rate': float(info.frame_rate), 'sampled_keys': int(info.frame_count),
                'raw_bone_track_count': int(info.bone_track_count),
                'raw_track_count_note': 'Raw data model may be absent in migrated assets; zero does not mean motion is absent',
                'rate_scale': float(info.rate_scale), 'samples': []}
        for t in (0.0, float(info.duration) * .5, float(info.duration)):
            poses = unreal.AnimSequenceService.get_pose_at_time(p, t, False)
            anim['samples'].append({'time_s': t, 'space': 'UE bone local cm',
                'bones': [{'name': str(b.bone_name), 'index': int(b.bone_index),
                           'transform': transform(b.transform)} for b in poses]})
        report['animations'].append(anim)
    (ROOT / 'Source/rig_animation_baseline.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('CONTROLRIG_RIG_ANIMATION_QUERY_OK ' + str(len(report['elements'])))


if __name__ == '__main__':
    try:
        main()
    except Exception:
        (ROOT / 'rig_animation_query_error.txt').write_text(traceback.format_exc(), encoding='utf-8')
        unreal.log_error(traceback.format_exc())
    finally:
        if FLAG in unreal.SystemLibrary.get_command_line():
            unreal.SystemLibrary.quit_editor()
