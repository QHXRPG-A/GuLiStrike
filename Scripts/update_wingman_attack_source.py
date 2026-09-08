"""Author the two Ship wingman sheets through the project's excelize CLI, then export normally."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
BOOK = ROOT / 'Data/Excel/GuLiStrikeShip.xlsx'
OUT = ROOT / 'TestResults/WingmanAttack'

def column(index):
    value = ''
    while index:
        index, remainder = divmod(index - 1, 26)
        value = chr(65 + remainder) + value
    return value

def author(sheet, fields, rows):
    existing = subprocess.check_output([str(CLI), 'sheets', str(BOOK), '--json'], text=True, encoding='utf-8')
    if sheet not in existing:
        subprocess.run([str(CLI), 'add-sheet', str(BOOK), '--name', sheet], check=True)
    data = [[item[0] for item in fields], [item[1] for item in fields],
            ['Optional' if item[0] == 'Note' else 'Necessary' for item in fields]] + rows
    cells = [{'cell': f'{column(c + 1)}{r + 1}', 'value': value,
              'type': 'string' if r < 3 or isinstance(value, str) else 'auto'}
             for r, values in enumerate(data) for c, value in enumerate(values)]
    path = OUT / f'{sheet}-cells.json'
    path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
    subprocess.run([str(CLI), 'write', str(BOOK), '--sheet', sheet, '--data-file', str(path)], check=True)
    subprocess.run([str(CLI), 'read', str(BOOK), '--sheet', sheet, '--format', 'tsv'], check=True)

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    author('WingmanTargeting', [('id','int'),('name','str'),('Note','str'),
        ('AcquireRadiusCentimeters','float'),('ReleaseRadiusCentimeters','float'),
        ('GuardRejoinFraction','float'),('ScanIntervalSeconds','float')],
        [[1, 'Default', '1500m获取/1800m释放；超距归队达到门槛后恢复自动索敌',
          150000, 180000, 0.8, 0.2]])
    fields = [('id','int'),('name','str'),('Note','str'),('SkillId','str'),('AttackPattern','str'),
        ('ExecutorId','str'),('Damage','float'),('CooldownSeconds','float'),('RangeCentimeters','float'),
        ('FireConeHalfAngleDegrees','float'),('FlightSpeedCentimetersPerSecond','float'),
        ('DiveSeconds','float'),('MissileCount','int'),('StripLengthCentimeters','float'),
        ('PullUpHeightCentimeters','float'),('ExplosionRadiusCentimeters','float'),
        ('BreakawayDistanceCentimeters','float'),('RetreatMinimumDistanceCentimeters','float'),
        ('RetreatLongitudinalMinFraction','float'),('RetreatLongitudinalMaxFraction','float'),
        ('RetreatLateralRadiusCentimeters','float'),('RetreatVerticalRadiusCentimeters','float'),
        ('ManeuverArrivalRadiusCentimeters','float'),('TurnYawMinDegrees','float'),
        ('TurnYawMaxDegrees','float'),('TurnPitchMaxDegrees','float'),
        ('ProjectileSpeedCentimetersPerSecond','float'),('ProjectileLifetimeSeconds','float'),
        ('SweepRadiusCentimeters','float'),('MuzzleX','float'),('MuzzleY','float'),('MuzzleZ','float')]
    author('WingmanWeapons', fields, [
        [1,'WingmanMachineGun','三维往返缠斗；仅接近段机头射界满足时开火',
         'Wingman.MachineGun','AirDogfight','WingmanMachineGun',10,0.5,150000,20,4500,1.5,10,12000,10000,800,
         30000,45000,0.35,0.60,15000,10000,7500,40,90,30,120000,1.5,45,1200,0,0],
        [2,'WingmanGroundMissile','每架每轮10枚/1.5秒；固定条带；纯AOE；调试初值',
         'Wingman.GroundMissile','GroundDive','WingmanGroundMissile',30,8,150000,20,4500,1.5,10,12000,10000,800,
         30000,45000,0.35,0.60,15000,10000,7500,40,90,30,6000,8,30,1200,0,0]])

if __name__ == '__main__':
    main()
