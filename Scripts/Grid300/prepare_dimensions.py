"""Produce an immutable-value migration recipe from live UE collision and definition data."""
import hashlib
import json
import math
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'ArtSource/Navigation/Grid300/Baseline'
OUT=ROOT/'ArtSource/Navigation/Grid300/dimensions.json'

def dimension(d):
    assert math.isfinite(d) and d>0
    n=max(1,math.floor(d/300+1.e-6))
    return {'source_diameter_cm':d,'footprint_cells':n,'target_diameter_cm':300*n,'scale':300*n/d}

def vector(value):
    if isinstance(value,dict):return [value[a] for a in ('X','Y','Z')]
    found=dict(re.findall(r'([XYZ])=([-+0-9.eE]+)',value))
    return [float(found[a]) for a in ('X','Y','Z')]

def main():
    definitions=json.loads((BASE/'definitions.json').read_text(encoding='utf-8'))
    editor=json.loads((BASE/'editor.json').read_text(encoding='utf-8'))
    player=json.loads((BASE/'player_dimensions.json').read_text(encoding='utf-8'))
    units=[]
    for row in definitions['tables']['DT_GuLiStrikeCommander_Soldiers']['rows']:
        key=str(row['Id'])
        if key in definitions['capsules']:diameter=definitions['capsules'][key]['radius_cm']*2
        elif row.get('MassAvoidanceRadiusMeters',0)>0:diameter=row['MassAvoidanceRadiusMeters']*200
        elif row['Id'] in (1,2):diameter=row['ModelWidthMeters']*100
        else:diameter=300
        d=dimension(diameter)
        units.append({'id':row['Id'],'name':row['Name'],**d,'source_presentation_scale':row['PresentationScale'],
                      'target_presentation_scale':row['PresentationScale']*d['scale']})
    buildings=[]
    factory=editor['classes']['GuLiResourceFactoryActor']['components']['FactoryCollision']['box_extent']
    for row in definitions['tables']['DT_GuLiStrikeBuildings_Buildings']['rows']:
        extent=factory if row['Name']=='ResourceFactory' else vector(row['CollisionExtent'])
        d=dimension(math.hypot(*extent[:2])*2)
        buildings.append({'id':row['Id'],'name':row['Name'],**d,'source_collision_extent_cm':extent,
                          'target_collision_extent_cm':[v*d['scale'] for v in extent]})
    resource=editor['assets']['/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap']
    clusters=[{'cluster_id':identity['cluster_id'],**dimension(c['obstacle_radius_centimeters']*2),
               'node_start':identity['first_node_index'],'node_count':c['node_count']}
              for c,identity in zip(resource['clusters'],definitions['clusters'])]
    manifest={'schema':'guli-grid300/dimensions-v1','cell_cm':300,
              'application':'Resolve dimensions from source values once per catalog load; never multiply a live instance scale repeatedly.',
              'source_hashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (BASE/'editor.json',BASE/'definitions.json')},
              'units':units,'buildings':buildings,'clusters':clusters,
              'ground_player':{'source':player,**dimension(460.)},
              'node_identities':definitions['nodes'],'initial_ore_total':sum(n['initial_amount'] for n in resource['nodes'])}
    OUT.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'units':units,'buildings':buildings,'clusters':len(clusters),
                      'nodes':len(definitions['nodes']),'initial_ore_total':manifest['initial_ore_total']},ensure_ascii=False,indent=2))

if __name__=='__main__':main()
