"""Freeze the user's two cards and prepare registered imagegen edit anchors.

Masks and source-coordinate extraction are production inputs. All missing
surfaces and outer painting are completed by the built-in imagegen tool.
"""
from pathlib import Path
import hashlib
import json
import shutil
import sys
from datetime import datetime, timezone, timedelta

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, 'D:/gen-image/tools/ricochet-source-extraction')
import cv2

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/UI/GroundMechRogueCards/BurningCards_HTML_20261029_v1'
SIZE, PAD, CANVAS = 1024, 256, 1536
cv2.setRNGSeed(29)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def poly(points):
    image = Image.new('L', (SIZE, SIZE), 0)
    ImageDraw.Draw(image).polygon(points, fill=255)
    return np.asarray(image) > 0


def dilate(mask, radius=2):
    return cv2.dilate(mask.astype('uint8'), np.ones((radius*2+1, radius*2+1), 'uint8')) > 0


def close(mask, radius=2):
    return cv2.morphologyEx(mask.astype('uint8'), cv2.MORPH_CLOSE, np.ones((radius*2+1, radius*2+1), 'uint8')) > 0


def strokes(points, width):
    image = Image.new('L', (SIZE, SIZE), 0)
    ImageDraw.Draw(image).line(points, fill=255, width=width, joint='curve')
    return np.asarray(image) > 0


def grab(rgb, points, seeds):
    rough = poly(points)
    area = dilate(rough, 5)
    labels = np.full((SIZE, SIZE), cv2.GC_BGD, np.uint8)
    labels[area] = cv2.GC_PR_BGD
    labels[rough] = cv2.GC_PR_FGD
    for seed in seeds:
        labels[poly(seed)] = cv2.GC_FGD
    cv2.grabCut(rgb, labels, None, np.zeros((1,65)), np.zeros((1,65)), 3, cv2.GC_INIT_WITH_MASK)
    return ((labels == cv2.GC_FGD) | (labels == cv2.GC_PR_FGD)) & area


def fragments(rgb, outlines):
    # Polygons follow free fragments on the original input; they are not new art.
    result = np.zeros((SIZE, SIZE), bool)
    for points in outlines:
        region = poly(points)
        r,g,b = [rgb[:,:,i].astype('int16') for i in range(3)]
        material = (r > g*1.13) | ((r+g+b) < 240)
        selected = region & material
        result |= close(dilate(selected, 1), 2) & region
    return result


