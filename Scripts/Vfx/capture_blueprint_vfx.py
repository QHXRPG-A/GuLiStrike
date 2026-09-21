"""Read project Blueprint graph/component defaults without modifying assets."""
import json
from pathlib import Path
import unreal
root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
out=root/'outputs/vfx-registry-20260921'
baseline=json.loads((out/'asset-baseline.json').read_text(encoding='utf-8'))
records=[]
for item in baseline['assets']:
    if item['class']!='Blueprint' or '/Rollback' in item['path'] or '/Demo/' in item['path'] or '/Review/' in item['path']:
        continue
    p=item['path']
    record={'path':p,'variables':[], 'components':[], 'graphs':[]}
    record['variables']=[v.export_text() for v in unreal.BlueprintService.list_variables(p)]
    for comp in unreal.BlueprintService.list_components(p):
        name=comp.get_editor_property('component_name')
        record['components'].append({'info':comp.export_text(),'properties':[v.export_text() for v in unreal.BlueprintService.get_all_component_properties(p,name,True)]})
    for graph in unreal.BlueprintService.list_graphs(p):
        name=graph.get_editor_property('graph_name')
        record['graphs'].append({'name':str(name),'nodes':[n.export_text() for n in unreal.BlueprintService.get_nodes_in_graph(p,name)]})
    records.append(record)
(out/'blueprint-baseline.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
print('Blueprints inspected: '+str(len(records)))
