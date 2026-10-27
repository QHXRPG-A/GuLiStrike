"""User-authorized original-pixel extraction, with a common padded canvas.

No generated illustration is used for any originally visible object pixel.
AI plates are consumed only beneath extracted objects and outside the source.
"""
from pathlib import Path
import hashlib
import json
import sys
import zipfile
from datetime import datetime
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/CardProduction_20261026_v1"
sys.path.insert(0, "D:/gen-image/tools/ricochet-source-extraction")
import cv2
cv2.setRNGSeed(42)

PAD = 128
SIZE = 1024
CANVAS = SIZE + 2 * PAD
NAMES = ["01_Foreground", "02_SpeedLines", "03_PlayerMech", "04_Scene", "05_Ricochet_Enemies", "06_FarBackground"]
for folder in ("Masks", "Inputs", "Supplements", "CoreLayers"):
    (OUT / folder).mkdir(exist_ok=True)
rgb = np.asarray(Image.open(OUT / "Master.png").convert("RGB")).copy()
assert rgb.shape == (SIZE, SIZE, 3)


def poly(points):
    image = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(image).polygon(points, fill=255)
    return np.asarray(image) > 0


def grab(points, seeds, iterations=5):
    rough = poly(points)
    expanded = ndimage.binary_dilation(rough, iterations=9)
    labels = np.full((SIZE, SIZE), cv2.GC_BGD, dtype=np.uint8)
    labels[expanded] = cv2.GC_PR_BGD
    labels[rough] = cv2.GC_PR_FGD
    for seed in seeds:
        labels[poly(seed)] = cv2.GC_FGD
    cv2.grabCut(rgb, labels, None, np.zeros((1,65)), np.zeros((1,65)), iterations, cv2.GC_INIT_WITH_MASK)
    return ((labels == cv2.GC_FGD) | (labels == cv2.GC_PR_FGD)) & expanded