def burst_masks(rgb):
    r,g,b = [rgb[:,:,i].astype('int16') for i in range(3)]
    debris = fragments(rgb, [
        [(39,124),(97,127),(135,211),(110,242),(62,219)],
        [(678,0),(755,0),(843,106),(839,158),(806,220),(745,190),(699,109)],
        [(37,652),(123,653),(224,753),(301,864),(324,942),(294,1002),(244,976),(151,887),(64,764)],
        [(819,838),(897,859),(976,898),(1023,907),(1023,1023),(853,1023),(831,943)],
        [(921,750),(1003,766),(1023,850),(1003,900),(960,875)],
        [(320,692),(367,694),(389,751),(363,810),(329,784)],
        [(166,292),(194,296),(204,333),(180,345),(166,322)],
        [(54,258),(88,256),(101,285),(79,301),(59,285)],
        [(860,300),(889,288),(908,310),(894,348),(867,345)],
        [(930,354),(963,339),(984,359),(968,389),(944,389)],
        [(763,71),(790,57),(809,83),(795,113)],
        [(44,552),(88,550),(103,581),(80,620),(50,600)],
        [(951,685),(998,683),(1009,718),(974,743)],
    ])
    rock_roi = poly([(0,753),(49,774),(76,815),(119,858),(121,918),(89,966),(59,1023),(0,1023)])
    rock_roi |= poly([(350,896),(421,908),(477,919),(535,929),(593,947),(649,977),(769,1023),(348,1023),(377,968)])
    gray = (np.abs(r-g)<44) & (np.abs(g-b)<48) & (r<203) & (g<205)
    rocks = close(dilate(rock_roi & gray,1),3) & rock_roi

    ring_roi = strokes([(0,518),(127,551),(284,585),(451,616),(633,647),(813,675),(1023,690)],28)
    ring_roi |= strokes([(0,250),(92,230),(190,208),(308,184)],18)
    ring_roi |= strokes([(773,260),(889,269),(1023,264)],18)
    rays=[[(0,338),(281,404)],[(0,422),(255,459)],[(0,466),(235,467)],
          [(0,290),(271,348)],[(0,555),(256,514)],[(764,359),(1023,302)],
          [(796,414),(1023,384)],[(816,470),(1023,496)],[(809,565),(1023,607)],
          [(794,623),(1023,738)],[(735,695),(876,918)],[(543,735),(495,970)]]
    for line in rays: ring_roi |= strokes(line,12)
    hot=(r>230)&(g>101)&(b<185)
    ring=dilate(ring_roi & hot,1)&ring_roi

    body=grab(rgb,[(422,238),(486,210),(575,230),(640,196),(687,162),(757,248),(798,380),
        (867,421),(905,507),(876,574),(815,600),(824,640),(859,716),(935,821),
        (905,883),(868,860),(808,780),(738,692),(704,649),(674,764),(642,851),
        (565,882),(514,838),(522,762),(519,696),(484,649),(427,639),(347,680),
        (296,733),(241,725),(239,677),(191,684),(144,669),(169,599),(212,527),
        (289,454),(287,404),(316,378),(277,347),(278,304),(305,269),(361,283),
        (396,322),(435,340),(454,296)],
        [[(302,299),(348,305),(371,349),(333,375),(302,342)],
         [(473,259),(505,266),(529,302),(501,326),(475,302)],
         [(565,560),(617,579),(618,646),(568,659)],
         [(760,466),(834,477),(857,516),(821,552),(774,529)],
         [(263,494),(288,483),(301,508),(277,542)],
         [(571,760),(610,775),(612,829),(565,851)]])
    flame_roi=poly([(279,0),(760,0),(853,179),(857,349),(938,485),(884,675),
        (938,840),(833,904),(660,900),(516,894),(312,857),(222,767),(197,557),(218,326),(290,174)])
    fire=((r>190)&(r>g+23)&(g>b+14)) | ((r>70)&(r>g+4)&(g>b+3)&(r-g<60)&(g-b<55))
    body |= close(flame_roi & fire,2)
    body |= strokes([(473,17),(580,269)],7) | strokes([(543,124),(597,261)],7)

    distant=grab(rgb,[(15,331),(87,331),(122,316),(166,337),(230,367),(227,449),
        (196,491),(155,494),(124,453),(87,463),(32,462),(12,418)],
        [[(108,355),(151,357),(172,389),(153,417),(113,405)],[(23,366),(59,369),(68,387),(24,391)]])
    distant |= grab(rgb,[(853,513),(889,516),(913,500),(950,520),(979,533),(1023,539),
        (1023,641),(994,663),(965,650),(946,613),(915,629),(879,607),(853,577)],
        [[(907,543),(940,539),(959,571),(943,594),(909,577)],[(979,555),(1012,556),(1017,584),(983,579)]])
    return [debris,rocks,ring,body,distant]


