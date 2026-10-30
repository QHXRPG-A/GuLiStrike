"""Copy one real imagegen result into the project and record its provenance."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import hashlib
import json
import shutil
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'ArtSource/UI/GroundMechRogueCards/BurningCards_HTML_20261029_v1'
REPAIR = OUT/'Repairs'/('RigidObjects_v4' if len(sys.argv)>3 and sys.argv[3]=='v4' else 'CompleteLayers_v3')


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    request_id, source_name = sys.argv[1:3]
    request = next(r for r in json.loads((REPAIR/'requests.json').read_text(encoding='utf-8')) if r['id']==request_id)
    source = Path(source_name)
    assert source.is_file() and source.suffix.lower()=='.png'
    destination = Path(request['output'])
    assert destination.resolve().is_relative_to(REPAIR.resolve())
    for path, digest in zip(request['referenced_image_paths'], request['input_sha256']):
        assert sha(path)==digest, 'Editing input changed during generation'
    with Image.open(source) as image:
        image.load()
        alpha = image.convert('RGBA').getchannel('A').getextrema()
        assert alpha==((255,255) if request['index']==6 else (0,255)), (request_id,alpha)
        size=list(image.size);mode=image.mode
    if destination.is_file():assert sha(destination)==sha(source), 'Do not overwrite a different raw generation'
    else:shutil.copy2(source,destination)
    records_path=REPAIR/'results.json'
    records=json.loads(records_path.read_text(encoding='utf-8')) if records_path.is_file() else []
    record={'id':request_id,'source':str(source),'output':destination.as_posix(),
        'size':size,'mode':mode,'alpha_extrema':list(alpha),'sha256':sha(destination),
        'status':'active','revision':request.get('revision',3),'tool':'built-in imagegen',
        'prompt_file':request['prompt_file'],'input_sha256':request['input_sha256'],
        'recorded_at':datetime.now(timezone(timedelta(hours=8))).isoformat(),
        'visual_review':'pending_user'}
    previous=[r for r in records if r['id']==request_id]
    if previous:assert previous[0]['sha256']==record['sha256']
    else:records.append(record)
    records_path.write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'id':request_id,'copied':destination.as_posix(),'size':size,'alpha':alpha,'recorded':len(records)},ensure_ascii=False))


if __name__=='__main__':main()
