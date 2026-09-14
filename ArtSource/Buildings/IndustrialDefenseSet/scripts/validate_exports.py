"""Read exported FBX/GLB back through Blender and check their actual contents."""
import bpy
import sys
import json
import struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from blender_common import bounds, describe, Vector

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/(sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'delivery')
expected=json.loads((OUT/'Validation_Report.json').read_text(encoding='utf-8'))
checks=[]

def import_file(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if path.suffix=='.glb':
        bpy.ops.import_scene.gltf(filepath=str(path),import_pack_images=True,disable_bone_shape=True)
    else:
        bpy.ops.import_scene.fbx(filepath=str(path))
    bpy.context.view_layer.update()
    return [o for o in bpy.context.scene.objects if o.type=='MESH']

def world_points(ob):
    dg=bpy.context.evaluated_depsgraph_get()
    ev=ob.evaluated_get(dg)
    mesh=ev.to_mesh()
    points=[ev.matrix_world @ v.co for v in mesh.vertices]
    ev.to_mesh_clear()
    return points

for name in ['RedOreRefinery','ShieldGenerator','HeavyDefenseCannon']:
    prefix='SK_' if name=='HeavyDefenseCannon' else 'SM_'
    for extension in ['fbx','glb']:
        path=OUT/name/(prefix+name+'.'+extension)
        obs=import_file(path)
        lo,hi=bounds(obs)
        error=max(abs((hi-lo)[i]-expected['assets'][name]['dimensions_m'][i]) for i in range(3))
        arms=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
        record={'file':str(path.relative_to(OUT)),'mesh_objects':len(obs),
                'dimensions_m':list(hi-lo),'max_dimension_error_m':error,
                'triangles':sum(len(o.data.loop_triangles) for o in obs),
                'uv_missing':sum(not o.data.uv_layers for o in obs),
                'armature_count':len(arms),'bone_names':[b.name for a in arms for b in a.data.bones]}
        if extension=='glb':
            content=path.read_bytes()
            size=struct.unpack_from('<I',content,12)[0]
            manifest=json.loads(content[20:20+size])
            record['gltf_scene_count']=len(manifest.get('scenes',[]))
            record['gltf_animation_count']=len(manifest.get('animations',[]))
        if name=='HeavyDefenseCannon':
            bad=0
            for ob in obs:
                for v in ob.data.vertices:
                    groups=[g for g in v.groups if g.weight>1e-6]
                    bad+=not(len(groups)==1 and abs(groups[0].weight-1)<1e-6)
            record['invalid_rigid_weights']=bad
            record['bone_parents']={b.name:b.parent.name if b.parent else None for a in arms for b in a.data.bones}
            valid=(len(obs)==3 and len(arms)==1 and set(record['bone_names'])=={'root','base_yaw','barrel_pitch'} and bad==0)
        else:
            valid=(len(obs)==1 and not arms)
        record['passed']=bool(valid and error<.001 and not record['uv_missing'] and record.get('gltf_scene_count',1)==1
                              and record.get('gltf_animation_count',0)==0)
        checks.append(record)
        print('ROUNDTRIP '+json.dumps(record),flush=True)

for extension in ['fbx','glb']:
    path=OUT/'HeavyDefenseCannon'/('SK_HeavyDefenseCannon_AimDemo.'+extension)
    obs=import_file(path)
    actions=list(bpy.data.actions)
    frame_range=[min(a.frame_range[0] for a in actions),max(a.frame_range[1] for a in actions)] if actions else [0,0]
    first=round(frame_range[0])
    last=round(frame_range[1])
    bpy.context.scene.frame_set(first)
    bpy.context.view_layer.update()
    foundation=next(o for o in obs if 'Foundation' in o.name)
    barrel=next(o for o in obs if 'PitchAssembly' in o.name)
    root_start=world_points(foundation)
    barrel_start=world_points(barrel)
    samples=[]
    for fraction in [.16,.33,.5,.66,.83,1.0]:
        frame=round(first+(last-first)*fraction)
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        fixed=world_points(foundation)
        moving=world_points(barrel)
        samples.append({'frame':frame,'fixed_base_displacement_m':max((a-b).length for a,b in zip(fixed,root_start)),
                         'barrel_max_displacement_m':max((a-b).length for a,b in zip(moving,barrel_start))})
    result={'file':str(path.relative_to(OUT)),'actions':[a.name for a in actions],
            'frame_range':frame_range,'samples':samples,
            'passed':bool(actions and max(s['fixed_base_displacement_m'] for s in samples)<.0001
                          and max(s['barrel_max_displacement_m'] for s in samples)>.2)}
    checks.append(result)
    print('ANIMATION '+json.dumps(result),flush=True)

report={'passed':all(c['passed'] for c in checks),'checks':checks}
(OUT/'Export_Roundtrip_Report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('EXPORT_VALIDATION '+str(report['passed']),flush=True)
if not report['passed']:
    raise RuntimeError('One or more export checks failed; see Export_Roundtrip_Report.json')
