"""Actual engine before/after ore renders in transient editor fixtures, no mesh edits."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008/Images/OreRockReview'
OUT.mkdir(parents=True, exist_ok=True)


def run():
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    rows = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8'))
    models = [next(r for r in rows if r['Id'] == mid) for mid in (7003,7015)]
    actors = []
    camera = editor.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0,0,30000), transient=True)
    parents = [unreal.load_asset('/Game/GuLiStrike/Resources/Ores/Materials/M_Ore_' + side + '_Rock') for side in ('Blue','Red')]
    amounts = [next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionConstant)
                    if e.get_outer() == m and e.get_editor_property('desc') == 'GuLi.OreRockFill.v1.FillAmount') for m in parents]
    originals = [e.get_editor_property('r') for e in amounts]
    crystal_parents = [unreal.load_asset('/Game/GuLiStrike/Resources/Ores/Materials/M_Ore_' + side + '_Crystal') for side in ('Blue','Red')]
    customs = [next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom)
                    if e.get_outer() == m and e.get_editor_property('desc') == 'GuLi.OreCrystalFill.v1.ReadableDarkSides') for m in crystal_parents]
    crystal_codes = [e.get_editor_property('code') for e in customs]
    files = []
    try:
        capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
        if capture.get_owner() != camera: raise RuntimeError('Capture component must belong to the transient actor')
        capture.capture_every_frame = False
        capture.capture_on_movement = False
        capture.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        capture.projection_type = unreal.CameraProjectionMode.ORTHOGRAPHIC
        capture.primitive_render_mode = unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST
        target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(.1,.13,.15,1), False, False)
        target.target_gamma = 2.2
        capture.texture_target = target
        for index, model in enumerate(models):
            actor = editor.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(index*6000,0,30000), transient=True)
            actor.static_mesh_component.set_static_mesh(unreal.load_object(None, model['ResourcePath']))
            actors.append(actor)
        for revision, value in [('before',0),('after',.85)]:
            for material, amount in zip(parents,amounts):
                amount.set_editor_property('r', value)
                unreal.MaterialEditingLibrary.recompile_material(material)
            for material, custom, code in zip(crystal_parents,customs,crystal_codes):
                custom.set_editor_property('code', 'return OriginalEmission.rgb;' if revision == 'before' else code)
                unreal.MaterialEditingLibrary.recompile_material(material)
            for side, actor in zip(('blue','red'),actors):
                center, extent = actor.get_actor_bounds(False, True)
                radius = max(extent.x,extent.y,extent.z,1)
                position = center + unreal.Vector(1,1,.8)*radius*5
                camera.set_actor_location_and_rotation(position,unreal.MathLibrary.find_look_at_rotation(position,center),False,True)
                capture.ortho_width = radius*3.1
                capture.clear_show_only_components()
                for component in actor.get_components_by_class(unreal.MeshComponent):
                    capture.show_only_component(component)
                capture.capture_scene()
                filename = side + '_' + revision + '.png'
                unreal.RenderingLibrary.export_render_target(world,target,str(OUT),filename)
                files.append(str(OUT/filename))
    finally:
        for material, amount, value in zip(parents, amounts, originals):
            amount.set_editor_property('r',value)
            unreal.MaterialEditingLibrary.recompile_material(material)
            unreal.EditorAssetLibrary.save_loaded_asset(material,False)
        for material, custom, code in zip(crystal_parents,customs,crystal_codes):
            custom.set_editor_property('code',code)
            unreal.MaterialEditingLibrary.recompile_material(material)
            unreal.EditorAssetLibrary.save_loaded_asset(material,False)
        for actor in actors: editor.destroy_actor(actor)
        editor.destroy_actor(camera)
    report = {'success':len(files)==4,'files':files,'models':[r['Id'] for r in models],
              'source':'actual UE source ore meshes, same camera and lighting, gamma 2.2',
              'geometry_unchanged':True,'shader_restored_and_saved':True}
    (ROOT/'Artifacts/ModelRegistryRuntime20261008/ore-rock-comparison.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return report


unreal.MCPythonHelper.submit_result(json.dumps(run()))
