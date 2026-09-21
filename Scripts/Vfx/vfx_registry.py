"""Authoring helper backed by exported Excel data. Contains no visual resource defaults."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def rows():
    return json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf-8'))

def definition(name_or_id):
    found=[r for r in rows() if r['Name']==name_or_id or r['Id']==name_or_id]
    if len(found)!=1:
        raise ValueError(f'Unknown or ambiguous VFX definition: {name_or_id}')
    return found[0]

def vfx_id(name):
    return definition(name)['Id']

def resource(name_or_id):
    return definition(name_or_id)['ResourcePath']

def scale(name_or_id):
    return tuple(definition(name_or_id)['Scale'][axis] for axis in 'XYZ')

def require_id(resource_path, base_scale):
    """Legacy authoring operation must explicitly register its selection before configuring it."""
    values=(base_scale,)*3 if isinstance(base_scale,(int,float)) else tuple(base_scale)
    path=resource_path if '.' in resource_path.rsplit('/',1)[-1] else resource_path+'.'+resource_path.rsplit('/',1)[-1]
    matches=[r['Id'] for r in rows() if r['ResourcePath'].casefold()==path.casefold()
             and all(abs(r['Scale'][a]-v)<=1.e-6 for a,v in zip('XYZ',values))]
    if len(matches)!=1:
        raise ValueError(f'Register resource + base scale in GuLiStrikeVfx.xlsx first: {path} {values}')
    return matches[0]

def visual_variant(name, scale_parameter='', random_yaw=False, maximum_lifetime=3.0):
    import unreal
    v=unreal.GuLiEffectVisualVariant()
    v.set_editor_property('vfx_id',vfx_id(name))
    v.set_editor_property('scale_parameter_name',scale_parameter)
    v.set_editor_property('random_yaw',random_yaw)
    v.set_editor_property('maximum_lifetime',maximum_lifetime)
    return v
