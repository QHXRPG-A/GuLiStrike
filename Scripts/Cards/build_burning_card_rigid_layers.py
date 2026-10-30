"""One physical object owner per layer; complete ring and rigid motion data."""
from pathlib import Path
from datetime import datetime,timezone,timedelta
from copy import deepcopy
import json
import numpy as np
from PIL import Image
from complete_burning_card_layers import sha,rgba,bbox,fit_box,warp,CANVAS,PAD

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/UI/GroundMechRogueCards/BurningCards_HTML_20261029_v1'
REPAIR=OUT/'Repairs/RigidObjects_v4'
CHANGED={'Burst_2','Burst_3','Burst_6','Bullet_2'}
OWNERS={
 'Burst':{
  1:['free_burning_armor_shards','free_charred_mechanical_chips'],
  2:['near_gray_foreground_stones'],
  3:['complete_torso_shockwave_ring'],
  4:['central_pioneer_chassis','central_pioneer_four_legs','central_pioneer_two_guns','central_pioneer_two_antennae','central_attached_fire_and_smoke'],
  5:['left_distant_pioneer','right_distant_pioneer'],
  6:['grass_and_soil_landscape','distant_stone_terraces','distant_ruins','sky_and_clouds']},
 'Bullet':{
  1:['projectile_body_and_attached_flame'],
  2:['lower_left_free_armor_shard','bottom_free_armor_plate','upper_right_free_armor_shard','free_small_charred_chips'],
  3:['independent_torso_impact_flare'],
  4:['hit_pioneer_chassis','hit_pioneer_four_legs','hit_pioneer_two_guns','hit_pioneer_two_antennae','hit_pioneer_embedded_embers'],
  5:['distant_blue_shooter','shooter_attached_muzzle_flash_and_trajectory'],
  6:['grass_and_stone_landscape','distant_ruins','sky_and_clouds']},
}


def ring_fit(raw):
    alpha=raw[:,:,3]
    edge=max(int(alpha[0,:].max()),int(alpha[-1,:].max()),int(alpha[:,0].max()),int(alpha[:,-1].max()))
    assert edge<32,'Ring has strong opaque content clipped by the raw-image border; regenerate it, do not erase the border'
    box=bbox(raw);target=np.array([PAD-190,PAD+190,PAD+1210,PAD+735],float)
    scales=(target[2:]-target[:2])/(box[2:]-box[:2])
    matrix=np.column_stack([np.diag(scales),target[:2]-scales*box[:2]])
    painted=warp(raw,matrix)
    return painted,{'method':'complete ellipse registered to torso plane with overscan','matrix':matrix.tolist(),
        'raw_border_alpha_max':edge,'painted_canvas_bounds':bbox(painted).tolist(),
        'source_window_ring_bounds':[-190,190,1210,735],
        'left_right_turns':'inside full texture, outside the visible card window',
        'texture_crop_mask':False,'independent_ring_parts':False}