def bullet_masks(rgb):
    r,g,b=[rgb[:,:,i].astype('int16') for i in range(3)]
    bullet_shape=poly([(514,363),(557,388),(600,429),(650,465),(701,493),(747,538),
        (798,579),(836,643),(866,702),(902,760),(937,831),(960,882),(938,885),
        (870,845),(813,798),(758,748),(708,690),(660,629),(609,570),(568,505),(543,445),(522,398)])
    flame_red=(r>223)&(g<118)&(r>g+75)
    bullet=close(bullet_shape & ~flame_red,1)
    # User correction: the projectile's attached burning envelope belongs to
    # layer 1 and travels with the projectile, not with the impact-fire layer.
    yy,xx=np.mgrid[:SIZE,:SIZE]
    attached_fire_roi=dilate(bullet_shape,42)&(yy>415)&(xx>493)
    attached_fire=attached_fire_roi&(r>219)&(g>35)&(b<164)&(r>g+9)
    bullet |= close(attached_fire,2)
    debris=fragments(rgb,[
        [(0,507),(52,512),(116,578),(180,647),(178,738),(117,793),(37,754),(0,738)],
        [(0,676),(91,697),(186,790),(251,872),(296,969),(285,1023),(0,1023)],
        [(274,861),(325,852),(401,908),(439,959),(444,1023),(333,1023),(279,962)],
        [(806,57),(866,55),(894,107),(876,155),(826,151),(798,117)],
        [(945,68),(1023,53),(1023,286),(974,269),(945,204)],
        [(846,898),(910,929),(936,1001),(908,1023),(854,1023)],
        [(432,665),(468,651),(501,686),(483,733),(441,731)],
        [(322,594),(349,581),(377,604),(367,641),(333,644)],
        [(251,247),(276,242),(289,266),(270,286),(248,272)],
        [(654,66),(680,61),(692,87),(675,105),(650,92)],
        [(833,231),(862,216),(885,238),(872,276),(845,279)],
        [(933,520),(961,508),(986,533),(967,561),(940,555)],
        [(573,743),(604,734),(629,765),(614,794),(579,790)],
    ])
    fire_roi=poly([(405,281),(576,307),(705,399),(837,520),(934,673),(1004,835),(1023,998),
        (917,1023),(780,916),(653,767),(552,622),(477,492),(426,409)])
    orange=(r>224)&(g>38)&(b<132)&(r>g+13)
    fire=fire_roi&orange
    impact_roi=poly([(423,286),(498,292),(577,318),(627,379),(612,447),(538,471),(460,434),(422,367)])
    fire |= impact_roi & ((r>230)&(g>120)&(b<218))
    for line in [[(0,322),(470,349)],[(0,418),(456,374)],[(0,600),(472,404)],
                 [(1023,329),(599,360)],[(1023,503),(611,398)],[(1023,27),(622,292)],
                 [(17,0),(445,309)],[(234,1023),(479,475)]]:
        fire |= strokes(line,16)&((r>230)&(g>75)&(b<217))
    fire=close(fire,1)
    body=grab(rgb,[(421,150),(500,159),(553,173),(615,192),(681,219),(744,228),
        (790,309),(839,347),(919,359),(999,420),(991,499),(949,530),(914,518),
        (955,694),(947,730),(897,738),(859,645),(796,546),(725,526),(672,592),
        (636,655),(595,671),(546,616),(488,548),(422,628),(406,689),(345,704),
        (287,658),(316,577),(286,506),(218,530),(174,580),(141,567),(149,493),
        (180,431),(140,427),(141,364),(189,276),(242,278),(263,307),(285,345),
        (316,308),(350,249),(373,207),(419,204)],
        [[(430,172),(478,178),(494,208),(459,230),(430,210)],
         [(338,230),(371,240),(394,291),(370,321),(336,291)],
         [(170,366),(206,344),(225,363),(208,405),(170,408)],
         [(686,242),(718,247),(741,279),(724,308),(691,292)],
         [(853,391),(940,407),(960,449),(915,475),(859,448)],
         [(346,526),(377,538),(389,585),(360,613),(336,580)]])
    body |= strokes([(594,0),(651,232)],8) | strokes([(651,4),(681,227)],8)
    shooter=grab(rgb,[(41,123),(93,124),(115,131),(148,133),(179,143),(199,164),
        (198,193),(174,206),(196,248),(185,278),(158,276),(137,251),(119,229),
        (102,232),(68,252),(44,242),(43,220),(62,192),(50,179)],
        [[(88,148),(122,142),(147,156),(135,183),(94,177)],
         [(156,219),(175,227),(180,252),(163,253)],[(56,212),(77,200),(84,216),(63,235)]])
    shot=strokes([(187,176),(526,365)],17)&((r>230)&(g>80)&(b<221))
    flash=poly([(154,151),(187,147),(211,150),(226,179),(211,201),(180,207),(160,191)])
    shooter |= shot | (flash & ((r>225)&(g>65)))
    return [bullet,debris,fire,body,shooter]


COMMON = '''Use case: precise-object-edit / registered parallax-layer completion.
Image 1 is the EDIT TARGET: a source-pixel layer anchor on a full 1536x1536 canvas. The original 1024x1024 illustration occupies exactly [256,256,1280,1280]. The transparent 256-pixel gutter on every side is intentional working space. Keep this ENTIRE canvas and all object positions. Never crop to the object, center it, auto-fit its bounds, change scale, rotate the horizon, mirror the scene, or alter the camera. Reconstruct the missing/occluded parts in place. Extend objects which cross the old source boundary naturally into the outer gutter; do not stretch or repeat edge pixels.
Image 2 is the user's FROZEN COMPLETE REFERENCE, for identity, layout, occlusion and exact drawing style. It is NOT a request to redraw the full scene in this layer.
The user selected this clean flat anime version: crisp black outlines, three clear value groups, large matte color shapes, burgundy/ivory machines, golden orange fire. Preserve it exactly; do not revert to a detailed painted earlier draft, glossy 3D or photoreal texture. Original visible fragments in image 1 are coordinate anchors, not pieces to rearrange. Fill small holes and missing solid surfaces but preserve intentional battle damage. Remove all mistakenly included pixels from OTHER layers. Output only the one named layer, never a six-panel atlas or contact sheet. No text, labels, card frame, ruler, guide marks, watermark, checkerboard or background color for transparent layers.
'''

