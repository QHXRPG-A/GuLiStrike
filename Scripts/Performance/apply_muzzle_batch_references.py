"""ID52's optional batch references through Excel, preserving every other cell."""
import json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'outputs/performance/20261010-muzzle-batch'
CLI='D:/UE5.7/excelize-cli/bin/xlsx.exe';BOOK=ROOT/'Data/Excel/GuLiStrikeVFX.xlsx'
def read():return json.loads(subprocess.check_output([CLI,'read',str(BOOK),'--sheet','Effects','--format','json'],text=True,encoding='utf-8'))['rows']
assert json.loads((OUT/'runtime-contract-validation.json').read_text())['success']
before=read();index=next(i for i,r in enumerate(before) if i>=3 and int(r[0])==52)
assert before[0][9:12]==['BatchResourcePath','ReducedBatchResourcePath','MinimalBatchResourcePath']
changes=[];cells=[]
for col,suffix in zip('JKL',['','_Reduced','_Minimal']):
 package='/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_Batch'+suffix
 value=package+'.'+package.rsplit('/',1)[1];old=before[index][ord(col)-65] if len(before[index])>ord(col)-65 else ''
 cells.append({'cell':f'{col}{index+1}','value':value,'type':'string'})
 changes.append({'field':before[0][ord(col)-65],'before':old,'after':value})
payload=OUT/'formal-muzzle-cells.json';payload.write_text(json.dumps(cells,indent=2),encoding='utf-8')
subprocess.run([CLI,'write',str(BOOK),'--sheet','Effects','--data-file',str(payload)],check=True)
after=read()
assert len(after)==len(before)
for i,(a,b) in enumerate(zip(before,after)):
 for j in range(max(len(a),len(b))):
  if i==index and j in (9,10,11):continue
  assert (a[j] if j<len(a) else '')==(b[j] if j<len(b) else ''),(i,j)
subprocess.run(['python','-X','utf8',str(ROOT/'Tools/DataPipeline/export_data_from_excel.py')],cwd=ROOT,check=True)
(OUT/'formal-muzzle-references.json').write_text(json.dumps({'id':52,'excel_row':index+1,'changes':changes,'base_scale':[float(v) for v in before[index][4:7]],'other_cells_preserved':True,'authorization':'PLEASE IMPLEMENT THIS PLAN: GuLiStrike 客户端表现专属技能、枪口批量化与截帧验证','technical_validation':'passed','visual_acceptance':'pending_user_PIE','default_mode':0},ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'success':True,'row':index+1,'fields':3,'other_cells_preserved':True}))
