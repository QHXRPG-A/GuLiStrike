"""Client-only icon catalog and foreign-key checks, before any export writes."""
import re

def validate(tables, error_type):
    templates={r['Id']:r for r in tables.get('DT_GuLiStrikeCardArtwork_Templates',{}).get('rows',[])}
    blank_materials={r['Material'] for r in tables.get('DT_GuLiStrikeCardArtwork_Artworks',{}).get('rows',[])
                     if templates.get(r['TemplateId'],{}).get('CompositePreset')=='BlankPlaceholder'}
    entry=tables.get('DT_GuLiStrikeIcons_Icons')
    if not entry:raise error_type('缺少统一图标表 GuLiStrikeIcons.xlsx / Icons')
    ids=set()
    for row in entry['rows']:
        ident=row['Id'];path=row.get('ResourcePath','')
        if type(ident) is not int or not 0<ident<=2147483647 or ident in ids:
            raise error_type('图标 ID 必须为唯一正整数: '+str(ident))
        if not re.fullmatch(r'/Game/[A-Za-z0-9_/]+\.[A-Za-z0-9_]+',path):
            raise error_type(f'Icons/{row["Name"]}: ResourcePath 必须为完整资源路径')
        ids.add(ident)
    for name,table in tables.items():
        for row in table['rows']:
            if 'IconId' in row:
                value=row['IconId']
                if type(value) is not int or value<0 or value and value not in ids:
                    raise error_type(f'{name}/{row["Name"]}: 悬空或非法 IconId {value}')
                required=(name=='DT_GuLiStrikeMech_Skills' or name=='DT_GuLiStrikeBuffs_Buffs'
                    or name=='DT_GuLiStrikeRogueCards_Cards' and row['Name'].startswith('GroundMech_')
                    or name=='DT_GuLiStrikeGroundHUD_Elements' and row['Name'] in ('IconAim','IconJetCapacity'))
                if name=='DT_GuLiStrikeRogueCards_Cards' and row.get('FrontMaterial') in blank_materials:required=False
                if required and not value:raise error_type(f'{name}/{row["Name"]}: 必须配置 IconId')