def prepare():
    mech_points = [(0,281),(25,270),(32,234),(83,219),(130,213),(146,197),(179,196),(194,216),
                   (230,232),(250,229),(278,239),(298,255),(313,284),(351,285),(388,302),
                   (423,311),(448,331),(455,361),(432,379),(405,382),(408,418),(383,442),
                   (341,447),(310,426),(282,433),(257,443),(267,456),(302,454),(327,463),
                   (342,487),(359,517),(388,540),(417,539),(447,581),(465,609),(482,653),
                   (500,689),(518,716),(529,755),(564,773),(580,806),(612,807),(638,843),
                   (621,874),(589,883),(553,870),(529,848),(500,833),(466,804),(443,771),
                   (415,735),(395,689),(381,655),(360,632),(335,602),(311,583),(280,562),
                   (263,536),(242,518),(226,507),(215,533),(196,548),(158,536),(142,514),
                   (141,473),(118,452),(100,428),(106,399),(86,380),(49,354),(34,336),(0,343)]
    seeds = [[(72,250),(110,242),(120,259),(93,279),(64,279)],
             [(166,281),(194,287),(208,324),(192,350),(158,333)],
             [(299,314),(344,319),(359,367),(333,382),(307,364)],
             [(261,476),(301,478),(310,507),(286,528),(263,511)],
             [(364,562),(398,560),(418,598),(391,608)],
             [(443,701),(465,711),(481,753),(463,761)]]
    mech = grab(mech_points, seeds)
    # Both attached flames and the muzzle flash remain with the player mech.
    flames = poly([(112,367),(140,379),(124,425),(91,474),(46,533),(0,571),(0,446),(37,405),(81,381)])
    flames |= poly([(202,404),(227,418),(211,464),(176,523),(127,581),(65,625),(11,622),(42,558),(108,482),(166,429)])
    fire_color = (rgb[:,:,0] > 160) & (rgb[:,:,1] > 90) & (rgb[:,:,0].astype(int) > rgb[:,:,2].astype(int)+45)
    mech |= ndimage.binary_closing(flames & fire_color, iterations=2)
    muzzle = poly([(437,329),(467,323),(483,344),(477,367),(448,381),(433,365)])
    mech |= muzzle & ((rgb[:,:,0] > 215) & (rgb[:,:,1] > 150))

    enemy_specs = [
      ([(484,215),(521,216),(552,205),(583,210),(619,225),(650,243),(667,275),(656,303),
        (641,327),(642,355),(616,366),(611,391),(584,409),(556,380),(532,364),(511,354),
        (479,329),(481,291),(485,255)], [[(552,254),(579,254),(593,283),(577,310),(550,295)],[(536,232),(547,233),(555,247),(542,252)]]),
      ([(616,343),(649,342),(671,330),(699,338),(715,347),(750,346),(779,352),(799,373),
        (811,398),(830,417),(835,456),(821,477),(794,474),(774,455),(756,454),(742,481),
        (718,487),(697,470),(683,452),(660,448),(632,445),(610,430),(609,405),(621,377)],
       [[(715,367),(746,368),(764,391),(746,415),(714,406)],[(673,351),(686,349),(697,365),(683,372)]]),
      ([(820,416),(850,407),(877,419),(893,393),(910,394),(918,433),(946,434),(967,422),
        (991,431),(1009,454),(1023,466),(1023,598),(1001,602),(985,587),(970,570),
        (948,578),(930,595),(906,588),(892,562),(867,558),(849,540),(822,526),(814,499)],
       [[(917,453),(947,456),(965,479),(950,501),(921,495)],[(890,424),(903,425),(906,448),(894,448)]])
    ]
    enemies = np.zeros((SIZE,SIZE), dtype=bool)
    for points, enemy_seeds in enemy_specs:
        enemies |= grab(points, enemy_seeds)
    # Select the original bright trail through narrow, source-coordinate bands.
    trail_guide = Image.new("L", (SIZE,SIZE), 0)
    draw = ImageDraw.Draw(trail_guide)
    draw.line([(451,352),(578,280),(742,385),(941,474)], fill=255, width=26)
    for x,y,radius in [(578,280,32),(742,385,32),(941,474,35)]:
        draw.ellipse((x-radius,y-radius,x+radius,y+radius), fill=255)
    r,g,b = [rgb[:,:,i].astype(np.int16) for i in range(3)]
    gold = (r > 215) & (g > 144) & (r >= g-4) & ((g-b > 36) | ((r+g+b) > 708))
    enemies |= ndimage.binary_dilation((np.asarray(trail_guide)>0) & gold, iterations=1)

    right_wall = grab([(1023,570),(1003,591),(1001,621),(966,641),(948,649),(918,660),
                       (885,671),(871,708),(836,710),(816,754),(795,789),(780,811),
                       (766,853),(746,873),(724,899),(705,948),(688,975),(658,1004),
                       (651,1023),(1023,1023)],
                      [[(924,733),(978,750),(971,894),(905,925),(851,898)],
                       [(849,866),(878,895),(838,963),(794,977)]])
    left_rocks = grab([(0,812),(29,826),(68,834),(100,858),(129,864),(143,889),
                       (215,900),(281,921),(315,937),(354,960),(370,990),(453,1023),(0,1023)],
                      [[(0,876),(59,865),(98,895),(77,949),(0,974)],[(131,941),(203,953),(231,1001),(153,1007)]])
    dirt_roi = poly([(0,420),(42,443),(86,471),(117,512),(165,545),(223,560),(279,590),
                     (330,599),(387,635),(450,641),(485,682),(542,708),(554,751),(587,791),
                     (627,809),(665,835),(709,885),(707,928),(671,961),(586,1004),(375,1023),(0,1023)])
    dirt_color = ((r > g+7) & (g > b+7)) | ((r > 175) & (g > 148) & (g > b+6) & (r > g+3))
    dirt = dirt_roi & dirt_color
    dirt = ndimage.binary_closing(dirt, iterations=2)
    foreground = (right_wall | left_rocks | dirt) & ~mech & ~enemies

    speed_roi = np.zeros((SIZE,SIZE), dtype=bool)
    bands = [ [(0,100),(204,298),(190,300),(0,140)],
              [(0,160),(176,306),(160,309),(0,188)],
              [(0,602),(0,637),(616,509),(596,494)],
              [(0,655),(0,693),(575,561),(565,543)],
              [(0,744),(0,777),(452,619),(438,601)],
              [(0,844),(0,884),(334,738),(319,724)],
              [(210,1023),(284,1023),(555,779),(543,767)] ]
    for band in bands:
        speed_roi |= poly(band)
    whites = (r > 200) & (g > 195) & (b > 182) & ((r-b) < 46) & ((g-b)<30)
    candidates = speed_roi & whites
    labels, count = ndimage.label(candidates)
    speed = np.zeros_like(candidates)
    for index, bounds in enumerate(ndimage.find_objects(labels), 1):
        if bounds is None:
            continue
        yy,xx = np.where(labels[bounds]==index)
        if len(xx)<20 or max(xx.max()-xx.min(), yy.max()-yy.min())<22:
            continue
        eigen=np.linalg.eigvalsh(np.cov(np.stack([xx,yy])))
        if eigen[-1]/max(eigen[0],.1)>9:
            speed[bounds] |= labels[bounds]==index
    speed = ndimage.binary_dilation(speed, iterations=1) & speed_roi & whites

    far = poly([(0,0),(1023,0),(1023,415),(883,352),(712,263),(547,180),(437,158),
                (339,124),(190,72),(0,162)])
    near_arch = poly([(0,0),(359,0),(354,325),(314,350),(255,323),(227,297),(178,288),
                      (117,274),(70,254),(0,196)])
    sky_color = (b > r+20) & (b > g+10) & (b > 145)
    far &= ~near_arch | sky_color

    # Every source pixel has exactly one owner; no layer is independently scaled.
    owner = np.full((SIZE,SIZE),4,dtype=np.uint8)
    owner[far] = 6
    owner[enemies] = 5
    owner[mech] = 3
    owner[speed] = 2
    owner[foreground & ~speed] = 1
    Image.fromarray(owner).save(OUT/'Masks/Ownership.png')
    counts={}
    for index,name in enumerate(NAMES,1):
        mask=(owner==index)
        counts[str(index)]=int(mask.sum())
        Image.fromarray(mask.astype(np.uint8)*255).save(OUT/'Masks'/f'{name}.png')
        core=np.zeros((SIZE,SIZE,4),dtype=np.uint8)
        core[mask,:3]=rgb[mask]
        core[:,:,3]=mask.astype(np.uint8)*255
        Image.fromarray(core).save(OUT/'CoreLayers'/f'{name}.png')
    assert sum(counts.values())==SIZE*SIZE and all(counts.values())
    guide=np.zeros((CANVAS,CANVAS,4),dtype=np.uint8)
    guide[PAD:PAD+SIZE,PAD:PAD+SIZE]=np.asarray(Image.open(OUT/'CoreLayers/01_Foreground.png'))
    Image.fromarray(guide).save(OUT/'Inputs/ForegroundOverscanGuide.png')
    record={"method":"original RGB pixels with mutually exclusive masks; no resizing or moving original pixels",
            "user_authorization":"2026-10-26: 采用原图像素抠层（推荐）",
            "source_sha256":hashlib.sha256((OUT/'Master.png').read_bytes()).hexdigest(),
            "source_canvas":[SIZE,SIZE],"padded_canvas":[CANVAS,CANVAS],"padding_each_side":PAD,
            "source_window":[PAD,PAD,PAD+SIZE,PAD+SIZE],"pixel_ownership_counts":counts,
            "ownership_is_partition":True,"mask_method":"source-coordinate polygons and OpenCV GrabCut, constrained colour extraction",
            "assistant_visual_validation":False,"user_mask_review":"pending",
            "alpha_policy":"source RGB copied unchanged; painted object interiors opaque; transparency only outside ownership mask"}
    (OUT/'source-extraction.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(record,ensure_ascii=False))


def finish():
    """Compose only hidden fills and overscan around the original RGB cutouts."""
    owner=np.asarray(Image.open(OUT/'Masks/Ownership.png'))
    clean_raw=OUT/'Supplements/CleanEnvironment_Raw.png'
    outer_raw=OUT/'Supplements/OuterCanvas_Raw.png'
    assert clean_raw.is_file() and outer_raw.is_file()
    clean_image=Image.open(clean_raw).convert('RGB')
    outer_image=Image.open(outer_raw).convert('RGB')
    raw_sizes={'clean_environment':list(clean_image.size),'outer_extension':list(outer_image.size)}
    clean=np.asarray(clean_image.resize((SIZE,SIZE),Image.Resampling.LANCZOS))
    outer=np.asarray(outer_image.resize((CANVAS,CANVAS),Image.Resampling.LANCZOS))
    clean_padded=np.pad(clean,((PAD,PAD),(PAD,PAD),(0,0)),mode='reflect')
    edge_rgb=np.pad(rgb,((PAD,PAD),(PAD,PAD),(0,0)),mode='edge')
    yy,xx=np.mgrid[:CANVAS,:CANVAS]
    distance=np.maximum.reduce([PAD-xx,xx-(PAD+SIZE-1),PAD-yy,yy-(PAD+SIZE-1),np.zeros_like(xx)])
    outside=distance>0
    # The seam transition is restricted to new gutter pixels. The source is never blended.
    blend=np.clip(distance/64.,0,1)[:,:,None]
    extension=np.rint(edge_rgb*(1-blend)+outer*blend).astype(np.uint8)
    slices=np.s_[PAD:PAD+SIZE,PAD:PAD+SIZE]
    layers=[]
    counts={str(i):int((owner==i).sum()) for i in range(1,7)}
    assert sum(counts.values())==SIZE*SIZE and all(counts.values())
    speed_mask=(owner==2).astype(np.uint8)*255
    line_underfill=cv2.inpaint(rgb,speed_mask,4,cv2.INPAINT_TELEA)
    for index,name in enumerate(NAMES,1):
        mask=owner==index
        support=mask.copy()
        if index in (1,3,5):
            closed=ndimage.binary_closing(mask,iterations=5,border_value=1)
            support |= closed & (owner==2)
        rgba=np.zeros((CANVAS,CANVAS,4),dtype=np.uint8)
        if index==6:
            rgba[:,:,:3]=clean_padded
            rgba[:,:,3]=255
            rgba[slices][:,:,:3][mask]=rgb[mask]
        elif index==4:
            domain=owner!=6
            padded_domain=np.pad(domain,PAD,mode='edge')
            rgba[:,:,:3][padded_domain]=clean_padded[padded_domain]
            rgba[:,:,3]=padded_domain.astype(np.uint8)*255
            rgba[slices][:,:,:3][mask]=rgb[mask]
        else:
            padded_mask=np.pad(support,PAD,mode='edge')
            use_extension=outside & padded_mask
            rgba[:,:,:3][use_extension]=extension[use_extension]
            rgba[:,:,3][use_extension]=255
            core=rgba[slices]
            hidden=support & ~mask
            core[hidden,:3]=line_underfill[hidden]
            core[hidden,3]=255
            core[mask,:3]=rgb[mask]
            core[mask,3]=255
        output=OUT/'Layers'/f'{name}.png'
        Image.fromarray(rgba).save(output)
        histogram=Image.fromarray(rgba[:,:,3]).histogram()
        layers.append({'index':index,'name':name,'file':f'Layers/{name}.png',
                       'canvas':[CANVAS,CANVAS],'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
                       'original_owned_pixels':counts[str(index)],'hidden_speed_line_underfill_pixels':int((support & ~mask).sum()),'source_window':[PAD,PAD,PAD+SIZE,PAD+SIZE],
                       'source_pixel_resampling':False,'source_pixel_independent_transform':False,
                       'alpha_extrema':[int(rgba[:,:,3].min()),int(rgba[:,:,3].max())],
                       'transparent_pixels':histogram[0],'opaque_pixels':histogram[255]})
    # This reference uses the exact stored RGB; raw Master.png stays untouched.
    Image.fromarray(rgb).save(OUT/'Reference_RGB.png')
    old=json.loads((OUT/'manifest.json').read_text(encoding='utf-8'))
    provenance_path=OUT/'superseded-v1-generation.json'
    if not provenance_path.exists():
        provenance_path.write_text(json.dumps({'status':'rejected_by_user_composition_drift',
            'requests':json.loads((OUT/'generation-requests.json').read_text(encoding='utf-8')),
            'results':json.loads((OUT/'generation-results.json').read_text(encoding='utf-8'))},ensure_ascii=False,indent=2),encoding='utf-8')
    requests=json.loads((OUT/'repair-generation-requests.json').read_text(encoding='utf-8'))
    manifest={'state':'source_pixel_repair_ready','updated_at':datetime.now().astimezone().isoformat(),
              'task':old['task'],'workflow':'original RGB extraction; built-in imagegen for hidden fill and external border only',
              'user_authorization':'采用原图像素抠层（推荐）','comfyui_used':False,'gameplay_modified':False,
              'master':'Master.png','master_sha256':hashlib.sha256((OUT/'Master.png').read_bytes()).hexdigest(),
              'source_canvas':[SIZE,SIZE],'canvas':[CANVAS,CANVAS],'padding_each_side':PAD,
              'source_window':[PAD,PAD,PAD+SIZE,PAD+SIZE],'source_window_uv':[.1,.1,.9,.9],
              'source_rgb_policy':'original RGB copied without resampling or colour edits; painted interiors opaque',
              'source_ownership_partition':{'pixels':SIZE*SIZE,'counts':counts,'mutually_exclusive':True,'complete':True},
              'layers':layers,'composite_back_to_front':[6,4,5,3,1,2],
              'draw_order_note':'speed lines cover the foreground; narrow source-line holes are filled underneath their own original pixels',
              'generative_supplements':{'requests':'repair-generation-requests.json','actual_raw_sizes':raw_sizes,
                  'resampling':'only AI supplements normalized to their target fill canvas; original RGB is never resized',
                  'allowed_domains':'behind opaque source cutouts and outside source window only'},
              'technical_validation':{'passed':True,'scope':'dimensions, alpha, source ownership partition and shared source coordinates; no render or image comparison'},
              'preview':{'file':'preview.html','url':'http://127.0.0.1:8191/preview.html',
                         'shared_css':{'width_percent':125,'height_percent':125,'left_percent':-12.5,'top_percent':-12.5},
                         'default_mode':'flat','default_parallax_pixels':4,'maximum_parallax_pixels':8},
              'assistant_output_visual_test':False,'user_art_review':'pending',
              'ue_imported':False,'ue_delivery':{'import_script':'ImportToUE.py','imported':False,'reason':'Editor connection unavailable; script not executed'},
              'replacement':'same six layer filenames and same preview URL; rejected generated layers are no longer consumed'}
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (OUT/'generation-requests.json').write_text(json.dumps(requests,ensure_ascii=False,indent=2),encoding='utf-8')
    readme='''# 折射子弹 · 原图像素抠层修订

用户确认采用“原图像素抠层，AI只补绘遮挡处和外沿”。沿用同一个预览地址和六个图层文件名。

原图Master.png保持不变。每个原图RGB像素按互斥蒙版归属六层之一，原位置、比例、颜色不做重采样或单层补偿。涂绘对象内部使用不透明alpha，空白处透明；Reference_RGB.png供同色彩对照。
SAM仅做分割蒙版，不生成任何机甲或弹道像素。内置imagegen仅生成遮挡填补底图与新外沿；这些补绘只能出现在原物体后方和原图矩形外。

六张PNG共同为1280×1280；原始1024×1024位于[128,128,1152,1152]，四边各增加128像素。所有层使用同一映射：显示宽高125%，左上偏移-12.5%；原图内容保持原尺度，未放大前景物体。
近景边界沿源蒙版向新留白延续，外沿色彩由生成补图过渡。默认视差4px、最大8px；远景保持固定。

图层编号：1近景、2速度线、3机甲与附着火焰、4场景、5折射弹道与三个敌方机甲、6远景。叠放顺序为6→4→5→3→1→2；速度线覆盖近景，其下被线挡住的窄缝使用局部填充，原位仍由原始速度线像素遮住。

打开preview.html。默认原位叠合，可切换原图或视差，并逐层开关、下载。原位RGB来源由像素归属与统一坐标保证；蒙版语义、运动边界、遮挡补绘和最大倾斜自然程度仍由用户视觉验收。助手未做结果读图、截图、合成图片对比或UE视口测试。

完整生图提示词与输入在repair-generation-requests.json和Prompts/Repair_*；蒙版与来源记录在Masks/和source-extraction.json。模型辅助文件不放入素材包。
未使用ComfyUI，未修改卡表、技能、效果或玩法逻辑。UE编辑器未连接，尚未导入；ImportToUE.py已按共同画布更新，仍需在目标编辑器执行并保存回读，不能将静态语法检查视为已接入。
'''
    (OUT/'README.md').write_text(readme,encoding='utf-8')
    print(json.dumps({'layers':6,'canvas':[CANVAS,CANVAS],'source_window':[PAD,PAD,PAD+SIZE,PAD+SIZE],
                      'source_pixels':counts,'original_rgb_resampling':False,'assistant_visual_test':False},ensure_ascii=False))


if __name__=='__main__':
    if len(sys.argv)==1 or sys.argv[1]=='prepare':
        prepare()
    elif sys.argv[1]=='refresh-guide':
        owner=np.asarray(Image.open(OUT/'Masks/Ownership.png'))
        for index,name in enumerate(NAMES,1):
            mask=owner==index
            core=np.zeros((SIZE,SIZE,4),dtype=np.uint8)
            core[mask,:3]=rgb[mask]
            core[:,:,3]=mask.astype(np.uint8)*255
            Image.fromarray(core).save(OUT/'CoreLayers'/f'{name}.png')
        guide=np.zeros((CANVAS,CANVAS,4),dtype=np.uint8)
        guide[PAD:PAD+SIZE,PAD:PAD+SIZE]=np.asarray(Image.open(OUT/'CoreLayers/01_Foreground.png'))
        Image.fromarray(guide).save(OUT/'Inputs/ForegroundOverscanGuide.png')
        print('Foreground editing input: hidden RGB cleared, original visible RGB preserved')
    elif sys.argv[1]=='finish':
        finish()
