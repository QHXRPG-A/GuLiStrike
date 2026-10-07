"""Lay out unedited native style renders and actual decoded movie checkpoints."""
import json
from pathlib import Path
import numpy as np
from PIL import Image,ImageFont,ImageDraw
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1';V=O/'AnimationPreviews';Q=O/'QA';S=O/'Sheets'
font=lambda n:ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',n);bg='#F3EEE5';ink='#1A182F'
style=Image.new('RGB',(4200,4470),bg);draw=ImageDraw.Draw(style);draw.text((100,60),'B_v1 实际材质拆分 · 相同模型 / 姿态 / 机位',font=font(68),fill=ink)
labels={'FlatColor':'基础色块','LineAndOutline':'基础色 + 内部结构线与实际描边壳','ThreeTone':'基础色 + 暗 / 中 / 亮三档','Combined':'最终实际成品'}
for i,(mode,label) in enumerate(labels.items()):
 x=(i%2)*2050+50;y=(i//2)*2100+270;im=Image.open(O/'Renders'/('Style_'+mode+'.png')).convert('RGB').resize((2000,2000),Image.Resampling.LANCZOS);style.paste(im,(x,y));draw.text((x+70,y-55),label,font=font(40),fill=ink)
style.save(S/'Actual_Style_Breakdown.png')
d=json.loads((V/'movie_readback_report.json').read_text());checks=[]
for clip in d['clips']:
 for c in clip['decoded_checkpoints']:
  a=np.array(Image.open(V/c['file']).convert('RGB'),dtype=float);b=np.array(Image.open(V/c['native_source']).convert('RGB'),dtype=float);assert a.shape==b.shape
  error=float(np.abs(a-b).mean());assert error<4.,(clip['clip'],c['frame_index'],error)
  checks.append({'clip':clip['clip'],'frame_index':c['frame_index'],'H264_vs_native_mean_absolute_RGB_8bit_error':error})
for page in range(4):
 clips=d['clips'][page*6:(page+1)*6]
 if not clips:continue
 board=Image.new('RGB',(2880,650*len(clips)),bg);draw=ImageDraw.Draw(board)
 for row,clip in enumerate(clips):
  draw.text((35,row*650+15),clip['clip']+' · 三档同播',font=font(30),fill=ink)
  for col,idx in enumerate((0,2,4)):
   c=clip['decoded_checkpoints'][idx];im=Image.open(V/c['file']).convert('RGB').resize((960,384),Image.Resampling.LANCZOS);board.paste(im,(col*960,row*650+100));draw.text((col*960+25,row*650+510),f"实际 MP4 回读 · {c['time_s']:.2f}s",font=font(27),fill=ink)
 board.save(Q/f'All_Movies_Decoded_{page+1}.png')
for asset in sorted({c['asset'] for c in d['clips']}):
 clips=[c for c in d['clips'] if c['asset']==asset];board=Image.new('RGB',(3840,480*len(clips)+140),bg);draw=ImageDraw.Draw(board);draw.text((55,35),asset+' · 全部动画实际视频回读',font=font(50),fill=ink)
 for row,clip in enumerate(clips):
  for col,c in enumerate(clip['decoded_checkpoints']):
   board.paste(Image.open(V/c['file']).convert('RGB').resize((768,308),Image.Resampling.LANCZOS),(col*768,row*480+220));draw.text((col*768+25,row*480+540),f"{c['time_s']:.2f}s",font=font(26),fill=ink)
  draw.text((45,row*480+160),clip['clip'],font=font(33),fill=ink)
 board.save(S/(asset+'_All_Animation_Checkpoints.png'))
(V/'movie_pixel_validation.json').write_text(json.dumps({'all_110_checkpoints_compared':len(checks)==110,'maximum_mean_RGB_error':max(c['H264_vs_native_mean_absolute_RGB_8bit_error'] for c in checks),'checks':checks},indent=2),encoding='utf8')
print('SSF_ACTUAL_MOVIE_AND_STYLE_BOARDS_OK',len(checks),flush=True)