SPECS = {
    'Burst': [
        ('飞溅的燃烧的碎片','01_BurningFragments',True,'''Complete ONLY the free flying burning mechanical fragments and small airborne shards: large burgundy/ivory slab at bottom left, cropped slab at bottom right, detached burgundy top-right armor, top-left fragment and the other small debris anchors. Reconstruct their complete mechanical surfaces behind fire streaks and continue edge-clipped fragments into the gutter. Fire attached to EACH detached fragment travels WITH that fragment in this layer. Exclude the entire central robot, all attached central fire/smoke, shockwave arcs, foreground rocks, both distant robots and landscape. Preserve every fragment's original location and apparent scale. True alpha=0 outside separate fragments and their own flames.'''),
        ('前景石块','02_ForegroundRocks',True,'''Complete ONLY the closest grey foreground stone blocks and broken stone terrace along the lower-left and lower edge. Reconstruct rock faces hidden by the flying armor fragment; make them continuous solid stone, not robot-shaped holes or floating slivers. Preserve the sloping original perspective and extend the bottom/left cropped rock surfaces into the gutter. Keep a small amount of moss attached to stone. No burning metal fragments, orange fire, shockwave, machine, grass-field background or sky. True alpha=0 outside these foreground stone silhouettes.'''),
        ('冲击环','03_Shockwave',True,'''Complete ONLY the huge outward-propagating orange/cream shockwave at the central machine's MIDBODY height: its bright near arc sweeps diagonally across the lower torso, far arc curves behind the torso, and outward pressure/flame streaks fan laterally. Reconstruct portions hidden by the robot and debris so this one full coherent wave can be occluded in HTML. Keep both side turning points beyond the source crop and continue through the gutter. Preserve the near arc's exact diagonal course and the outward motion. It is a thin transparent pressure front, not a ground circle, spherical shield or opaque fire disc. No robot, attached burning plume, smoke cloud, fragments, rocks or scenery. All space between strokes is real transparency.'''),
        ('爆炸的机甲','04_BurningPioneer',True,'''Complete ONLY the central burning ivory/burgundy quadrupedal Pioneer: four original legs, both lateral multi-barrel guns, two long antennae, exposed black internal mechanisms, torn installed armor, attached deflagration flames and the central rising smoke plume. Repair body surfaces hidden by the shockwave, free debris and foreground stone; keep intentional torn/exploded armor and the original exact pose. Flame roots stay attached to this machine. Remove the thin long shockwave ring and radial pressure lines; no free flying armor shards or other robots. No environment. Complete clipped antennas/smoke through the upper gutter. True transparent background around this one complete burning machine and its attached fire/smoke.'''),
        ('远处机甲','05_DistantPioneers',True,'''Complete ONLY the TWO distant small ivory/burgundy Pioneer machines, at the exact original left-middle and right-middle positions. Each has four legs, two guns and antennae. Preserve their different original sizes and angles. Restore small hidden surfaces/legs where pressure streaks or foreground fire crossed them. Continue the right machine through the old crop if needed. No central machine, flames, shockwave, loose fragments, terrain, dust wall, architecture or sky. The TWO machines stay separate on one common full canvas, with real alpha=0 everywhere between them. Never enlarge or center this pair.'''),
        ('背景草地和远景','06_Environment',False,'''Create the COMPLETE FULLY OPAQUE background plate: sloping green grassland and stone terraces, far ruined monumental arches and towers, bright blue sky and white clouds, subdued distant dust. Remove every central/distant machine, bullet, loose fragment, close foreground rock, attached burning plume, orange fire and shockwave. Paint the full terrain/ruins/sky hidden underneath all removed objects; leave NO robot-shaped holes or ghost outlines. Preserve the original tilted horizon, layout and flat cel-shaded palette. Continue this complete landscape through all four gutters. This one layer covers EVERY canvas pixel with opaque color.'''),
    ],
    'Bullet': [
        ('子弹（含附着燃烧）','01_BurningBullet',True,'''The user explicitly corrected this layer: it MUST be a BURNING BULLET. Complete the giant golden/bronze projectile TOGETHER WITH ITS ATTACHED ORANGE-RED FLAME ENVELOPE. The hot fire wraps tightly around the metal body and licks backward along its sides, visibly attached to this projectile, with cream-hot flame roots, orange/red tips and a few local sparks. Reconstruct the solid metallic body underneath without transparent flame-shaped holes, then preserve the original flaming silhouette around it. Both metal and its attached burning flames belong to THIS SAME LAYER and move together. Keep the original dark panels, ivory reflection stripes, curved bands and sharp nose readable through the fire. Preserve the exact perspective axis from small rear/base near original (524,375) to sharp nose near (952,879), exact placement, angle and scale. Do not center or enlarge it. Exclude the separate star-shaped puncture explosion at the rear origin, independent flying fragments, robot, long trajectory rays and scenery. Genuinely transparent RGBA outside the flaming projectile; soft alpha only in wispy flame edges. This is not a plain unlit metal projectile. Keep the full empty upper-left canvas and the 256-pixel gutter.'''),
        ('飞溅的燃烧的碎片','02_BurningFragments',True,'''Complete ONLY free flying fragments and their attached small burning flames: huge cut-off burgundy/ivory slabs at left and lower edge, burgundy pieces at upper right and lower right, free dark mechanical/stone shards scattered around the hit. Reconstruct solid fragment faces hidden by streaks and continue edge-clipped pieces through the gutter. Keep their exact original positions, sizes and perspective. No giant bullet, central robot, main flame envelope, impact burst, distant shooter, beam, landscape or sky. True alpha=0 between detached pieces; attached flames stay with their own fragments.'''),
        ('火焰（命中爆燃）','03_Flames',True,'''Complete ONLY the SEPARATE IMPACT DEFLAGRATION: the hot star-shaped burst at the puncture near original (524,365), short outward flame jets from the torn enemy torso, and the existing long tapered hot impact rays. Reconstruct parts hidden by the flaming projectile, fragments and robot while keeping the exact origin and ray directions. The projectile's own attached fire envelope is deliberately assigned to LAYER 1 after the user's correction: exclude that whole near projectile and its wrapped flames from this layer. No gold metal bullet, armor fragments, robot body, shooter or scenery. Keep this explosion localized to the hit and its outward rays; do not fill the lower-right canvas with a second copy of the bullet's fire. Real transparent alpha around and between fire tongues; no opaque orange background or spherical explosion ball.'''),
        ('爆炸的机甲','04_HitPioneer',True,'''Complete ONLY the one large central ivory/burgundy quadrupedal Pioneer being punctured: four legs, both black lateral multi-barrel guns, two tall antennae, torn armor and dark exposed mechanisms. Preserve its exact original pose, placement, apparent scale, deliberate armor damage and puncture. Reconstruct surfaces hidden by the independent projectile, flame envelope and flying fragments; leave the intentional central penetration opening as damaged exposed structure, not an arbitrary transparency hole. Exclude all flames, orange speed/impact lines, projectile, free flying panels, small distant shooter and environment. True alpha=0 around this one machine. Extend clipped body pieces and antennas through the gutter without changing position.'''),
        ('远处正在射击的机甲和弹道','05_ShooterAndTrajectory',True,'''Complete ONLY the original small BLUE/IVORY bipedal firing mech in the upper-left background, its attached muzzle flash, and its SINGLE straight shot trajectory toward the central puncture. The user deliberately chose this exact mech appearance: do not substitute any newer model or redesign it. Keep the original shooter scale and upper-left coordinates. The muzzle near original (187,176), trajectory and target puncture near (524,365) must remain collinear. Restore any interrupted part of this path behind the main enemy, for occlusion by that separate layer; do not move the impact endpoint. No large central enemy, giant near projectile, free fragments, main impact explosion, landscape or sky. True alpha=0 around this shooter/flash/trajectory group. No recentering.'''),
        ('背景草地和远景','06_Environment',False,'''Create a COMPLETE FULLY OPAQUE environment plate: original sloping green grassland, stepped grey stone terraces, monumental ruined towers and arches with small moss areas, blue sky, white clouds and distant tan dust. Remove the huge projectile and all its flame, every main/distant mech, all shot/impact rays and flying debris. Rebuild the landscape hidden by these objects with continuous ground and complete ruin surfaces. Keep the exact original tilted camera, horizon, architecture positions and flat three-tone palette. Extend all four gutters naturally. No object-shaped holes, floating armor remnants, seams or silhouettes. EVERY pixel fully opaque.'''),
    ]
}


