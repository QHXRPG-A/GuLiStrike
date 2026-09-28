"""Create localizable card text data, world UMG and scoped Blueprint interfaces."""
import json
import sys
import traceback
from pathlib import Path
import unreal

sys.path.insert(0,'D:/UE5.7/test1/Scripts/Cards')
from card_reveal_graph import B,Graph,Node,require,variable,function,compile_save
ROOT='/Game/GuLiStrike/CardSystem/WarMachineTarot'
DEMO='/Game/GuLiStrike/CardSystem/RevealDemo'
CARD=DEMO+'/Blueprints/BP_ParallaxRevealCard'
DIRECTOR=DEMO+'/Blueprints/BP_CardRevealDirector'
WIDGET=ROOT+'/UI/WBP_CardText'
TABLE=ROOT+'/Data/DT_CardText'
OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards')
REPORT={'success':False}


def configure_data():
    structpath=ROOT+'/Data/FCardTextRow'
    if not unreal.EnumStructService.struct_exists(structpath):
        require(unreal.EnumStructService.create_struct(ROOT+'/Data','FCardTextRow'),'Create row struct')
        for n in ['Title','Description']:require(unreal.EnumStructService.add_struct_property(structpath,n,'FText'),n)
    struct=unreal.load_asset(structpath)
    require(unreal.EditorAssetLibrary.save_loaded_asset(struct,False),'Save struct')
    if not unreal.DataTableService.data_table_exists(TABLE):
        require(unreal.DataTableService.create_data_table(struct.get_path_name(),ROOT+'/Data','DT_CardText'),'Create table')
    table=unreal.load_asset(TABLE)
    require(unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(table,(OUT/'Data'/'CardText.csv').read_text(encoding='utf-8-sig'),struct),'Import text CSV')
    require(unreal.EditorAssetLibrary.save_loaded_asset(table,False),'Save table')
    (OUT/'Data'/'CardText_UE_Export.csv').write_text(unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(table),encoding='utf-8-sig')
    REPORT['table']=str(unreal.DataTableService.get_info(TABLE))


def configure_widget():
    W=unreal.WidgetService
    if not B.blueprint_exists(WIDGET):require(B.create_blueprint('WBP_CardText','UserWidget',ROOT+'/UI'),'Create widget')
    for kind,name,parent in [('CanvasPanel','Canvas',''),('ScaleBox','TitleFit','Canvas'),('TextBlock','CardTitle','TitleFit'),
                             ('ScaleBox','DescriptionFit','Canvas'),('TextBlock','CardDescription','DescriptionFit')]:
        if not W.widget_exists(WIDGET,name):require(W.add_component(WIDGET,kind,name,parent,True).success,name)
    for name,y,height in [('TitleFit',16,105),('DescriptionFit',135,136)]:
        for key,value in {'Anchor Min X':.5,'Anchor Max X':.5,'Anchor Min Y':0,'Anchor Max Y':0,
                          'Alignment X':.5,'Alignment Y':0,'Position X':0,'Position Y':y,'Size X':928,'Size Y':height}.items():
            require(W.set_property(WIDGET,name,key,str(value)),name+' '+key)
        require(W.set_property(WIDGET,name,'Stretch','ScaleToFit'),name+' fit')
        require(W.set_property(WIDGET,name,'StretchDirection','DownOnly'),name+' down only')
    for name,size in [('CardTitle',74),('CardDescription',38)]:
        require(W.set_property(WIDGET,name,'Text',''),name+' blank')
        require(W.set_property(WIDGET,name,'Justification','Center'),name+' center')
        require(W.set_property(WIDGET,name,'Visibility','HitTestInvisible'),name+' input')
        require(W.set_font(WIDGET,name,unreal.WidgetFontInfo(size=size,color='(R=0.96,G=0.96,B=0.91,A=1)')),name+' font')
    # Explicit wrap width avoids ScaleBox/auto-wrap feeding a shrinking width back
    # into layout on long translations. WrapTextAt still performs word wrapping.
    require(W.set_property(WIDGET,'CardDescription','AutoWrapText','false'),'Fixed description wrapping')
    require(W.set_property(WIDGET,'CardDescription','WrapTextAt','910'),'Description width')
    require(W.set_property(WIDGET,'Canvas','Visibility','HitTestInvisible'),'Canvas input')
    require(B.set_property(WIDGET,'bIsFocusable','false'),'No widget keyboard focus')
    function(WIDGET,'SetContent',[('Title','FText'),('Description','FText')])
    function(WIDGET,'SetFromRow',[('TextTable','UDataTable'),('CardId','FName')])
    variable(WIDGET,'CurrentTextRow','FCardTextRow')
    compile_save(WIDGET)
    g=Graph(WIDGET,'SetContent',True)
    for comp,arg in [('CardTitle','Title'),('CardDescription','Description')]:
        g.method('TextBlock','SetText',g.get(comp),InText=g.arg(arg))
    g.layout()
    g=Graph(WIDGET,'SetFromRow',True)
    row=Node(g,B.create_node_by_key(WIDGET,g.name,'NODE K2Node_GetDataTableRow',*g.pos()))
    row.put('DataTable',TABLE)
    row.put('RowName',g.arg('CardId'))
    row.put('DataTable',g.arg('TextTable'))
    row.put('execute',g.tail)
    g.tail=row['then']
    g.set('CurrentTextRow',row['ReturnValue'])
    data=Node(g,B.add_get_variable_node(WIDGET,g.name,'CurrentTextRow',*g.pos()))
    require(B.split_pin(WIDGET,g.name,data.guid,'CurrentTextRow'),'Split FText row')
    data.pins={p.pin_name:p for p in B.get_node_pins(WIDGET,g.name,data.guid)}
    title=next(n for n in data.pins if 'Title' in n)
    description=next(n for n in data.pins if 'Description' in n)
    g.invoke('SetContent',Title=data[title],Description=data[description])
    g.tail=row['RowNotFound']
    g.invoke('SetContent',Title='',Description='')
    g.layout()
    REPORT['widget_compile']=compile_save(WIDGET)


