"""Author the icon ID references and two small HUDuiPRO loop atlases.

Only xlsx.exe edits workbooks. PNG processing is source-sequence atlas production,
not visual QA. Run the standard exporter afterwards.
"""
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/UI/GroundMech/HUD_Revision_20261103'
XLSX = r'D:/UE5.7/excelize-cli/bin/xlsx.exe'

def cli(*args):
    return subprocess.check_output([XLSX, *map(str, args)], text=True, encoding='utf-8')

def column(n):
    result = ''
    while n:
        n, rem = divmod(n-1, 26)
        result = chr(65+rem)+result
    return result

def read(book, sheet):
    return json.loads(cli('read', book, '--sheet', sheet, '--format', 'json'))['rows']

def write(book, sheet, cells):
    with tempfile.TemporaryDirectory(prefix='guli-icons-') as directory:
        path = Path(directory)/'cells.json'
        path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
        cli('write', book, '--sheet', sheet, '--data-file', path)

def cell(col, row, value, kind='string'):
    return {'cell': f'{column(col)}{row}', 'value': value, 'type': kind}

def icon_column(filename, sheet, values):
    book=ROOT/'data/Excel'/filename
    rows=read(book,sheet)
    index=rows[0].index('IconId') if 'IconId' in rows[0] else len(rows[0])
    cells=[cell(index+1,1,'IconId'),cell(index+1,2,'int'),cell(index+1,3,'Optional')]
    for number,row in enumerate(rows[3:],4):
        if row and row[0]:
            value=values.get(row[1],int(row[index]) if len(row)>index and row[index] else 0)
            cells.append(cell(index+1,number,value,'int'))
    write(book,sheet,cells)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    icons=json.loads((ROOT/'ArtSource/UI/GroundMech/Icons/catalog.json').read_text(encoding='utf-8'))['icons']
    book=ROOT/'data/Excel/GuLiStrikeIcons.xlsx'
    if not book.exists():cli('new',book,'--sheet','Icons')
    rows=[['id','name','Note','ResourcePath'],['int','str','str','softobject'],['Necessary','Necessary','Optional','Necessary']]
    rows += [[p['id'],p['name'],p['note'],p['resource_path']] for p in icons]
    write(book,'Icons',[cell(c,r,v,'int' if r>=4 and c==1 else 'string') for r,row in enumerate(rows,1) for c,v in enumerate(row,1)])
    cli('style',book,'--sheet','Icons','--range','A1:D1','--bold','--bg','D9E1F2')
    cli('col-width',book,'--sheet','Icons','--col','D','--width','100')
    icon_column('GuLiStrikeRogueCards.xlsx','Cards',{'GroundMech_Ricochet_Lv1':2,'GroundMech_BurningBullet_Lv1':3,'GroundMech_BurningBurst_Lv1':4})
    icon_column('GuLiStrikeMech.xlsx','技能表',{'BasicMachinegun':5,'RocketJump':1,'Blink':8})
    icon_column('GuLiStrikeBuffs.xlsx','Buffs',{'Burning':3,'BurningBurst':4})
    icon_column('GuLiStrikeGroundHUD.xlsx','Elements',{})
    catalog_path=ROOT/'ArtSource/UI/HUDuiPRO/Components20261030/manifest.json'
    catalog={c['key']:c for c in json.loads(catalog_path.read_text(encoding='utf-8-sig'))['components']}
    clips=[]
    for key,source in [('AmbientHealth','Circle_Elements_04'),('AmbientMap','Circle_Elements_Bars_02')]:
        definition=catalog[source];directory=Path(definition['sequenceDirectory'])
        start=0 if (directory/'Frame_000000.png').is_file() else 1
        atlas=Image.new('RGBA',(400,1200));sources=[]
        for index,frame in enumerate(range(24,120,2)):
            path=directory/f'Frame_{frame+start:06d}.png'
            with Image.open(path) as im:
                assert im.mode=='RGBA' and im.size==(definition['width'],definition['height'])
                tile=im.convert('RGBa').resize((96,96),Image.Resampling.LANCZOS).convert('RGBA')
                x,y=index%4*100+2,index//4*100+2
                atlas.paste(tile,(x,y))
                atlas.paste(tile.crop((0,0,96,1)).resize((96,2)),(x,y-2))
                atlas.paste(tile.crop((0,95,96,96)).resize((96,2)),(x,y+96))
                atlas.paste(tile.crop((0,0,1,96)).resize((2,96)),(x-2,y))
                atlas.paste(tile.crop((95,0,96,96)).resize((2,96)),(x+96,y))
            sources.append({'frame':frame,'path':str(path.relative_to(ROOT)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
        png=OUT/f'T_GroundHUD_{key}.png';atlas.save(png)
        clips.append({'key':key,'source_component':source,'source_fps':24,'first':24,'last':118,'step':2,'frames':48,
            'columns':4,'rows':12,'frame_size':[96,96],'padding':2,'cell_size':[100,100],'atlas_size':[400,1200],
            'duration':4,'seam_seconds':.25,'opacity':.18,'rgba_bytes':400*1200*4,'png':str(png.relative_to(ROOT)),
            'asset':f'/Game/GuLiStrike/GroundMech/UI/Motion/Textures/T_GroundHUD_{key}',
            'sha256':hashlib.sha256(png.read_bytes()).hexdigest(),'source_frames':sources})
    original=json.loads((ROOT/'ArtSource/UI/GroundMech/HUD_Motion_20261102/manifest.json').read_text(encoding='utf-8'))
    total=original['rgba_bytes']+sum(c['rgba_bytes'] for c in clips)
    assert total<=64*1024**2
    manifest={'clips':clips,'rgba_mib_all_motion':total/1024**2,'budget_mib':64,'visual_acceptance':'pending_user'}
    (OUT/'loop_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    book=ROOT/'data/Excel/GuLiStrikeGroundHUD.xlsx';rows=read(book,'Elements');header=rows[0]
    lookup={r[1]:(i+4,r) for i,r in enumerate(rows[3:]) if r and r[0]}
    next_id=max(int(r[0]) for r in rows[3:] if r and r[0])+1;next_row=len(rows)+1;cells=[]
    entries=[{'name':'IconAim','IconId':6,'Note':'瞄准界面图标'}, {'name':'IconJetCapacity','IconId':1,'Note':'喷气容量图标'},
             {'name':'MotionAmbientSeconds','Value':4},{'name':'MotionAmbientSeamSeconds','Value':.25},{'name':'MotionAmbientOpacity','Value':.18}]
    for clip in clips:
        prefix='Motion'+clip['key']
        entries += [{'name':prefix+'Atlas','Texture':clip['asset']+'.'+clip['asset'].rsplit('/',1)[1],'Sprite':48,'Value':4,'Note':'HUDuiPRO循环片段图集'},
                    *[{'name':prefix+k,'Value':v} for k,v in [('Columns',4),('Rows',12),('PaddingX',.02),('PaddingY',.02)]]]
    material='/Game/GuLiStrike/GroundMech/UI/Motion/Materials/M_GroundHUD_Ambient'
    entries.append({'name':'MotionAmbientMaterial','Texture':material+'.M_GroundHUD_Ambient','Note':'循环插值与首尾淡化UI材质'})
    for entry in entries:
        name=entry['name']
        if name in lookup:number,old=lookup[name];ident=int(old[0])
        else:number=next_row;next_row+=1;ident=next_id;next_id+=1
        values={'id':ident,'name':name,'Note':'Ground HUD 表现配置','Texture':'','TextId':'','Sprite':-1,'Value':0,'ColorSRGB':'','IconId':0,**entry}
        for c,key in enumerate(header,1):
            if key in values:cells.append(cell(c,number,values[key],'int' if key in ('id','Sprite','IconId') else 'float' if key=='Value' else 'string'))
    write(book,'Elements',cells)
    print(json.dumps({'icons':len(icons),'loops':len(clips),'motion_mib':manifest['rgba_mib_all_motion']}))

if __name__=='__main__':main()
