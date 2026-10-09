"""Read saved source/derived graphs and parameter values without modifying assets."""
import json
from pathlib import Path
import unreal

root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
out=root/'ArtSource/LocalTeamColorReview_20261008'
edit=unreal.MaterialEditingLibrary
models=json.loads((out/'asset-authoring.json').read_text(encoding='utf8'))['models']

def nodes(owner):
    if isinstance(owner,unreal.Material): edit.get_num_material_expressions(owner)
    return [x for x in unreal.ObjectIterator(unreal.MaterialExpression) if x.get_path_name().startswith(owner.get_path_name()+':')]

def base(material):
    while isinstance(material,unreal.MaterialInstance): material=material.get_editor_property('parent')
    return material

def graph(owner):
    result=[]
    for n in nodes(owner):
        entry={'name':n.get_name(),'class':n.get_class().get_name()}
        for k in ('code','coordinate_index','constant','default_value','parameter_name','material_function','texture','sampler_type'):
            try:
                v=n.get_editor_property(k)
                entry[k]=str(v)
                if k=='texture' and v:entry['srgb']=v.get_editor_property('srgb')
                if k=='material_function' and v:entry['function_graph']=graph(v)
            except Exception:pass
        try:
            entry['links']=[str(x) for x in edit.get_inputs_for_material_expression(owner,n)]
        except Exception:pass
        result.append(entry)
    return result

report={'success':True,'models':[],'native_build_executed':False,'gameplay_started':False}
for m in models:
    entry={'name':m['name'],'slots':[]}
    for s in m['materials']:
        if s['fixed']:continue
        item={'slot':s['slot'],'source':graph(base(unreal.load_asset(s['source']))),'derived':graph(unreal.load_asset(s['derived_parent'])),'parameters':{}}
        for v in ('blue','red','regions'):
            material=unreal.load_asset(s[v])
            item['parameters'][v]={'team':str(edit.get_material_instance_vector_parameter_value(material,'ReviewTeamColor')),'mark':edit.get_material_instance_scalar_parameter_value(material,'ReviewRegionMark')}
        entry['slots'].append(item)
    report['models'].append(entry)
(out/'material-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
