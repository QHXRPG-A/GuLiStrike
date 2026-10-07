"""Read back only WM01 turn assets and the saved Mass preview, without running PIE.

Exports the real render LODs for the companion Blender inspection. Set
GULI_TURN_INSPECT_SCENE=False for the asset-only stage before scene authoring.
"""
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/WarMachineTurn_20260930'
MESH='/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid'
FN='/Game/Commander/Units/MechanicalAnimation/MF_GuLiRigidMechanical'
MAP='/Game/Maps/LVL_CommanderMassPrototype'
LIB=unreal.EditorAssetLibrary
r={'success':False,'native_compile':False,'runtime_validation':False}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    spec=json.loads((ROOT/'ArtSource/MechanicalAnimation_20260929/WarMachine/manifest.json').read_text(encoding='utf-8'))
    mesh=unreal.load_asset(MESH);assert isinstance(mesh,unreal.StaticMesh)
    sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    before=json.loads((OUT/'ue-import.json').read_text(encoding='utf-8'))['assets'][0]['old_dimensions_cm']
    dimensions=list((mesh.get_bounds().box_extent*2).to_tuple())
    assert max(abs(a-b) for a,b in zip(before,dimensions))<.01,(before,dimensions)
    sockets={}
    for name,pos in spec['sockets_cm'].items():
        s=mesh.find_socket(name);assert s,name
        loc=list(s.relative_location.to_tuple())
        assert max(abs(a-b) for a,b in zip(loc,pos))<.01,name
        sockets[name]={'position_cm':loc,'rotation':list(s.relative_rotation.to_tuple()),'scale':list(s.relative_scale.to_tuple())}
    for joint in spec['leg_joints_cm']:
        s=mesh.find_socket('Rigid_LegRoot_'+joint['label'])
        axis=unreal.MathLibrary.get_forward_vector(s.relative_rotation)
        assert max(abs(a-b) for a,b in zip(axis.to_tuple(),joint['axis']))<.0001
    for label in ('FL','FR','RL','RR'):
        s=mesh.find_socket('FX_Hover_'+label);assert s and s.relative_scale.x>0
        sockets['FX_Hover_'+label]={'position_cm':list(s.relative_location.to_tuple()),'diameter_cm':s.relative_scale.x}
    for name in ('FX_Muzzle_Basic_01','FX_Muzzle_Basic_02','FX_Missile_01','FX_Missile_02'):
        socket=mesh.find_socket(name);assert socket,name
        sockets[name]={'position_cm':list(socket.relative_location.to_tuple()),'rotation':list(socket.relative_rotation.to_tuple())}
    lods=[]
    for i in range(sub.get_lod_count(mesh)):
        b=sub.get_lod_build_settings(mesh,i)
        assert b.use_full_precision_u_vs and not b.generate_lightmap_u_vs
        lods.append({'lod':i,'triangles':mesh.get_num_triangles(i),'full_precision_uv':b.use_full_precision_u_vs})
    assert len(lods)==3
    task=unreal.AssetExportTask();task.object=mesh;task.filename=(OUT/'UE_RenderLODs.fbx').as_posix()
    task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=unreal.StaticMeshExporterFBX()
    options=unreal.FbxExportOption();options.ascii=False;options.collision=False;options.level_of_detail=True;task.options=options
    assert unreal.Exporter.run_asset_export_task(task)
    for row in lods:row['triangles']=mesh.get_num_triangles(row['lod'])
    fn=unreal.load_asset(FN);assert LIB.get_metadata_tag(fn,'GuLi.RigidWPO')=='v4'
    expressions=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==fn]
    custom=next(n for n in expressions if isinstance(n,unreal.MaterialExpressionCustom))
    assert custom.get_editor_property('code')==(ROOT/'Scripts/Materials/GuLiRigidMechanical.hlsl').read_text()
    indices=sorted(n.get_editor_property('data_index') for n in expressions if isinstance(n,unreal.MaterialExpressionPerInstanceCustomData))
    assert indices==list(range(1,51)),indices
    material_paths=list(dict.fromkeys(str(s.material_interface.get_path_name()) for s in mesh.static_materials))
    material_paths += ['/Game/GuLiStrike/FX/UnitFeedback/M_UnitWreckRust','/Game/GuLiStrike/FX/UnitFeedback/M_UnitHitWhite_Instanced','/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportBody']
    material_rows=[]
    for path in material_paths:
        mat=unreal.load_asset(path);assert LIB.get_metadata_tag(mat,'GuLi.RigidWPO')=='v4',path
        render_padding=mat.get_editor_property('max_world_position_offset_displacement')
        assert render_padding==2000,(path,render_padding)
        nodes=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==mat]
        assert any(isinstance(n,unreal.MaterialExpressionMaterialFunctionCall) and n.get_editor_property('material_function')==fn for n in nodes),path
        params={}
        for n in nodes:
            if isinstance(n,unreal.MaterialExpressionVectorParameter):
                v=n.get_editor_property('default_value')
                params[str(n.get_editor_property('parameter_name'))]=[v.r,v.g,v.b,v.a]
        for i in range(4):
            for kind in ('Root','End','Axis'):assert 'RigidLeg'+kind+str(i) in params,path
        graph=json.loads(unreal.MaterialNodeService.export_material_graph(mat.get_path_name()))
        graph_nodes={n['id']:n for n in graph['expressions']}
        bindings={c['target_input'].split(' ')[0]:graph_nodes[c['source_id']].get('parameter_name')
                  for c in graph['connections'] if graph_nodes[c['target_id']]['class']=='MaterialFunctionCall'}
        expected={'Kind':'RigidKind','UpperPivot':'RigidUpperPivot'}
        for i in range(4):
            for kind in ('Root','End','Axis'):expected['Leg'+kind+str(i)]='RigidLeg'+kind+str(i)
        assert all(bindings.get(k)==v for k,v in expected.items()),(path,bindings)
        if '/MechanicalAnimation/M_WarMachine_' in path:
            for i,joint in enumerate(spec['leg_joints_cm']):
                for kind,key in [('Root','root_cm'),('End','end_cm'),('Axis','axis')]:
                    assert max(abs(a-b) for a,b in zip(params['RigidLeg'+kind+str(i)][:3],joint[key]))<.01
        material_rows.append({'path':path,'version':'v4','bindings':bindings,'render_wpo_limit_cm':render_padding,
                              'leg_parameters':{k:v for k,v in params.items() if k.startswith('RigidLeg')}})
    render=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.get_missile_pod_mesh_diagnostics(mesh))
    assert all(row['nonintegral_parts']==row['mixed_pod_triangles']==0 and row['uv_channels']==3 for row in render['lods'])
    r.update(mesh=MESH,metadata=LIB.get_metadata_tag(mesh,'GuLi.Animation'),sockets=sockets,lods=lods,
        mesh_dimensions_cm=dimensions,mesh_gameplay_bounds_unchanged=True,
        material_function=FN,custom_data_indices=indices,materials=material_rows,render_buffers=render)
    if globals().get('GULI_TURN_INSPECT_SCENE',True):
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert world.get_path_name()==MAP+'.LVL_CommanderMassPrototype'
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        previews=[a for a in actors if 'WarMachineHoverPreview20260929' in [str(t) for t in a.tags]]
        assert len(previews)==1
        actor=previews[0];c=actor.get_component_by_class(unreal.InstancedStaticMeshComponent)
        assert c and c.get_editor_property('static_mesh')==mesh and c.get_instance_count()==13
        assert c.get_editor_property('num_custom_data_floats')==51
        data=list(c.get_editor_property('per_instance_sm_custom_data'));assert len(data)==13*51
        instances=[]
        for i in range(13):
            t=c.get_instance_transform(i,True)
            values=data[i*51:(i+1)*51]
            assert values[29]==values[30]==(1 if i==12 else 0)
            assert values[1:15]==values[15:29] and values[31:41]==values[41:51]
            instances.append({'index':i,'location_cm':list(t.translation.to_tuple()),'scale':list(t.scale3d.to_tuple()),'pod_visible':values[29]})
        camera=next(a for a in actors if a.get_actor_label()=='WarMachineTurn_ReviewCamera')
        note=next(a for a in actors if a.get_actor_label()=='WarMachineHover_PreviewInstructions')
        legacy=[]
        for a in actors:
            if 'MassRigidReview20260929' not in [str(t) for t in a.tags]:continue
            old=a.get_component_by_class(unreal.InstancedStaticMeshComponent)
            if not old:continue
            assert old.get_editor_property('num_custom_data_floats')==51
            assert len(old.get_editor_property('per_instance_sm_custom_data'))==old.get_instance_count()*51
            legacy.append({'label':a.get_actor_label(),'instances':old.get_instance_count(),'custom_floats':51})
        dirty=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
        assert MAP not in dirty,dirty
        r['scene']={'map':MAP,'saved':True,'actor':actor.get_path_name(),'custom_floats':51,'instances':instances,
            'note':note.get_editor_property('text'),'camera':camera.get_path_name(),
            'camera_location_cm':list(camera.get_actor_location().to_tuple()),'legacy_previews':legacy,'native_driver_loaded':False}
    r['success']=True
except Exception:
    r['error']=traceback.format_exc()
OUT.mkdir(parents=True,exist_ok=True)
name='scene-readback.json' if globals().get('GULI_TURN_INSPECT_SCENE',True) else 'ue-asset-readback.json'
(OUT/name).write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(r,ensure_ascii=False))
