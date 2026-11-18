"""One-time registration of the four approved HTML artworks; daily edits belong in Excel."""
import hashlib
import json
import math
import re
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/UI/GroundMechRogueCards/ExplosiveCards_Production_20261116_v1'
OUT = ROOT / 'Artifacts/CardArtworkPipeline/GroundMechFourCards_20261117'
CLI = r'D:\UE5.7\excelize-cli\bin\xlsx.exe'
PARENT = '/Game/GuLiStrike/CardSystem/RevealDemo/Materials/M_CardArtwork_RegisteredSix_v1'
CARDS = [('FireRate', 10, '09.01'), ('EnhancedBullet', 11, '10.01'),
         ('ExplosivePackage', 12, '11.01'), ('NoSurvivors', 13, None)]


def ground_frame_layout(canvas, window):
    """This existing batch retains its original full-frame crop at user request.

    New artwork must be authored for the fixed Ground aperture aspect; do not
    use this legacy batch's 2:3 source window as the new-production standard.
    """
    frame_w,frame_h=1280*48.617/1556,48.617
    amin,amax=(.04,.145),(.96,.84)
    face_w, face_h = 30 * 1.35, 45 * 1.35
    aperture_w, aperture_h = frame_w*(amax[0]-amin[0]), frame_h*(amax[1]-amin[1])
    center_z = (.5 - (amin[1]+amax[1])/2) * frame_h
    center = (.5, .5 - (center_z + 5) / face_h)
    cw, ch = canvas
    left, top, right, bottom = window
    fit = max(aperture_w / (right-left), aperture_h / (bottom-top))
    scale = (cw * fit / face_w, ch * fit / face_h)
    offset = (center[0]-.5-((left+right)/(2*cw)-.5)*scale[0],
              center[1]-.5-((top+bottom)/(2*ch)-.5)*scale[1])
    half = (aperture_w/face_w/2, aperture_h/face_h/2)
    aperture = (center[0]-half[0]-.002, center[1]-half[1]-.002,
                center[0]+half[0]+.002, center[1]+half[1]+.002)
    return scale, offset, aperture


def read(book, sheet):
    return json.loads(subprocess.check_output([CLI, 'read', str(ROOT / 'data/Excel' / book),
        '--sheet', sheet, '--format', 'json'], encoding='utf-8'))['rows']


def column(n):
    result = ''
    while n:
        n, remainder = divmod(n - 1, 26)
        result = chr(65 + remainder) + result
    return result


