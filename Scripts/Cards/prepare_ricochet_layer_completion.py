"""Prepare six registered edit inputs for built-in imagegen layer completion."""
from pathlib import Path
import hashlib
import json
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/CardProduction_20261026_v1'
PAD=256
SIZE=1536
NAMES=['01_Foreground','02_SpeedLines','03_PlayerMech','04_Scene','05_Ricochet_Enemies','06_FarBackground']
(OUT/'Inputs/CompletionR3').mkdir(parents=True,exist_ok=True)

COMMON='''Use case: precise-object-edit / layer completion for a parallax card.
Image 1 is the actual edit target: an incomplete isolated layer on a 1536x1536 canvas. The original 1024x1024 image occupies exactly the central rectangle [256,256,1280,1280]; the 256-pixel surrounding gutter is intentional new working space. Preserve that full canvas and coordinate system. Never crop to the object, auto-fit its bounds, center it, change the camera, scale it, or straighten the tilted horizon.
Image 2 is the original complete illustration, supplied only to identify the original design, placement, hidden connections, colours and clean anime three-tone cel shading. Do NOT put the entire second image into the output.
Complete ONLY the specified isolated layer into a coherent usable painted asset. Repair cutout nicks and tiny missing colour islands, remove traces of objects belonging to other layers, reconstruct the parts hidden by other objects, and continue any object that crosses the old central rectangle into the new gutter. Keep all originally visible intended structure, pose, contours, colours, outlines and locations as closely as possible. Existing coloured fragments are coordinate anchors, not pieces to rearrange. Do not leave transparent cracks inside solid surfaces, torn rectangular crop edges or detached pieces caused by segmentation.
Match the original clean black mechanical outlines, matte large colour blocks, three hard-edged light levels, bright daylight and low-noise anime rendering. No photoreal texture, grain, scratches, new story elements, text, watermarks, labels, UI, card frame or checkerboard texture.
'''

