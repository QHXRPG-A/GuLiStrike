"""Transient same-camera material preview. Run in the editor after captures; never save the level."""
import json
import unreal

ROOT = 'D:/UE5.7/test1/TestResults/CommanderGpuOptimization-20260916'
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
MAPPINGS = {row['original']: row['variant'] for row in json.load(open(ROOT + '/material-variants.json'))['mappings']}
BASELINE = {'r.AntiAliasingMethod':4, 'r.VolumetricFog':1,
    'r.Shadow.Virtual.ResolutionLodBiasDirectional':-1.5, 'r.Shadow.Virtual.ResolutionLodBiasDirectionalMoving':-1.5,
    'r.Shadow.Virtual.ResolutionLodBiasLocal':0, 'r.Shadow.Virtual.ResolutionLodBiasLocalMoving':1,
    'r.Lumen.ScreenProbeGather.DownsampleFactor':16, 'r.Lumen.ScreenProbeGather.RadianceCache.ProbeResolution':32,
    'r.LumenScene.DirectLighting.UpdateFactor':32, 'r.LumenScene.Radiosity.UpdateFactor':64}
OPTIMIZED = dict(zip(BASELINE, [1,0,0,0,1,2,32,16,64,128]))


class UnitReflectionPreview:
    def __init__(self):
        assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
        self.actors = []
        self.meshes = []
        self.originals = []
        self.flags = []
        self.camera = EDITOR.get_level_viewport_camera_info()
        self.selection = EAS.get_selected_level_actors()
        self.anchor = unreal.Vector(-240000, 130000, 65000)
        self.descriptions = []

    def make(self):
        specifications = [
            ('Soldier', '/Game/Commander/Units/SM_CommanderFourFRobot_Crowd', None),
            ('WM01', '/Game/Commander/Units/SM_WM01_Crowd', None),
            ('Vehicle', None, '/Game/GuLiStrike/Vehicles/ElectromagneticMiner/BP_MiningVehicle_TransporterLvl2'),
            ('Wingman', '/Game/GuLiStrike/Wingman/SM_Wingman_Mass', None)]
        for index, (label, path, blueprint) in enumerate(specifications):
            location = self.anchor + unreal.Vector(0, (index-1.5)*10500, 0)
            if path:
                actor = EAS.spawn_actor_from_class(unreal.StaticMeshActor, location, unreal.Rotator(), transient=True)
                mesh = actor.static_mesh_component
                mesh.set_static_mesh(unreal.load_asset(path))
            else:
                cls = unreal.EditorAssetLibrary.load_blueprint_class(blueprint)
                source = unreal.get_default_object(cls).get_component_by_class(unreal.SkeletalMeshComponent)
                actor = EAS.spawn_actor_from_class(unreal.SkeletalMeshActor, location, unreal.Rotator(), transient=True)
                mesh = actor.skeletal_mesh_component
                mesh.set_skeletal_mesh_asset(source.get_skeletal_mesh_asset())
                for slot in range(source.get_num_materials()): mesh.set_material(slot, source.get_material(slot))
            actor.set_actor_label('GPUPreview_' + label)
            mesh.set_mobility(unreal.ComponentMobility.MOVABLE)
            mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            origin, extent = actor.get_actor_bounds(False)
            scale = 6500 / (2 * max(extent.x, extent.y, extent.z))
            actor.set_actor_scale3d(unreal.Vector(scale,scale,scale))
            self.actors.append(actor); self.meshes.append(mesh)
            self.originals.append([mesh.get_material(i) for i in range(mesh.get_num_materials())])
            flags = {key: True for key in ['visible_in_ray_tracing','affect_distance_field_lighting','affect_dynamic_indirect_lighting','visible_in_reflection_captures','visible_in_real_time_sky_captures']}
            if label in ['Soldier', 'WM01']:
                for key in ['visible_in_ray_tracing','affect_distance_field_lighting','affect_dynamic_indirect_lighting']: flags[key] = False
                mesh.set_cast_shadow(False)
            self.flags.append(flags)
            self.descriptions.append({'label':label,'scale':scale,'materials':[m.get_path_name() for m in self.originals[-1]]})
        EDITOR.set_level_viewport_camera_info(self.anchor + unreal.Vector(-30000,0,17000), unreal.Rotator(pitch=-25,yaw=0,roll=0))
        EAS.clear_actor_selection_set()
        open(ROOT+'/preview-layout.json','w').write(json.dumps(self.descriptions,indent=2))

    def mode(self, optimized):
        for key, value in (OPTIMIZED if optimized else BASELINE).items():
            unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), f'{key} {value}')
        for mesh, originals, flags in zip(self.meshes, self.originals, self.flags):
            for slot, original in enumerate(originals):
                path = MAPPINGS.get(original.get_path_name()) if optimized else None
                mesh.set_material(slot, unreal.load_asset(path) if path else original)
            for flag, value in flags.items(): mesh.set_editor_property(flag, False if optimized else value)

    def shot(self, label):
        unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), 'HighResShot 1920x1080 filename="'+ROOT+'/units-'+label+'.png"')

    def cleanup(self):
        self.mode(True)
        for actor in self.actors: EAS.destroy_actor(actor)
        self.actors.clear()
        EDITOR.set_level_viewport_camera_info(*self.camera)
        EAS.set_selected_level_actors(self.selection)


unit_reflection_preview = UnitReflectionPreview()
unit_reflection_preview.make()
unit_reflection_preview.mode(False)
print(json.dumps({'preview_ready': True, 'next': 'After settling, unit_reflection_preview.shot("before"); mode(True); settle; shot("after"); cleanup(). No level save.'}))
