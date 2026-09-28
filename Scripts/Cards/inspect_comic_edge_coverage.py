"""Read the current comic card components and material graphs before repair."""
import json
from pathlib import Path
import unreal

OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Comic_Closeups_v6/Production/EdgeCoverageFix')
OUT.mkdir(parents=True,exist_ok=True)
ROOT='/Game/GuLiStrike/Cards/WarMachineTarot/Materials/'
r={'pie':unreal.WidgetService.is_pie_running(),'cards':[]}
if not r['pie']:
    for name in ['M_CelCardParallax_Comic_v6','M_CelCardUI_Comic_v6']:
        (OUT/(name+'-before.json')).write_text(unreal.MaterialNodeService.export_material_graph(ROOT+name),encoding='utf-8')
players=[p for p in unreal.ObjectIterator(unreal.PlayerController) if not p.get_name().startswith('Default__') and p.get_viewport_size()[0]>0]
if players:
    p=players[0]
    assert 'LVL_WarMachineTarotReview' in p.get_world().get_path_name()
    d=p.get_editor_property('Director')
    r.update(viewport=list(p.get_viewport_size()),phase=d.get_editor_property('Phase'))
    for i in range(3):
        card=d.get_editor_property('Card'+str(i))
        if not card:continue
        row={'index':i,'path':card.get_path_name(),'components':[]}
        for c in card.get_components_by_class(unreal.SceneComponent):
            cr={'name':c.get_name(),'location':list(c.get_editor_property('relative_location').to_tuple()),'rotation':str(c.get_editor_property('relative_rotation')),'scale':list(c.get_editor_property('relative_scale3d').to_tuple())}
            if isinstance(c,unreal.StaticMeshComponent):
                cr['mesh']=c.static_mesh.get_path_name() if c.static_mesh else None
                cr['materials']=[m.get_path_name() if m else None for m in c.get_materials()]
            row['components'].append(cr)
        r['cards'].append(row)
    unreal.SystemLibrary.execute_console_command(p.get_world(),'Shot showui filename="'+str(OUT/'user-session-before.png')+'" -nosuffix',p)
(OUT/('inspection-before.json' if r['pie'] else 'editor-before.json')).write_text(json.dumps(r,indent=2),encoding='utf-8')
print(json.dumps(r))