SPECS=[
('近景',True,'''Complete the nearest foreground only: the close grey stone slabs and green leaves at bottom left, the closest massive dark grey moss-covered broken wall at bottom right, the brown braking dirt wave, airborne small rocks and beige dust around the planted foot. Turn the fragmented dust into coherent layered dust volumes with natural outer silhouettes. Reconstruct the foreground surfaces hidden by white speed streaks, flames and the mech where appropriate. Remove accidental white speed-line fragments and any blue, ivory or cream mechanical armour pieces incorrectly included in this cutout. Keep free earthy debris only.
The right wall and bottom-left rocks must extend continuously beyond their original RIGHT, LEFT and BOTTOM crop boundaries through the new 256-pixel gutter, reaching the new outer canvas edges wherever the same object continues. Do not shrink them or leave a transparent rectangular border just outside the old source square. Preserve their original perspective and apparent scale. Complete obscured wall/rock surfaces and natural dust contours rather than stretching or repeating edge pixels.
Do NOT include any player mech, enemy robot, muzzle flash, flames, golden bullet trail, white speed line, grassland backdrop, distant architecture or sky. Use genuinely transparent RGBA outside foreground objects. Solid rock/dirt/foliage interiors opaque; wispy dust may have soft alpha.'''),
('速度线',True,'''Complete only the original white/light-grey comic speed streaks as clean continuous tapered strokes. Use the original complete image to recover portions missing from this cutout. Keep each original direction, width, start and end, density and perspective; join broken portions instead of adding unrelated rays. Extend cropped streaks naturally into the new gutter without relocating their central visible portions.
No dust, rocks, armour, grass, buildings, sky, orange flames, golden ricochet lines or missile smoke. Genuinely transparent RGBA around the white/light-grey strokes, with clean anti-aliased stroke edges.'''),
('机甲',True,'''Complete the one blue-and-ivory bipedal player ground mech, its two attached orange-yellow exhaust flames and the attached muzzle flash. Reconstruct missing armour surfaces, black mechanical joints, leg/foot pieces and structural connections obscured by dust or speed streaks. Preserve the exact original over-shoulder pose, left-foreground scale, firing direction, two rear nozzles, gun geometry and braking stance. Complete hidden parts by continuing the same existing mechanical design; do not invent extra limbs, barrels, antennas or faces. Close small transparent holes and disconnected seams inside the mech. Keep the two flame roots attached directly to their original outlets.
If a mech part or flame crosses the old original canvas edge, extend it naturally into the new gutter. No recentering or zooming out. Do NOT include dirt, dust clouds, loose rocks, white speed streaks, golden ricochet trail, enemies or environment. Genuinely transparent RGBA outside this one complete mech and its attached effects.'''),
('场景',True,'''Complete the mid/near environment: the sloping sunny meadow, grass platforms, stone terraces and large near broken arches and mossy walls. Recover all ground and architecture hidden by the mech, enemies, impact bursts, bullet trails, dust, close foreground rocks and closest right-side wall. The meadow and architecture must be continuous clean surfaces, without robot-shaped holes, leftover armour islands or rectangular patches. Continue terrain and near architecture through the gutter at unchanged perspective and scale.
Remove all player/enemy mechs, flames, muzzle flash, golden ricochet, white speed streaks, close braking dust and closest foreground pieces. Sky openings and regions outside midground environment must be genuinely transparent. Keep the far sky, distant missile trails and far skyline for another layer; do not paint an opaque sky behind the arches. Genuinely transparent RGBA outside environment silhouettes.'''),
('折射弹道和三个敌方机甲',True,'''Complete one connected group consisting of exactly the THREE original ivory-and-burgundy quadrupedal enemy mechs, their three impact explosions, their associated loose armour/parts and the single connected golden ricochet path. Reconstruct missing structural pieces hidden by fire/smoke and repair accidental transparent nicks in armour while retaining deliberate battle damage, broken armour, explosive fragmentation and all three different staggered poses.
Keep the exact original three enemy positions, apparent sizes and trajectory connection order. Keep the gold path beginning just outside the player muzzle, then joining all three original impact points. Repair broken short segments so the path is continuous; do not move its bends or add independent beams. Complete localized impact smoke and detached fragments as coherent effects with clean alpha. Extend any enemy part clipped by the old right canvas boundary into the gutter in the same perspective.
No player mech, player exhaust flame, player muzzle flash, white speed streaks, foreground dirt or environment. Genuinely transparent RGBA between the enemies, legs, path segments and particles. No solid rectangular background.'''),
('远景',False,'''Complete a full opaque far background plate: original blue sky, white clouds, distant grassland terraces, far postmodern ruined skyline, distant smoke and fires, and original distant missiles with smoke trails. Retain their original camera tilt, layout, lighting, colour blocks and scale. Fill every hole in the input and all four outer gutters with continuous quiet distant terrain or sky as appropriate.
Remove all player/enemy mechs, close golden ricochet, close impact bursts, player flames, speed lines, foreground braking dust, close rocks, closest right wall and large near arch foregrounds. No silhouettes of removed units or repeated mechs. This must be a genuinely FULLY OPAQUE PNG covering the entire 1536x1536 canvas, suitable behind all other layers.''')]

requests=[]
for index,(name,transparent,instruction) in enumerate(SPECS,1):
    core=np.asarray(Image.open(OUT/'CoreLayers'/f'{NAMES[index-1]}.png').convert('RGBA'))
    guide=np.zeros((SIZE,SIZE,4),dtype=np.uint8)
    guide[PAD:PAD+1024,PAD:PAD+1024]=core
    guide[guide[:,:,3]==0,:3]=0
    target=OUT/'Inputs/CompletionR3'/f'{NAMES[index-1]}_Edit.png'
    Image.fromarray(guide).save(target)
    prompt=COMMON+'\nTHIS OUTPUT LAYER: '+str(index)+' / '+name+'\n'+instruction
    prompt_path=OUT/'Prompts'/f'CompletionR3_{NAMES[index-1]}.txt'
    prompt_path.write_text(prompt,encoding='utf-8',newline='\n')
    requests.append({'layer':index,'name':name,'slug':NAMES[index-1],'prompt':prompt,
                     'prompt_file':str(prompt_path),'prompt_sha256':hashlib.sha256(prompt.encode()).hexdigest(),
                     'referenced_image_paths':[str(target),str(OUT/'Master.png')],
                     'transparent_background':transparent,'requested_canvas':[SIZE,SIZE],
                     'source_window':[PAD,PAD,PAD+1024,PAD+1024],
                     'raw_output':str(OUT/'Supplements'/f'CompletedR3_{NAMES[index-1]}_Raw.png')})
(OUT/'completion-r3-requests.json').write_text(json.dumps(requests,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'inputs':len(requests),'canvas':[SIZE,SIZE],'source_window':[PAD,PAD,PAD+1024,PAD+1024],
                  'request_file':str(OUT/'completion-r3-requests.json')},ensure_ascii=False))
