"""Compare every native MP4 readback frame with its actual Blender source frame."""
from pathlib import Path
import hashlib,json
import numpy as np
from PIL import Image
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2')
V=R/'AnimationPreviews'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
clips=[]
for clip,count in [('Deploy',76),('Idle',131),('Walk',76)]:
    rows=[]
    for frame in range(1,count+1):
        source=V/f'Mech_{clip}_Frames/{frame:04d}.png'
        decoded=R/f'Logs/FullMovieReadback/{clip}/{frame:04d}.png'
        a=np.asarray(Image.open(source).convert('RGB'),dtype=np.float32)
        b=np.asarray(Image.open(decoded).convert('RGB'),dtype=np.float32)
        assert a.shape==b.shape==(1280,1280,3)
        difference=np.abs(a-b); foreground=np.min(a,axis=2)<190
        mae=float(difference.mean()); foreground_mae=float(difference[foreground].mean())
        assert mae<2 and foreground_mae<6,(clip,frame,mae,foreground_mae)
        rows.append({'frame':frame,'rgb_mae_0_255':mae,'foreground_rgb_mae_0_255':foreground_mae})
    clips.append({'clip':clip,'frame_count':count,'mp4_sha256':sha(V/f'ControlRigMech_B_v2_{clip}.mp4'),
        'all_frames_match_source_within_lossy_codec_tolerance':True,
        'max_rgb_mae_0_255':max(x['rgb_mae_0_255'] for x in rows),
        'max_foreground_rgb_mae_0_255':max(x['foreground_rgb_mae_0_255'] for x in rows),'frames':rows})
report={'version':'B-v2','passed':True,'checked_frames':sum(x['frame_count'] for x in clips),
    'comparison':'Every decoded MP4 frame against corresponding native Blender rendered PNG; no editing or resampling.',
    'rgb_mae_limit_0_255':2,'foreground_rgb_mae_limit_0_255':6,'foreground_rule':'min(source RGB)<190',
    'scope':'Confirms video reproduces native rendered frames; does not prove every source rig pose, collision or UE compatibility.',
    'source_blend_sha256':sha(R/'ControlRigMech_B_v2_Production.blend'),'clips':clips}
(V/'movie_frame_fidelity_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('ALL_MOVIE_FRAMES_MATCH',report['checked_frames'],json.dumps([{k:v for k,v in x.items() if k!='frames'} for x in clips]))