def main():
    OUT.mkdir(parents=True, exist_ok=False)
    all_requests=[]
    for key,source in [
        ('Burst',Path('C:/Users/a/AppData/Local/Temp/codex-clipboard-679150f9-8b4f-414e-b6bc-2526b6ed39ac.png')),
        ('Bullet',Path('C:/Users/a/AppData/Local/Temp/codex-clipboard-fb77333f-9c81-42e3-8c2c-c8a2c0c1b251.png')),
    ]:
        folder=OUT/key
        for name in ['Inputs','Masks','CoreLayers','Prompts','Supplements','Layers']:
            (folder/name).mkdir(parents=True)
        shutil.copy2(source,folder/'Master.png')
        with Image.open(source) as image:
            assert image.size==(SIZE,SIZE)
            rgb=np.asarray(image.convert('RGB')).copy()
        masks=burst_masks(rgb) if key=='Burst' else bullet_masks(rgb)
        owner=np.full((SIZE,SIZE),6,np.uint8)
        for index in range(5,0,-1): owner[masks[index-1]]=index
        # A complete disjoint source-pixel partition; no source RGB is moved.
        Image.fromarray(owner).save(folder/'Masks/Ownership.png')
        source_record={'source':source.as_posix(),'frozen':'Master.png','sha256':sha(source),
            'size':[SIZE,SIZE],'user_selected_for_layering':True,'user_art_review':'layered output pending',
            'layer_order':'user order: nearest to farthest','canvas':[CANVAS,CANVAS],
            'source_window':[PAD,PAD,PAD+SIZE,PAD+SIZE], 'source_pixel_transform':False,
            'production_method':'source-coordinate masks + original pixel anchors; imagegen fills hidden surfaces and gutters',
            'assistant_visual_test':False,'created_at':datetime.now(timezone(timedelta(hours=8))).isoformat()}
        (folder/'reference.json').write_text(json.dumps(source_record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        for index,(name,slug,transparent,instruction) in enumerate(SPECS[key],1):
            mask=owner==index
            Image.fromarray(mask.astype('uint8')*255).save(folder/'Masks'/f'{slug}.png')
            core=np.zeros((SIZE,SIZE,4),np.uint8)
            core[mask,:3]=rgb[mask];core[mask,3]=255
            Image.fromarray(core).save(folder/'CoreLayers'/f'{slug}.png')
            guide=np.zeros((CANVAS,CANVAS,4),np.uint8)
            guide[PAD:PAD+SIZE,PAD:PAD+SIZE]=core
            target=folder/'Inputs'/f'{slug}_Edit.png'
            Image.fromarray(guide).save(target)
            prompt=COMMON+'\nTHIS ONE OUTPUT: '+key+' / layer '+str(index)+' / '+name+'\n'+instruction
            prompt+='\nOutput a genuinely transparent RGBA PNG outside this layer.' if transparent else '\nOutput a fully opaque full-canvas PNG.'
            (folder/'Prompts'/f'{slug}.txt').write_text(prompt+'\n',encoding='utf-8')
            all_requests.append({'id':f'{key}_{index}','card':key,'index':index,'name':name,'slug':slug,
                'prompt':prompt,'referenced_image_paths':[target.as_posix(),(folder/'Master.png').as_posix()],
                'transparent_background':transparent,'output':(folder/'Supplements'/f'{slug}_Raw.png').as_posix(),
                'source_owned_pixels':int(mask.sum()),'input_sha256':[sha(target),sha(folder/'Master.png')]})
    (OUT/'generation-requests.json').write_text(json.dumps(all_requests,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'directory':OUT.as_posix(),'requests':len(all_requests),'canvas':[CANVAS,CANVAS],
        'source_window':[PAD,PAD,PAD+SIZE,PAD+SIZE], 'owned_pixels':{r['id']:r['source_owned_pixels'] for r in all_requests}},ensure_ascii=False))


if __name__=='__main__': main()
