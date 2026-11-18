"""Register the approved r4 six-layer artwork using excelize, without gameplay edits."""
import hashlib
import json
import math
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/UI/GroundMechRogueCards/EnhancedExplosive_Card_Production_20261117_v1'
OUT = ROOT / 'Artifacts/CardArtworkPipeline/EnhancedExplosive_20261117'
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
BOOK = 'GuLiStrikeCardArtwork.xlsx'
ART_ID, TEMPLATE_ID = 14, 7
ASSET = '/Game/GuLiStrike/Cards/GroundMech/ExplosiveEnhancement'
NAME = 'GroundMech_ExplosiveEnhancement'
MATERIAL = ASSET + '/Materials/MI_' + NAME + '_Artwork'
PARENT = '/Game/GuLiStrike/CardSystem/RevealDemo/Materials/M_CardArtwork_RegisteredSixProjectileFront_v1'


def read(book, sheet):
    return json.loads(subprocess.check_output([CLI, 'read', str(ROOT/'data/Excel'/book), '--sheet', sheet, '--format', 'json'], encoding='utf-8'))['rows']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(name, value):
    (OUT/name).write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def column(number):
    value = ''
    while number:
        number, remainder = divmod(number-1, 26)
        value = chr(65+remainder)+value
    return value


def update(book, sheet, records):
    rows = read(book, sheet)
    existing = {r[0]: i+1 for i, r in enumerate(rows) if i >= 3 and r}
    next_row = len(rows)+1
    payload = []
    for record in records:
        row_number = existing.get(str(record['id']))
        if row_number is None:
            row_number, next_row = next_row, next_row+1
        for key, value in record.items():
            payload.append(dict(cell=f'{column(rows[0].index(key)+1)}{row_number}', value=value,
                type='string' if isinstance(value, str) else 'int' if isinstance(value, int) else 'float'))
    name = book+'.'+sheet+'.cells.json'
    write_json(name, payload)
    subprocess.run([CLI, 'write', str(ROOT/'data/Excel'/book), '--sheet', sheet, '--data-file', str(OUT/name)], check=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    m = json.loads((SOURCE/'manifest.json').read_text(encoding='utf-8'))
    assert m['revision'] == '20261117-r4' and m['geometry']['draw_order'] == [6,5,4,1,2,3]
    old = {sheet:read(BOOK, sheet) for sheet in ['Templates','Artworks','Layers','Parameters']}
    cards = read('GuLiStrikeRogueCards.xlsx', 'Cards')
    icons = read('GuLiStrikeIcons.xlsx', 'Icons')
    assert str(ART_ID) not in {r[0] for r in old['Artworks'][3:]}, 'Already registered; maintain Excel directly'
    assert str(TEMPLATE_ID) not in {r[0] for r in old['Templates'][3:]}
    write_json('registration-before.json', dict(artwork=old, cards=cards, icons=icons))
    g=m['geometry'];cw,ch=g['canvas'];left,top,right,bottom=g['visible_window']
    frame_w, frame_h = 1280*48.617/1556, 48.617
    face_w, face_h = 30*1.35, 45*1.35
    aw, ah = frame_w*.92, frame_h*(.84-.145)
    fx,fy=aw/(right-left),ah/(bottom-top)
    assert math.isclose(fx,fy,rel_tol=1e-12), 'Source window does not match the fixed Ground frame'
    scale=(cw*fx/face_w,ch*fy/face_h)
    center=(.5,.5-((.5-(.145+.84)/2)*frame_h+5)/face_h)
    offset=(center[0]-.5-((left+right)/(2*cw)-.5)*scale[0],center[1]-.5-((top+bottom)/(2*ch)-.5)*scale[1])
    exact=(center[0]-aw/face_w/2,center[1]-ah/face_h/2,center[0]+aw/face_w/2,center[1]+ah/face_h/2)
    aperture=(exact[0]-.002,exact[1]-.002,exact[2]+.002,exact[3]+.002)
    params=[]
    def param(key,kind,value=0,source=None,texture=None):
        ident=ART_ID*1000+len(params)+1
        row=dict(id=ident,name=f'{NAME}_Parameter{ident}',Note='',ArtworkId=ART_ID,Key=key,Kind=kind,ScalarValue=value if kind=='Scalar' else 0,VectorR=0,VectorG=0,VectorB=0,VectorA=0,Texture='',SourceFile='',TextureRole='')
        if kind=='Vector':row.update(zip(('VectorR','VectorG','VectorB','VectorA'),value))
        if kind=='Texture':row.update(Texture=texture+'.'+texture.rsplit('/',1)[1],SourceFile=source.relative_to(ROOT).as_posix(),TextureRole='Color')
        params.append(row)
    layers=[];sources=[];margins=[]
    for item in m['layers']:
        i=item['index'];path=SOURCE/item['file']
        assert sha(path)==item['sha256']
        with Image.open(path) as im:
            assert im.mode=='RGBA' and list(im.size)==g['canvas']
            alpha=im.getchannel('A').getextrema()
        texture=ASSET+f'/Textures/T_{NAME}_Layer{i}'
        param(f'Layer {i} Map','Texture',source=path,texture=texture)
        layers.append(dict(id=ART_ID*10+i,name=f'{NAME}_Layer{i}',Note='r4完整PNG；共同坐标；整层刚性位移',ArtworkId=ART_ID,LayerIndex=i,Label=item['name'],TextureParameter=f'Layer {i} Map',Depth=item['depth'],Opacity=1,ScaleU=scale[0],ScaleV=scale[1],ScaleB=1,ScaleA=1,OffsetU=offset[0],OffsetV=offset[1],OffsetB=0,OffsetA=0,MotionOwnerLayer=5 if i==4 else 0))
        sources.append(dict(layer=i,file=path.relative_to(ROOT).as_posix(),sha256=sha(path),size=[cw,ch],alpha_range=alpha,texture=texture))
        motion=abs(g['motion_owner_depths'][item['motion_owner']])
        bounds=[((aperture[0]-.5-offset[0])/scale[0]+.5)*cw,((aperture[1]-.5-offset[1])/scale[1]+.5)*ch,((aperture[2]-.5-offset[0])/scale[0]+.5)*cw,((aperture[3]-.5-offset[1])/scale[1]+.5)*ch]
        margin=[bounds[0]-24*motion,bounds[1]-24*motion,cw-bounds[2]-24*motion,ch-bounds[3]-24*motion]
        assert min(margin)>g['filter_margin']
        margins.append(dict(layer=i,full_strength_margin_pixels=margin))
    param('Layers global depth','Scalar',.75)
    param('Parallax Travel','Vector',(24/cw,24/ch,0,0))
    param('Parallax Tilt Tangent','Scalar',math.tan(math.radians(16)))
    param('Artwork Aperture','Vector',aperture)
    param('Layer 3 Split Enabled','Scalar',0)
    update(BOOK,'Templates',[dict(id=TEMPLATE_ID,name='RegisteredSixRigidProjectileFront',Note='独立父材质；按HTML顺序6/5/4/1/2/3；兼容现有六层参数接口',ParentMaterial=PARENT+'.'+PARENT.rsplit('/',1)[1],Layout='Independent6',CompositePreset='RegisteredSixRigidLayer3Mask')])
    update(BOOK,'Artworks',[dict(id=ART_ID,name=NAME,Note='2026-11-17 用户明确授权r4正式入库；导弹表面与轴线修订',Title='强化爆炸弹',TemplateId=TEMPLATE_ID,Material=MATERIAL+'.'+MATERIAL.rsplit('/',1)[1],AssetRoot=ASSET,SourceRoot=SOURCE.relative_to(ROOT).as_posix(),ReferenceFile=(SOURCE/m['reference']['file']).relative_to(ROOT).as_posix(),PreviewUrl='http://127.0.0.1:8197/?v=20261117-r4')])
    update(BOOK,'Layers',layers)
    update(BOOK,'Parameters',params)
    # Register art independently. The gameplay row is bound only after its icon
    # choice is resolved; a non-placeholder card cannot export with IconId=0.
    assert read('GuLiStrikeRogueCards.xlsx','Cards')==cards
    assert read('GuLiStrikeIcons.xlsx','Icons')==icons
    for sheet,rows in old.items():
        after=read(BOOK,sheet)
        assert after[:len(rows)]==rows, 'Existing catalog rows changed: '+sheet
    report=dict(success=True,artwork_id=ART_ID,template_id=TEMPLATE_ID,card_id='12.01',material=MATERIAL,parent_material=PARENT,source_revision=m['revision'],source_manifest_sha256=sha(SOURCE/'manifest.json'),draw_order=g['draw_order'],scale=scale,offset=offset,exact_card_aperture=exact,material_aperture_with_underlap=aperture,source_window=g['visible_window'],uniform_physical_pixel_scale=[fx,fy],source_window_visible_fraction=[1,1],aperture_filled_fraction=[1,1],motion_margins=margins,sources=sources,gameplay_changed_cells=[],gameplay_binding_pending_icon_choice=True,prepared_artwork_binding={'card_id':'12.01','old_artwork_id':9,'new_artwork_id':ART_ID},icon_id_unchanged=0,other_gameplay_fields_preserved=True,other_catalog_rows_preserved=True,native_schema_changed=False,user_authorized_formal_import=True,assistant_visual_test=False)
    write_json('registration.json',report)
    print(json.dumps({k:report[k] for k in ['success','artwork_id','template_id','card_id','scale','offset']},ensure_ascii=False))


if __name__=='__main__':main()
