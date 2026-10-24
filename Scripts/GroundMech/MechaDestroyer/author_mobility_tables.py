"""Seed the four approved mech sheets via excelize; never overwrite existing tuning.

After BakeGroundContacts has measured the actual mesh/animations, --calibration writes
only the derived sole coordinates/reference speeds. All runtime values originate here
in GuLiStrikeMech.xlsx, then use the standard project exporter/importer.
"""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BOOK = ROOT / 'Data/Excel/GuLiStrikeMech.xlsx'
CLI = r'D:\UE5.7\excelize-cli\bin\xlsx.exe'
ANIMS = '/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations/'

def cli(*args):
    return subprocess.check_output([CLI, *map(str,args)], encoding='utf-8')

def column(n):
    out = ''
    while n:
        n, r = divmod(n-1,26); out = chr(65+r)+out
    return out

def write(sheet,cells):
    with tempfile.TemporaryDirectory(prefix='guli_mech_cells_') as temp:
        path=Path(temp)/'cells.json'; path.write_text(json.dumps(cells,ensure_ascii=False),encoding='utf8')
        cli('write',BOOK,'--sheet',sheet,'--data-file',path)

def sheet(name, fields, rows):
    fields=[('id','int'),('name','str'),('Note','str')]+fields
    cells=[]
    for col,(field,kind) in enumerate(fields,1):
        for row,value in enumerate([field,kind,'Optional' if field=='Note' or kind=='softobject' else 'Necessary'],1):
            cells.append({'cell':f'{column(col)}{row}','value':value,'type':'string'})
        for row,data in enumerate(rows,4):
            value=data.get(field,'' if kind in ('str','softobject') else False if kind=='bool' else 0)
            cells.append({'cell':f'{column(col)}{row}','value':value,'type':{'str':'string','softobject':'string'}.get(kind,kind)})
    cli('add-sheet',BOOK,'--name',name); write(name,cells)
    cli('style',BOOK,'--sheet',name,'--range',f'A1:{column(len(fields))}3','--bold','--bg','D9E1F2','--wrap')
    cli('col-width',BOOK,'--sheet',name,'--col','A','--to',column(len(fields)),'--width','22')

def asset(name): return ANIMS+name+'.'+name