def configure_component():
    if not B.component_exists(CARD,'EditableText'):
        bp=unreal.load_asset(CARD)
        sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        lib=unreal.SubobjectDataBlueprintFunctionLibrary
        handles=sub.k2_gather_subobject_data_for_blueprint(bp)
        parent=next(h for h in handles if str(lib.get_variable_name(lib.get_data(h)))=='ArtworkCenter')
        h,reason=sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=parent,new_class=unreal.WidgetComponent,blueprint_context=bp))
        require(lib.is_handle_valid(h),str(reason));require(sub.rename_subobject(h,unreal.Text('EditableText')),'Name component')
    for name,value in {'Space':'World','DrawSize':'(X=1024,Y=320)','Pivot':'(X=0.5,Y=0.5)',
        'bReceiveHardwareInput':'false','bWindowFocusable':'false','bIsTwoSided':'false','bDrawAtDesiredSize':'false',
        'RelativeLocation':'(X=0,Y=0.23,Z=-18.15)','RelativeRotation':'(Pitch=0,Yaw=90,Roll=0)',
        'RelativeScale3D':'(X=0.031421,Y=0.031421,Z=0.031421)','bVisible':'false','CastShadow':'false'}.items():
        require(B.set_component_property(CARD,'EditableText',name,value),'EditableText '+name)
    require(B.set_collision_settings(CARD,'EditableText','NoCollision','WorldDynamic','NoCollision',{}),'Text collision')
    function(CARD,'SetEditableText',[('TextTable','UDataTable'),('CardId','FName')])
    compile_save(CARD)
    g=Graph(CARD,'SetEditableText',True)
    valid=g.call('KismetSystemLibrary','IsValid',Object=g.arg('TextTable'))['ReturnValue']
    g.method('SceneComponent','SetVisibility',g.get('EditableText'),bNewVisibility=valid,bPropagateToChildren=False)
    g.branch(valid)
    pc=g.call('GameplayStatics','GetPlayerController',PlayerIndex=0)['ReturnValue']
    widget=g.call('WidgetBlueprintLibrary','Create',WidgetType=WIDGET+'.WBP_CardText_C',OwningPlayer=pc)['ReturnValue']
    text=g.cast(WIDGET,widget)
    g.method(WIDGET,'SetFromRow',text,TextTable=g.arg('TextTable'),CardId=g.arg('CardId'))
    g.method('WidgetComponent','SetWidget',g.get('EditableText'),Widget=text)
    g.layout()
    REPORT['card_compile']=compile_save(CARD)


try:
    require(not unreal.WidgetService.is_pie_running(),'Finish owned preview before editing')
    configure_data()
    configure_widget()
    configure_component()
    variable(DIRECTOR,'CardTextDataTable','UDataTable','None',True,'Card Artwork')
    if not B.variable_exists(DIRECTOR,'CardTextRowNames'):
        require(B.add_variable(DIRECTOR,'CardTextRowNames','FName','(None,None,None)',True,'Array'),'Row names')
    require(B.modify_variable(DIRECTOR,'CardTextRowNames',new_category='Card Artwork',set_instance_editable=1),'Editable row names')
    import importlib,card_artwork_config
    importlib.reload(card_artwork_config)
    card_artwork_config.write_start_function()
    REPORT['director_compile']=compile_save(DIRECTOR)
    REPORT['success']=True
except Exception:REPORT['error']=traceback.format_exc()
(OUT/'Inspection'/'editable-text.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(REPORT,ensure_ascii=False))
