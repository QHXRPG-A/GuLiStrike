"""Reviewed bulk mechanical migration of remaining world-space VFX literals (not timings)."""
import argparse,hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
manifest_path=root/'TestResults/Scale020/effect-literal-migration-v1.json'
rules={
 'Source/GuLiStrike/Gameplay/CombatEffects/GuLiCombatEffectLaserPresentation.cpp':[
  ('bGround ? Length : FMath::Max(1.0f, Length)','bGround ? Length : FMath::Max(0.2f, Length)'),
  ('Muzzle + Direction * 75.0f','Muzzle + Direction * 15.0f'),
  ('FVector2D(Catalog->LaserCoreWidth * 3.0f, 30.0f)','FVector2D(Catalog->LaserCoreWidth * 3.0f, 6.0f)'),
  ('Muzzle + Direction * 150.0f','Muzzle + Direction * 30.0f'),
  ('FMath::Max(Catalog->LaserCoreWidth * 3.0f, 100.0f)','FMath::Max(Catalog->LaserCoreWidth * 3.0f, 20.0f)')],
 'Source/GuLiStrike/Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.cpp':[
  ('Length <= 1.0f','Length <= 0.2f'),
  ('(GunfireBounds + PreviousGunfireBounds).ExpandBy(100.0)','(GunfireBounds + PreviousGunfireBounds).ExpandBy(20.0)')]
}
args=argparse.ArgumentParser();args.add_argument('--apply',action='store_true');apply=args.parse_args().apply
manifest=json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.exists() else {'version':1,'space':'world-centimeters','entries':[]}
entries={(r['file'],r['before']):r for r in manifest['entries']}
pending=[]
for file,pairs in rules.items():
    path=root/file;text=path.read_text(encoding='utf-8');before=text
    for old,target in pairs:
        assert text.count(old)+text.count(target)==1,(file,old)
        if (file,old) not in entries:
            assert old in text,('No captured baseline',file,old)
            row={'file':file,'before':old,'target':target,'sha256_before':hashlib.sha256(path.read_bytes()).hexdigest()}
            manifest['entries'].append(row);entries[file,old]=row
        if old in text:text=text.replace(old,target)
    if text!=before:pending.append((path,text))
if apply:
    manifest_path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    for path,text in pending:path.write_text(text,encoding='utf-8')
print(json.dumps({'entries':len(manifest['entries']),'files_to_write':len(pending),'applied':apply}))
