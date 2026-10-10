"""Apply six authorized resource paths through excelize and the existing exporter."""
import json, subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'outputs/performance/20261009-all-optimizations'
paths={
4:'/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool_SmallBatches',
5:'/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_AllOptimizations',
36:'/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized_GPU_All',
38:'/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool_SmallBatches',
45:'/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized_GPU_All',
52:'/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_AllOptimizations'}

def main():
    xlsx='D:/UE5.7/excelize-cli/bin/xlsx.exe'
    book=ROOT/'Data/Excel/GuLiStrikeVfx.xlsx'
    data=json.loads(subprocess.check_output([xlsx,'read',str(book),'--sheet','Effects','--format','json'],encoding='utf-8'))
    values=data['rows'];assert values[0][0:4]==['id','name','Note','ResourcePath']
    cells=[];proposal={'authorization':'直接应用所有优化，并补齐整版对照','approval':'approved_by_user','applied':False,'rows':[]}
    for index,row in enumerate(values[3:],start=4):
        if not row or not row[0] or int(row[0]) not in paths:continue
        effect_id=int(row[0]);path=paths[effect_id];full=path+'.'+path.rsplit('/',1)[1]
        cells.append({'cell':f'D{index}','value':full,'type':'string'})
        proposal['rows'].append({'id':effect_id,'old_resource':row[3],'new_resource':full,'base_scale':[float(x) for x in row[4:7]],'cell':f'D{index}'})
    assert len(cells)==6
    cell_file=OUT/'reference-cells.json';cell_file.write_text(json.dumps(cells,ensure_ascii=False,indent=2),encoding='utf-8')
    (OUT/'formal-reference-proposal.json').write_text(json.dumps(proposal,ensure_ascii=False,indent=2),encoding='utf-8')
    subprocess.run([xlsx,'write',str(book),'--sheet','Effects','--data-file',str(cell_file)],check=True)
    subprocess.run(['python','-X','utf8',str(ROOT/'Tools/DataPipeline/export_data_from_excel.py')],cwd=ROOT,check=True)

if __name__=='__main__':main()
