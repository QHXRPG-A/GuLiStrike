"""Reload the candidate and read back its actual color/structure contracts."""
import bpy,json,hashlib,ast
import numpy as np
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'TeamPalette_B_v2_20261007'
M=json.loads((O/'candidate_manifest.json').read_text(encoding='utf8'))
assert Path(bpy.data.filepath).resolve()==Path(M['candidate_blend']).resolve()
module=ast.parse((R/'Scripts/build_team_palette_b2.py').read_text(encoding='utf8'))
selected=ast.Module(body=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name in ('srgb','linear','mesh_signature','bone_signature')],type_ignores=[])
exec(compile(selected,'ReadbackFunctions','exec'))
out={'success':False,'independent_Blender_reload':True,'candidate_blend_sha256':M['candidate_blend_sha256'],
     'UE_modified':False,'geometry_checks':[],'palette_checks':[],'anchors':[],'shader_checks':[]}
assert hashlib.sha256(Path(M['candidate_blend']).read_bytes()).hexdigest()==M['candidate_blend_sha256']
for a in M['assets']:
    team,key=a['team'],a['key'];rig=bpy.data.objects[a['rig']]
    assert bone_signature(rig)==a['bone_signature']
    codes=list(M['role_mapping'][team][key].values());palette=np.array([linear(c) for c in codes])
    for lod in a['lods']:
        for item in lod['objects']:
            ob=bpy.data.objects[item['name']];original=bpy.data.objects[item['source']]
            assert mesh_signature(ob)==mesh_signature(original)==item['geometry_UV_normals_weights_signature']
            out['geometry_checks'].append({'team':team,'asset':key,'LOD':lod['LOD'],'role':item['role'],'signature_equal':True})
        ob=bpy.data.objects[lod['body']];m=ob.data;attr=m.color_attributes['SSF_PaletteLinear']
        values=np.empty(len(attr.data)*4,np.float32);attr.data.foreach_get('color',values);values=values.reshape(-1,4)
        opaque=np.ones(len(m.loops),bool)
        for p in m.polygons:
            if 'SourceTranslucent' in m.materials[p.material_index].name:opaque[list(p.loop_indices)]=False
        error=float(np.sqrt(((values[opaque,:3,None]-palette.T[None,:,:])**2).sum(1).min(1)).max())
        assert error<1e-6,(team,key,lod['LOD'],error)
        out['palette_checks'].append({'team':team,'asset':key,'LOD':lod['LOD'],'opaque_corners':int(opaque.sum()),'maximum_linear_RGB_error':error})
        mat=m.materials[0];nt=mat.node_tree
        ramp=nt.nodes['Approved_A7_ThreeTone'].color_ramp
        factors=[float(e.color[0]) for e in ramp.elements];thresholds=[float(e.position) for e in ramp.elements]
        assert np.max(np.abs(np.array(factors)-(.4,.72,1.)))<1e-6
        assert np.max(np.abs(np.array(thresholds)-(0,.38,.68)))<1e-6
        assert abs(nt.nodes['InternalLineStrength'].inputs[1].default_value-(1.,.7,0.)[lod['LOD']])<1e-6
        assert np.max(np.abs(np.array(nt.nodes['Team Color'].outputs[0].default_value[:3])-linear(codes[2])))<1e-6
        out['shader_checks'].append({'team':team,'asset':key,'LOD':lod['LOD'],'three_bands':factors,'thresholds':thresholds,
          'independent_line_mask':nt.nodes['Independent_InternalLineMask'].image.name,'outline_present':bool(lod.get('outline')),
          'Base_Color_interface_preserved':nt.nodes['Base Color'].outputs[0].default_value[:]==(1.,1.,1.,1.)})
    if (team,key) in (('Blue','AirBase'),('Red','CommandCenter')):
        candidate=bpy.data.objects[a['lods'][0]['body']].data.color_attributes['SSF_PaletteLinear']
        original=bpy.data.objects[a['lods'][0]['objects'][0]['source']].data.color_attributes['SSF_PaletteLinear']
        x=np.empty(len(candidate.data)*4,np.float32);y=np.empty_like(x);candidate.data.foreach_get('color',x);original.data.foreach_get('color',y)
        error=float(np.max(np.abs(x-y)));assert error<1e-6
        out['anchors'].append({'team':team,'asset':key,'maximum_palette_change_from_actual_attached_anchor':error})
construction=json.loads((R/'Production_B_v1/construction_report.json').read_text(encoding='utf8'))
curves=[]
for item in construction['animations']:
    action=bpy.data.actions[item['action']]
    count=sum(len(strip.channelbag(slot).fcurves) for layer in action.layers for strip in layer.strips for slot in action.slots if strip.channelbag(slot))
    assert count==item['curve_count']
    curves.append({'action':action.name,'curve_count':count,'retained_original_action':True})
out['retained_source_animations']=curves;assert len(curves)==22
out['original_B_v1_file_preserved']=hashlib.sha256(Path(M['source_blend']).read_bytes()).hexdigest()==M['source_blend_sha256']
assert out['original_B_v1_file_preserved']
out['success']=True
(O/'native_validation.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_TEAM_PALETTE_RELOADED_OK',len(out['geometry_checks']),len(out['palette_checks']),len(curves),flush=True)
