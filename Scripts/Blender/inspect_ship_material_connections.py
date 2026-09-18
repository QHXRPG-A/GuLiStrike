"""Read-only diagnosis of the user-marked material seams on existing meshes."""
import bpy
import importlib.util
import json
from pathlib import Path
from mathutils import Vector
spec=importlib.util.spec_from_file_location('study',Path('D:/UE5.7/test1/Scripts/Blender/style_ship_component_original_materials.py'))
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
folder=s.OUT/'ConnectionDiagnosis';folder.mkdir(exist_ok=True)
for key in ('CIWS','Twin_Barrel_Turret'):
    bpy.ops.wm.open_mainfile(filepath=str(s.OUT/(key+'_OriginalMesh_MaterialCandidate.blend')))
    body=next(o for o in bpy.context.scene.objects if o.type=='MESH' and o.get('source_file'))
    shell=next(o for o in bpy.context.scene.objects if o.get('render_helper_only'));shell.hide_render=True
    mat=body.data.materials[0];mat.node_tree.nodes['Line_Strength'].outputs[0].default_value=0
    s.render(folder/(key+'_no_ink.png'))
    source_normals=[n.vector[:] for n in body.data.corner_normals]
    body.data.normals_split_custom_set([(0,0,0)]*len(body.data.loops))
    s.render(folder/(key+'_automatic_normals.png'))
    body.data.normals_split_custom_set(source_normals)
    # A single neutral toon color isolates texture assignment from normals.
    body.data.materials.clear();body.data.materials.append(s.toon_material('Diagnostic_Uniform','Pearl'))
    for p in body.data.polygons:p.material_index=0
    s.render(folder/(key+'_uniform_toon.png'))
    face_seed=body.data.attributes['OriginalComponentSeed']
    groupmap={vg.index:vg.name for vg in body.vertex_groups}
    records=[]
    for p in body.data.polygons:
        if key=='CIWS' and abs(p.center.x)<1.03 and -.9<p.center.y<.8:
            records.append({'face':p.index,'seed':face_seed.data[p.index].value,'center':list(p.center),'normal':list(p.normal),
                            'bone_owners':[[groupmap[g.group] for g in body.data.vertices[i].groups if g.weight>.5] for i in p.vertices],
                            'corner_normals':[list(body.data.corner_normals[i].vector) for i in p.loop_indices]})
    s.dump(folder/(key+'_diagnosis.json'),{'attributes':[(a.name,a.domain,a.data_type) for a in body.data.attributes],
            'geometry_sha256':s.ink_bake.geometry_hash(body.data),'roi_faces':records})
print('CONNECTION_DIAGNOSIS_COMPLETE',flush=True)