def main():
    build=deepcopy(json.loads((REPAIR/'previous-build.json').read_text(encoding='utf-8')))
    requests=json.loads((REPAIR/'requests.json').read_text(encoding='utf-8'))
    results=json.loads((REPAIR/'results.json').read_text(encoding='utf-8'))
    active={r['id']:r for r in results if r.get('status')=='active'}
    assert len(active)==12 and len(requests)==12
    request_map={r['id']:r for r in requests}
    build.update(revision='rigid-objects-v4',created_at=datetime.now(timezone(timedelta(hours=8))).isoformat(),
        new_imagegen_edits=4,retained_painted_layers=8,user_art_review='pending',assistant_visual_test=False)
    build['assembly_policy'].update(motion_model='rigid-translation',spatial_uv_warp=False,
        physical_object_owner='one content layer per object; declared registry, visual confirmation pending',
        ring='one complete texture; one occlusion-composite draw, no split layer or baked cut mask')
    staged=[]
    for key,card in build['cards'].items():
        seen=set()
        for layer in card['layers']:
            n=layer['index'];identifier=f'{key}_{n}';req=request_map[identifier];record=active[identifier]
            assert sha(Path(record['output']))==record['sha256']
            if identifier in CHANGED:
                raw=rgba(record['output'])
                if identifier=='Burst_3':painted,registration=ring_fit(raw)
                elif n==6:
                    painted=np.asarray(Image.fromarray(raw).resize((CANVAS,CANVAS),Image.Resampling.LANCZOS)).copy()
                    assert np.all(painted[:,:,3]==255)
                    registration={'method':'complete cleaned environment, common full-canvas normalization',
                        'removed_foreground_objects':'near gray stones; retained exclusively in layer 2'}
                else:
                    target=[-130,820,1130,1220] if identifier=='Burst_2' else [-110,-80,1130,1120]
                    matrix=fit_box(raw,target);painted=warp(raw,matrix)
                    registration={'method':'complete separate objects, shared-canvas registration','matrix':matrix.tolist(),
                        'exclusions':'no connected central robot parts; no other layer object copies'}
                temporary=REPAIR/f'{identifier}_Assembled.png';Image.fromarray(painted).save(temporary)
                staged.append((temporary,OUT/layer['file']))
                layer.update(sha256=sha(temporary),alpha_extrema=list(Image.fromarray(painted).getchannel('A').getextrema()),
                    raw_generated_source=record['source'],raw_generated_copy=Path(record['output']).relative_to(OUT).as_posix(),
                    raw_size=record['size'],raw_alpha=record['alpha_extrema'],raw_sha256=record['sha256'],
                    generated_fill_registration=registration,prompt_file=req['prompt_file'])
            else:assert sha(OUT/layer['file'])==layer['sha256'],'Unchanged layer was modified externally'
            owned=OWNERS[key][n];assert not seen.intersection(owned),'Physical object assigned to multiple layers'
            seen.update(owned);layer['object_ids']=owned
            layer['motion_owner_layer']=4 if key=='Bullet' and n==3 else n
        card['object_ownership']={'rule':'one physical object has one owning texture layer',
            'registry':{str(n):ids for n,ids in OWNERS[key].items()},'duplicate_declared_ids':[],
            'visual_content_uniqueness':'pending_user_review'}
        card['motion_model']='rigid-translation'
        card['passes']=([{'layer':6},{'layer':5},{'layer':3,'host_layer':4,'occlusion_plane':[.13,393.7]},
            {'layer':2},{'layer':1}] if key=='Burst' else [{'layer':n} for n in [6,5,4,3,2,1]])
        consumed=[n for p in card['passes'] for n in ([p['layer'],p['host_layer']] if p.get('host_layer') else [p['layer']])]
        assert sorted(consumed)==list(range(1,7))
        card['note']=('完整冲击圈；每个物件只归属一层，整体平移。' if key=='Burst' else
            '燃烧弹体与附着火焰同层；命中火焰跟随受击机甲整体移动。')
        card['source_contract'].update(physical_object_assignment='single-owner registry',spatial_uv_warp=False,
            hidden_surface_appearance='pending_user_review')
    assert len(staged)==4
    for temporary,destination in staged:temporary.replace(destination)
    (OUT/'build.json').write_text(json.dumps(build,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (OUT/'generation-requests.json').write_text(json.dumps(requests,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    previous=json.loads((REPAIR/'previous-generation-results.json').read_text(encoding='utf-8'))
    for record in previous:
        if record.get('status')=='active':record['status']='superseded_by_rigid_objects_v4_snapshot'
    (OUT/'generation-results.json').write_text(json.dumps(previous+results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'revision':build['revision'],'new_painted_layers':4,'retained_layers':8,
        'motion_model':'rigid-translation','ring_texture_parts':1,'baked_ring_mask':False,
        'declared_duplicate_object_owners':0,'user_visual_review':'pending'},ensure_ascii=False))


if __name__=='__main__':main()
