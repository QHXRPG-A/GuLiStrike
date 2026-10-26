"""Append approved heavy-gait fields through excelize; preserve all existing tuning."""
import json
from author_mobility_tables import BOOK, cli, column, write

def append(sheet, fields):
    rows=json.loads(cli('read',BOOK,'--sheet',sheet,'--format','json'))['rows']
    headers=rows[0]; cells=[]
    for field,kind,value in fields:
        if field in headers:
            continue
        col=column(len(headers)+1); headers.append(field)
        for line,text in enumerate((field,kind,'Optional' if kind=='softobject' else 'Necessary'),1):
            cells.append(dict(cell=f'{col}{line}',value=text,type='string'))
        for line,row in enumerate(rows[3:],4):
            cells.append(dict(cell=f'{col}{line}',value=value(row[1]) if callable(value) else value,
                              type='string' if kind=='softobject' else kind))
    if cells: write(sheet,cells)

if __name__=='__main__':
    append('Mobility',[('GunReturnSeconds','float',.25)])
    locomotion={'WalkForward','WalkBackward','RunForward','RunBackward','StrafeLeft','StrafeRight'}
    prefix='/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations/'
    def turn_asset(name):
        if name not in ('TurnLeft','TurnRight'): return ''
        asset='Anim_Mecha_MoveTurn_'+('L' if name=='TurnLeft' else 'R')
        return prefix+asset+'.'+asset
    append('Gait',[
        ('HorizontalMotionScale','float',lambda n:.8 if n in locomotion else 1.),
        ('SwingLiftScale','float',lambda n:.65 if n in locomotion else 1.),
        ('PelvisMotionScale','float',lambda n:.55 if n in locomotion else 1.),
        ('SafeMaxStrideScale','float',1.35),
        ('MovingTurnMaxWeight','float',.30),
        ('MovingTurnAsset','softobject',turn_asset)])
    print('Heavy-gait fields available; existing cells preserved.')
