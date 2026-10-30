"""Register the user's rigid six-layer artwork in the formal Excel catalog."""
import json
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts/GroundMech/Rogue'))
from author_burning_cards import read,write,cell,upsert

def main():
    book='GuLiStrikeCardArtwork.xlsx'
    rows=read(book,'Layers')
    if 'MotionOwnerLayer' not in rows[0]:
        c=len(rows[0])+1
        write(book,'Layers',[cell(1,c,'MotionOwnerLayer'),cell(2,c,'int'),cell(3,c,'Optional')]+[cell(i+1,c,0) for i,r in enumerate(rows) if i>=3 and r and r[0]])
    upsert(book,'Templates',[{'id':4,'name':'RegisteredSixRigid','Note':'完整物件刚性平移；运动归属绑定；完整冲击环局部遮挡合成',
        'ParentMaterial':'/Game/GuLiStrike/CardSystem/RevealDemo/Materials/M_CardArtwork_RegisteredRigid_v1.M_CardArtwork_RegisteredRigid_v1','Layout':'Independent6','CompositePreset':'RegisteredSixRigid'}])
    source=ROOT/'ArtSource/UI/GroundMechRogueCards/BurningCards_HTML_20261029_v1'
    manifest=json.loads((source/'manifest.json').read_text(encoding='utf-8'))
    for key,ident,name in [('Bullet',7,'GroundMech_BurningBullet'),('Burst',8,'GroundMech_BurningBurst')]:
        card=manifest['cards'][key]
        asset=f'/Game/GuLiStrike/Cards/GroundMech/{"BurningBullet" if key=="Bullet" else "BurningBurst"}'
        upsert(book,'Artworks',[{'id':ident,'name':name,'Note':'用户2026-10-29授权正式接入；刚性分层04；视觉验收单独记录','Title':card['title'],
            'TemplateId':4,'Material':asset+f'/Materials/MI_{name}_Artwork.MI_{name}_Artwork','AssetRoot':asset,
            'SourceRoot':source.relative_to(ROOT).as_posix(),'ReferenceFile':(source/card['reference']).relative_to(ROOT).as_posix(),'PreviewUrl':'http://127.0.0.1:8192/'}])
        layers=[];params=[]
        def param(k,kind='Scalar',scalar=0,vector=(0,0,0,0),texture='',file='',role=''):
            i=ident*1000+len(params)+1
            params.append(dict(id=i,name=f'{name}_Parameter{i}',Note='',ArtworkId=ident,Key=k,Kind=kind,ScalarValue=scalar,
                VectorR=vector[0],VectorG=vector[1],VectorB=vector[2],VectorA=vector[3],Texture=texture,SourceFile=file,TextureRole=role))
        for layer in card['layers']:
            index=layer['index'];owner=layer.get('motion_owner_layer',index)
            texture=asset+f'/Textures/T_{name}_Layer{index}.T_{name}_Layer{index}'
            file=(source/layer['file']).relative_to(ROOT).as_posix()
            param(f'Layer {index} Map','Texture',texture=texture,file=file,role='Color')
            layers.append(dict(id=ident*10+index,name=f'{name}_Layer{index}',Note='单物件归属；完整1536画布',ArtworkId=ident,LayerIndex=index,
                Label=layer['name'],TextureParameter=f'Layer {index} Map',Depth=card['depth'][index-1]*0.04,Opacity=1,
                ScaleU=1.5,ScaleV=1.5,ScaleB=1,ScaleA=1,OffsetU=-0.25,OffsetV=-0.25,OffsetB=0,OffsetA=0,MotionOwnerLayer=0 if owner==index else owner))
        # Reuse the complete environment as the overscan source, never a painted edge extrusion.
        env=params[-1]
        param('Background Margin Map','Texture',texture=env['Texture'],file=env['SourceFile'],role='Color')
        param('Layers global depth',scalar=1)
        param('Ability behind machinery',scalar=0)
        param('Foreground behind machinery',scalar=0)
        param('Ring Occlusion Enabled',scalar=1 if key=='Burst' else 0)
        param('Ring Occlusion Plane','Vector',vector=(0.13,393.7,0,0))
        upsert(book,'Layers',layers)
        upsert(book,'Parameters',params)
    upsert(book,'Artworks',[{'id':6,'Note':'Ground折射正式卡06.01；本任务修正机甲正背与相关遮挡，保留正式MI入口'}])
    print(json.dumps({'success':True,'artworks':[7,8],'template':4,'source_revision':manifest['revision']},ensure_ascii=False))

if __name__=='__main__':main()
