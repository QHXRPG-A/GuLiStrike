"""Submit the approved three separate views through Tripo's multiview API.

Reuses the configured local connector transport/authentication without copying
credentials. A recorded task is never submitted twice by an ordinary rerun.
"""
import argparse, asyncio, json, sys, os
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
# The desktop shell injects a SOCKS proxy, whereas the connector runs directly.
# Match that connector transport in this process without changing user settings.
for proxy_key in ('ALL_PROXY','all_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy'):
    if os.environ.get(proxy_key,'').lower().startswith('socks'):
        os.environ.pop(proxy_key,None)
sys.path.insert(0,str(ROOT/'tripo-ue-mcp'))
from server import api_request

async def main():
    parser=argparse.ArgumentParser();parser.add_argument('--unit',choices=['Sweeper','WarMachine'],required=True)
    args=parser.parse_args();unit=args.unit
    source=ROOT/'ArtSource/TacticalStyle_20260916/Concepts'
    output=ROOT/'ArtSource/TacticalStyle_20260916/Tripo';output.mkdir(exist_ok=True)
    record=output/(unit+'_multiview_task.json')
    if record.exists():
        previous=json.loads(record.read_text(encoding='utf8'))
        if previous.get('task_id'):
            print(json.dumps({'existing_task':previous['task_id']}));return
        if previous.get('submission_pending'):
            raise RuntimeError('An earlier submission has uncertain status; inspect Tripo before resubmitting.')
    files=[];inputs=[]
    for view in ('Front','Left','Back'):
        image=source/(unit+'_'+view+'.png');assert image.is_file(),image
        token_file=output/(unit+'_'+view+'_upload.json')
        if token_file.exists():uploaded=json.loads(token_file.read_text(encoding='utf8'))
        else:
            with image.open('rb') as stream:
                uploaded=await api_request('POST','/upload',files={'file':(image.name,stream,'image/png')})
            token_file.write_text(json.dumps(uploaded,indent=2),encoding='utf8')
        token=uploaded.get('image_token') or uploaded.get('file_token')
        if not token:raise RuntimeError('Upload returned no image token: '+str(list(uploaded)))
        files.append({'type':'png','file_token':token});inputs.append(str(image.relative_to(ROOT)))
        print('UPLOADED',unit,view,flush=True)
    files.append({})
    payload={'type':'multiview_to_model','model_version':'P1-20260311','files':files,
             'face_limit':4000 if unit=='Sweeper' else 6000,'texture':True,'pbr':True,
             'model_seed':20260916,'texture_seed':20260916,'texture_quality':'standard',
             'texture_alignment':'geometry','export_uv':True}
    note={'unit':unit,'inputs':inputs,'payload':payload,'submission_pending':True,
          'documentation':'https://docs.tripo3d.ai/model-generation/multiview-to-model-p1-20260311.html'}
    record.write_text(json.dumps(note,indent=2),encoding='utf8')
    task=await api_request('POST','/task',json=payload)
    note.update(task);note['submission_pending']=False
    record.write_text(json.dumps(note,indent=2),encoding='utf8')
    print(json.dumps({'task_id':task.get('task_id'),'unit':unit,'record':str(record)},ensure_ascii=False),flush=True)

if __name__=='__main__':asyncio.run(main())