def upsert(book, sheet, records):
    rows = read(book, sheet)
    positions = {str(row[0]): i + 1 for i, row in enumerate(rows) if i >= 3 and row}
    next_row = len(rows) + 1
    changes = []
    for record in records:
        index = positions.get(str(record['id']))
        if index is None:
            index = next_row
            next_row += 1
        for key, value in record.items():
            changes.append({'cell': f'{column(rows[0].index(key) + 1)}{index}', 'value': value,
                'type': 'string' if isinstance(value, str) else 'int' if isinstance(value, int) else 'float'})
    path = OUT / f'{book}.{sheet}.cells.json'
    path.write_text(json.dumps(changes, ensure_ascii=False, indent=2), encoding='utf-8')
    subprocess.run([CLI, 'write', str(ROOT / 'data/Excel' / book), '--sheet', sheet,
        '--data-file', str(path)], check=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    book = 'GuLiStrikeCardArtwork.xlsx'
    old = {sheet: read(book, sheet) for sheet in ['Artworks', 'Templates', 'Layers', 'Parameters']}
    assert not any(str(row[0]) in {'10', '11', '12', '13'} for row in old['Artworks'][3:]), \
        'Already registered: maintain the existing Excel rows, do not rerun initialization'
    assert not any(str(row[0]) == '6' for row in old['Templates'][3:]), 'TemplateId 6 already used'
    before_cards = read('GuLiStrikeRogueCards.xlsx', 'Cards')
    (OUT / 'registration-before.json').write_text(json.dumps({'artwork': old, 'cards': before_cards},
        ensure_ascii=False, indent=2), encoding='utf-8')
    cards = json.loads((SOURCE / 'card-data_v7.js').read_text(encoding='utf-8').split('=', 1)[1].strip().rstrip(';'))
    gallery = (SOURCE / 'gallery.js').read_text(encoding='utf-8')
    cards.update(json.loads(re.search(r'Object\.assign\(window\.cardCollection,\s*(\{.*?\})\s*\);', gallery, re.S).group(1)))
    artworks, layers, parameters, evidence = [], [], [], []
    for key, ident, card_id in CARDS:
        data = cards[key]
        asset = f'/Game/GuLiStrike/Cards/GroundMech/{key}'
        name = f'GroundMech_{key}'
        mi = asset + f'/Materials/MI_{name}_Artwork'
        artworks.append(dict(id=ident, name=name, Note='2026-11-17 用户授权现有HTML成品正式入库；视觉验收待用户',
            Title=data['name'], TemplateId=6, Material=mi+'.'+mi.rsplit('/', 1)[1], AssetRoot=asset,
            SourceRoot=SOURCE.relative_to(ROOT).as_posix(), ReferenceFile=(SOURCE/data['source']).relative_to(ROOT).as_posix(),
            PreviewUrl=(SOURCE/'index.html').as_uri()+'?card='+key))
        cw, ch = data['canvas']; left, top, right, bottom = data['window']
        scale, offset, aperture = ground_frame_layout(data['canvas'], data['window'])
        card_params = []
        def param(key, kind='Scalar', value=0, file=None, texture=None, role=''):
            row_id = ident*1000 + len(card_params) + 1
            row = dict(id=row_id, name=f'{name}_Parameter{row_id}', Note='', ArtworkId=ident, Key=key, Kind=kind,
                ScalarValue=value if kind=='Scalar' else 0, VectorR=0, VectorG=0, VectorB=0, VectorA=0,
                Texture='', SourceFile='', TextureRole='')
            if kind == 'Vector':
                row.update(zip(('VectorR','VectorG','VectorB','VectorA'), value))
            if kind == 'Texture':
                row.update(Texture=texture+'.'+texture.rsplit('/',1)[1],
                    SourceFile=(SOURCE/file).relative_to(ROOT).as_posix(), TextureRole=role)
            card_params.append(row)
        source_checks = []
        for layer in data['layers']:
            index = layer['index']; path = SOURCE/layer['file']
            with Image.open(path) as image:
                assert image.mode == 'RGBA' and list(image.size) == data['canvas'], path
                alpha = image.getchannel('A').getextrema()
            source_checks.append(dict(layer=index, file=path.relative_to(ROOT).as_posix(),
                sha256=hashlib.sha256(path.read_bytes()).hexdigest(), dimensions=[cw,ch], alpha=alpha))
            param(f'Layer {index} Map', 'Texture', file=layer['file'], texture=asset+f'/Textures/T_{name}_Layer{index}', role='Color')
            layers.append(dict(id=ident*10+index, name=f'{name}_Layer{index}', Note='原始完整画布；居中映射；刚性平移',
                ArtworkId=ident, LayerIndex=index, Label=layer['name'], TextureParameter=f'Layer {index} Map',
                Depth=layer['depth'], Opacity=1, ScaleU=scale[0], ScaleV=scale[1], ScaleB=1, ScaleA=1,
                OffsetU=offset[0], OffsetV=offset[1], OffsetB=0, OffsetA=0,
                MotionOwnerLayer=4 if key=='FireRate' and index in (3,5) else 0))
        param('Layers global depth', value=.75)
        param('Parallax Travel', 'Vector', (data['travel_px'][0]/cw, data['travel_px'][1]/ch, 0, 0))
        param('Parallax Tilt Tangent', value=math.tan(math.radians(16)))
        param('Artwork Aperture', 'Vector', aperture)
        param('Layer 3 Split Enabled', value=1 if key=='FireRate' else 0)
        if key == 'FireRate':
            mask=data['split_layer']['mask']
            with Image.open(SOURCE/mask) as image:
                assert image.mode == 'LA' and image.getchannel('L').tobytes()==image.getchannel('A').tobytes()
            param('Foreground Front Mask', 'Texture', file=mask,
                texture=asset+'/Textures/T_GroundMech_FireRate_LightningFrontMask', role='Mask')
            source_checks.append(dict(mask=mask, sha256=hashlib.sha256((SOURCE/mask).read_bytes()).hexdigest(),
                channel='R (source luminance equals alpha)'))
        parameters.extend(card_params)
        # Structural motion bound: the fixed aperture plus filtering underlap stays
        # inside every complete source canvas at all four full-strength extremes.
        bounds=[]
        for x,y in [(aperture[0],aperture[1]),(aperture[2],aperture[3])]:
            bounds.append(((x-.5-offset[0])/scale[0]+.5,(y-.5-offset[1])/scale[1]+.5))
        travel=(data['travel_px'][0]/cw, data['travel_px'][1]/ch)
        margin=(bounds[0][0]-travel[0],bounds[0][1]-travel[1],1-bounds[1][0]-travel[0],1-bounds[1][1]-travel[1])
        assert min(margin)>0, (key,margin)
        evidence.append(dict(key=key, artwork_id=ident, gameplay_card_id=card_id, source_revision=data['revision'],
            draw_order=data['draw_order'], scale=scale, offset=offset, aperture=aperture,
            full_strength_margin_pixels=[margin[0]*cw,margin[1]*ch,margin[2]*cw,margin[3]*ch], sources=source_checks))
    upsert(book, 'Templates', [dict(id=6,name='RegisteredSixRigidLayer3Mask',Note='HTML六层刚性视差；XY位移；第三层前后互补蒙版',
        ParentMaterial=PARENT+'.'+PARENT.rsplit('/',1)[1],Layout='Independent6',CompositePreset='RegisteredSixRigidLayer3Mask')])
    for sheet,records in [('Artworks',artworks),('Layers',layers),('Parameters',parameters)]: upsert(book,sheet,records)
    # User selected the existing Fire icon as the temporary HUD icon for all three.
    upsert('GuLiStrikeRogueCards.xlsx','Cards',[{'id':card_id,'ArtworkId':ident,'IconId':5} for _,ident,card_id in CARDS if card_id])
    after_cards=read('GuLiStrikeRogueCards.xlsx','Cards'); art_col=before_cards[0].index('ArtworkId')
    expected=json.loads(json.dumps(before_cards))
    for row in expected[3:]:
        for _,ident,card_id in CARDS:
            if card_id and row[0]==card_id:
                row[art_col]=str(ident)
                row[before_cards[0].index('IconId')]='5'
    assert after_cards==expected, 'Unexpected gameplay workbook change'
    report=dict(success=True,artworks=evidence,native_schema_changed=False,assistant_visual_test=False,
        source_changed=False,gameplay_changes={'09.01':10,'10.01':11,'11.01':12},unbound_artworks=[13],
        temporary_hud_icon_id=5)
    (OUT/'registration.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'success':True,'registered':[10,11,12,13],'report':str(OUT/'registration.json')}))


if __name__ == '__main__': main()
