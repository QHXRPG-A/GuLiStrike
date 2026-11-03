"""Structured local PIE readback; never captures or evaluates rendered images."""
import json
from pathlib import Path
import unreal

root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
out=root/'data/GroundHUDValidation/Reference20261102'
action=globals().get('GROUND_HUD_CHECK','read')
report_name=globals().get('GROUND_HUD_REPORT',action)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world,'Ground PIE must already be open'
pc=unreal.GameplayStatics.get_player_controller(world,0)
widgets=list(unreal.WidgetLibrary.get_all_widgets_of_class(world,unreal.GuLiGroundHUDWidget,False))
assert len(widgets)==1,f'Expected one Ground HUD, got {len(widgets)}'
hud=widgets[0]
if action in ('map','close','details'):
    hud.call_method({'map':'MapClicked','close':'CloseClicked','details':'DetailsClicked'}[action])
elif action in ('cards','cancel'):
    assert unreal.GuLiRogueCardQALibrary.action(pc,'Open' if action=='cards' else 'Cancel',0)
elif action!='read':raise ValueError(action)

prefix=hud.get_path_name()
items=[]
for widget in unreal.ObjectIterator(unreal.Widget):
    if not widget.get_path_name().startswith(prefix):continue
    item={'name':widget.get_name(),'class':widget.get_class().get_name(),'visibility':str(widget.get_visibility())}
    if isinstance(widget,unreal.TextBlock):item['text']=str(widget.get_text());item['font_size']=widget.get_editor_property('font').size
    if isinstance(widget,unreal.ProgressBar):item['percent']=widget.get_editor_property('percent')
    if isinstance(widget,unreal.Image):
        brush=widget.get_editor_property('brush');res=brush.get_editor_property('resource_object');item['image']=res.get_path_name() if res else None
    items.append(item)
cards=[]
card_class=unreal.load_class(None,'/Game/GuLiStrike/CardSystem/RevealDemo/Blueprints/BP_ParallaxRevealCard.BP_ParallaxRevealCard_C')
for card in unreal.GameplayStatics.get_all_actors_of_class(world,card_class):
    text=next((c for c in card.get_components_by_class(unreal.WidgetComponent) if c.get_name()=='EditableText'),None)
    widget=text.get_user_widget_object() if text else None
    cards.append({'name':card.get_name(),'text_widget':widget.get_class().get_name() if widget else None,
        'draw_size':str(text.get_draw_size()) if text else None,'text_location':str(text.get_editor_property('relative_location')) if text else None})
directors=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.load_class(None,'/Game/GuLiStrike/CardSystem/RevealDemo/Blueprints/BP_CardRevealDirector.BP_CardRevealDirector_C'))
director={}
if directors:
    director={key:directors[0].get_editor_property(key) for key in ['DirectSelection','ActiveCardCount','CardScale','GroundFitWidth','GroundFitHeight','GroundCardHeight','GroundCardSpacing','ViewWidth','ViewHeight']}
report={'action':action,'session':json.loads(unreal.get_editor_subsystem(unreal.GuLiLocalPIESubsystem).get_session_status()),
    'hud_count':len(widgets),'widget_count':len(items),'widgets':items,'rogue':json.loads(unreal.GuLiRogueCardQALibrary.snapshot(pc)),'card_actors':cards}
report['director']=director
out.mkdir(parents=True,exist_ok=True);(out/f'{report_name}-widgets.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'action':action,'hud_count':len(widgets),'widget_count':len(items),
    'native_size':[report['session']['ground_hud_width'],report['session']['ground_hud_height']],
    'visible':report['session']['ground_hud_visible'],'rogue':report['rogue'],'card_actors':cards,'director':director}))
