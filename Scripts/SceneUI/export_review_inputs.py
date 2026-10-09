"""Read presentation templates and export source color textures for palette analysis only."""
import json
import traceback
import runpy
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT/'ArtSource/LocalTeamColorReview_20261008'
runpy.run_path(str(ROOT/'Scripts/SceneUI/inspect_review_sources.py'))
EDIT = unreal.MaterialEditingLibrary
report = {'success': False, 'textures': [], 'factory_components': [], 'gameplay_started': False}
try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None
    models = json.loads((OUT/'source-readback.json').read_text(encoding='utf8'))['models']
    textures = set()
    for model in models[:12]:
        for slot in model['slots']:
            info = slot['material']
            if not info:
                continue
            for expression in info['expressions']:
                path = expression.get('texture', '')
                if 'BaseColor' in path:
                    textures.add(path)
            mat = unreal.load_asset(info['path'])
            if isinstance(mat, unreal.MaterialInstanceConstant):
                value = EDIT.get_material_instance_texture_parameter_value(mat, 'DiffuseColorMap')
                if value and value.get_path_name().startswith('/Game/'):
                    textures.add(value.get_path_name())
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    economy = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
    factory_class = economy.get_editor_property('factory_presentation_class')
    path = factory_class.get_path_name()
    report['factory_class'] = path
    actor = api.spawn_actor_from_class(factory_class, unreal.Vector(0,0,10000), transient=True)
    try:
        for component in actor.get_components_by_class(unreal.MeshComponent):
            mesh = component.static_mesh if isinstance(component, unreal.StaticMeshComponent) else component.skeletal_mesh
            info = {'name': component.get_name(), 'mesh': mesh.get_path_name(),
                    'transform': str(component.get_relative_transform()), 'materials': []}
            for i in range(component.get_num_materials()):
                material = component.get_material(i)
                info['materials'].append(material.get_path_name() if material else None)
                parent = material
                while isinstance(parent, unreal.MaterialInstance):
                    for param in parent.get_editor_property('texture_parameter_values'):
                        value = param.get_editor_property('parameter_value')
                        if value and ('BaseColor' in value.get_name() or 'base_color' in value.get_name().lower()):
                            textures.add(value.get_path_name())
                    parent = parent.get_editor_property('parent')
                if parent:
                    for node in unreal.ObjectIterator(unreal.MaterialExpression):
                        if node.get_outer() == parent and isinstance(node, unreal.MaterialExpressionTextureBase):
                            value = node.get_editor_property('texture')
                            if value and ('BaseColor' in value.get_name() or 'base_color' in value.get_name().lower()):
                                textures.add(value.get_path_name())
            report['factory_components'].append(info)
    finally:
        api.destroy_actor(actor)
    (OUT/'SourceTextures').mkdir(exist_ok=True)
    for path in sorted(textures):
        texture = unreal.load_asset(path)
        filename = OUT/'SourceTextures'/(texture.get_name()+'.png')
        task = unreal.AssetExportTask()
        task.set_editor_property('object', texture)
        task.set_editor_property('filename', str(filename))
        task.set_editor_property('automated', True)
        task.set_editor_property('prompt', False)
        task.set_editor_property('replace_identical', True)
        task.set_editor_property('exporter', unreal.TextureExporterPNG())
        assert unreal.Exporter.run_asset_export_task(task), path
        report['textures'].append({'asset': path, 'file': str(filename)})
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
(OUT/'input-export.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.log(str({'success':report['success'],'error':report.get('error')}))
