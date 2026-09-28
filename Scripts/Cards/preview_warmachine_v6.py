"""Capture the authorized comic replacement at center, edges and 16-degree corners.
Uses the existing card Blueprint presentation in the dedicated review-map PIE.
Does not inject OS input or change the presentation graph.
"""
from pathlib import Path
import json
import unreal

source=Path('D:/UE5.7/test1/Scripts/Cards/preview_warmachine_v5.py').read_text(encoding='utf-8')
source=source.replace('Cel_Closeups_v5_Redraw/Production','Comic_Closeups_v6/Production')
source=source.replace('v5-preview.json','v6-preview.json')
source=source.replace("OUT.mkdir(exist_ok=True)","OUT.mkdir(parents=True,exist_ok=True)")
source=source.replace("rows.append({'index':i,'location':list(card.get_actor_location().to_tuple()),'rotation':str(pivot.get_editor_property('relative_rotation'))})",
"""rot=pivot.get_editor_property('relative_rotation')
        assert (location-card.get_actor_location()).length()<.001
        assert abs(rot.yaw+x*16.)<.01 and abs(rot.roll-y*16.)<.01
        rows.append({'index':i,'location':list(card.get_actor_location().to_tuple()),'rotation':str(rot),'center_fixed':True})""")
start=source.index('JOBS=[')
end=source.index('\ndef tick(',start)
source=source[:start]+"""JOBS=[lambda:pose(0,0),lambda:shot('three-cards-comic'),export_cards]
for x,y,label in [(-1,0,'left'),(1,0,'right'),(0,-1,'top'),(0,1,'bottom'),
                  (-1,-1,'top-left'),(1,-1,'top-right'),(-1,1,'bottom-left'),(1,1,'bottom-right')]:
    JOBS.append(lambda x=x,y=y:pose(x,y))
    JOBS.append(lambda label=label:shot('hover-'+label))
JOBS.append(lambda:pose(0,0))

"""+source[end:]
source=source.replace("assert D.get_editor_property('Phase')==1","assert D.get_editor_property('Phase')==1\n            assert D.get_editor_property('MaximumTilt')==16.")
exec(compile(source,'preview_warmachine_v6_live','exec'),globals())
result=json.dumps({'preview_started':True,'scope':'center, eight edge/corner poses and actual card exports'})