def seed():
    existing=json.loads(cli('sheets',BOOK,'--json'))['sheets']
    # CLI returns a string array.
    definitions={}
    mobility=dict(WalkSpeed=720.,SprintSpeed=1440.,Acceleration=2880.,Braking=2880.,
        MovingTurnRate=540.,TurnStartDegrees=15.,TurnStopDegrees=5.,TurnMovementThreshold=10.,
        GunYawLimit=75.,GunPitchUpLimit=80.,GunPitchDownLimit=65.)
    definitions['Mobility']=([(k,'float') for k in mobility],[dict(id=1,name='Default',Note='cm / s / deg; Ground player only',**mobility)])
    gait_fields=[('Asset','softobject'),('ReferenceSpeed','float'),('MinStrideScale','float'),('MaxStrideScale','float'),
        ('MinPlayRate','float'),('MaxPlayRate','float'),('BasePlayRate','float'),('TransitionSeconds','float'),
        ('SpeedSmoothingSeconds','float'),('TurnDegrees','float'),('RecoilCentimeters','float')]
    clips=[('Idle','Anim_Mecha_Idle'),('WalkForward','Anim_Mecha_Walk_FWD'),('WalkBackward','Anim_Mecha_WalK_BWD'),
        ('StrafeLeft','Anim_Mecha_Strafe_L'),('StrafeRight','Anim_Mecha_Strafe_R'),('RunForward','Anim_Run_FWD'),
        ('RunBackward','Anim_Mecha_Run_BWD'),('TurnLeft','Montage_Mecha_Turn_L_Montage'),('TurnRight','Montage_Mecha_Turn_R_Montage'),
        ('JumpStart','Anim_Mecha_Jump_Start'),('JumpLoop','Anim_Mecha_Jump_Loop'),('Shoot','Anim_Mecha_Shoot'),
        ('WalkBlend','BlendSpace_Loco_Walk'),('RunBlend','BlendSpace_Loco_Run')]
    rows=[]
    for i,(name,clip) in enumerate(clips,1):
        rows.append(dict(id=i,name=name,Note='ReferenceSpeed derived from planted sole displacement (cm/s)',Asset=asset(clip),
            ReferenceSpeed=1.,MinStrideScale=.75,MaxStrideScale=1.35,MinPlayRate=.05,MaxPlayRate=32.,BasePlayRate=1.,
            TransitionSeconds=.18,SpeedSmoothingSeconds=.10,TurnDegrees=0.,RecoilCentimeters=5. if name=='Shoot' else 0.))
    definitions['Gait']=(gait_fields,rows)
    ik=dict(TraceUp=80.,TraceDown=100.,MaxFootCorrection=75.,PelvisDown=60.,PelvisUp=30.,MaxTiltDegrees=45.,
        SoleOffset=0.,PlantThreshold=.65,ReleaseThreshold=.35,BlendInSeconds=.15,BlendOutSeconds=.08,
        InterpSpeed=12.,LockReleaseDistance=180.,ContactHeightTolerance=5.,ContactBlendHeight=8.)
    ikfields=[('Enabled','bool')]+[(k,'float') for k in ik]
    for prefix in ['HeelLeft','ToeLeft','HeelRight','ToeRight']:
        for axis in 'XYZ': ikfields.append((prefix+axis,'float')); ik[prefix+axis]=0.
    definitions['GroundIK']=(ikfields,[dict(id=1,name='Default',Note='Heel in Foot bone space; Toe in FootEnd space; cm/deg/s; no stretch',Enabled=True,**ik)])
    camfields=[(k,'float') for k in ['ArmLength','SocketOffsetX','SocketOffsetY','SocketOffsetZ','PitchDegrees','YawDegrees','FieldOfView',
        'TransitionSeconds','Sensitivity','MinPitchDegrees','MaxPitchDegrees','MinDistance','MaxDistance','ZoomStep']]
    cameras=[]
    for i,name,length,y,z,pitch,yaw in [(1,'Free',1084.42362610,338.24235197,135.74471350,-14.85,-3.90),
        (2,'Aim',362.95970144,220.62727270,275.88868731,-9.65,-3.70)]:
        cameras.append(dict(id=i,name=name,Note='cm/deg/s; switch preserves current orbit; Camera local transform zero',ArmLength=length,
            SocketOffsetX=0.,SocketOffsetY=y,SocketOffsetZ=z,PitchDegrees=pitch,YawDegrees=yaw,FieldOfView=90.,TransitionSeconds=.2,
            Sensitivity=.15,MinPitchDegrees=-75.,MaxPitchDegrees=70.,MinDistance=length*.65,MaxDistance=length*2.,ZoomStep=122.01841))
    definitions['Camera']=(camfields,cameras)
    for name,(fields,rows) in definitions.items():
        if name not in existing: sheet(name,fields,rows)

def calibrate(path):
    data=json.loads(Path(path).read_text(encoding='utf8'))
    assert data['success'], data
    for name in ['GroundIK','Gait']:
        raw=json.loads(cli('read',BOOK,'--sheet',name,'--format','json'))['rows']
        headers=raw[0]; cells=[]
        for row_index,row in enumerate(raw[3:],4):
            key=row[headers.index('name')]
            values=data['sole'] if name=='GroundIK' else data['gait'].get(key,{})
            for field,value in values.items():
                if field in headers: cells.append({'cell':f'{column(headers.index(field)+1)}{row_index}','value':value,'type':'float'})
        write(name,cells)

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--calibration'); args=parser.parse_args()
    seed()
    if args.calibration: calibrate(args.calibration)
    print(cli('info',BOOK))
