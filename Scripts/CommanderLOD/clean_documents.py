"""Apply the user's explicit LOD erratum, retaining original text in evidence ZIP."""
import hashlib,json,re,zipfile,os
from pathlib import Path
from common import ROOT,ART
OLD=re.compile(r'(?:四|4)\s*(?:档|级).{0,16}LOD|LOD.{0,16}(?:四|4)\s*(?:档|级)|LOD\s*3(?!\d)|LOD\s*0\s*[–—~～/-]\s*3|LOD0\s*[/、]\s*1\s*[/、]\s*2\s*[/、]\s*3|LOD1\s*[/、]\s*2\s*[/、]\s*3|四\s*LOD|四档.{0,8}(?:静态|模型|FBX|骨骼|顶点)|三档远景|主模型.{0,20}(?:另外|另做|另有).{0,12}三档')
OLD=re.compile(OLD.pattern+r'|LOD\s*1\s*[–—~～-]\s*3')
TARGET=ROOT/'Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md'
paths=[]
for directory in ['Progress/RequirementDocument','Progress/DevelopmentDocumentation','Progress/Archive','Progress/Gameplay',
        'ArtSource','.agents/skills/guli-model-production/references']:
    for folder,dirs,names in os.walk(ROOT/directory):
        dirs[:]=[name for name in dirs if name not in {'Intermediate','Saved','Binaries','DerivedDataCache','Downloads'}]
        paths.extend(Path(folder)/name for name in names if Path(name).suffix in ['.md','.html'])
changes=[];backup=ART/'Reports/document_text_before_erratum.zip'
ART.joinpath('Reports').mkdir(parents=True,exist_ok=True)
archive=zipfile.ZipFile(backup,'a',compression=zipfile.ZIP_DEFLATED)
saved=set(archive.namelist())
for path in sorted(set(paths)):
    if ART in path.parents:continue
    raw=path.read_bytes();text=raw.decode('utf-8-sig')
    if not OLD.search(text):continue
    relative=path.relative_to(ROOT).as_posix()
    link=os.path.relpath(TARGET,path.parent).replace('\\','/')
    note=f'指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移]({link})。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。'
    if path.suffix=='.html':
        if 'ArtSource/FX/' in relative:
            # A VFX page can mention the unit LODs without being an LOD review.
            replacement=re.sub(r'四级导弹仓\s*LOD', '导弹仓网格（当前指挥官模型总共三档）',text)
        else:
        # Remove obsolete navigation while retaining every original image/source file.
            replacement='<!doctype html><html lang="zh"><meta charset="utf-8"><title>指挥官三档 LOD · 当前入口</title><body style="font-family:system-ui;background:#d5c09c;color:#2c3735;padding:40px"><h1>当前指挥官模型：LOD0／LOD1／LOD2</h1><p>此历史页面已撤出制作入口。源文件与原始回读保留为历史证据。</p><p><a href="http://127.0.0.1:8709/Review/index.html">打开三档候选与审核记录</a></p><p>新成品版本 B 待审核；正式资源尚未切换。</p></body></html>\n'
    else:
        lines=text.splitlines();result=[];i=0;removed=[];front=False
        while i<len(lines):
            line=lines[i]
            if i==0 and line=='---':front=True;result.append(line);i+=1;continue
            if front and line=='---':front=False;result.append(line);i+=1;continue
            if line.lstrip().startswith('|'):
                end=i
                while end<len(lines) and lines[end].lstrip().startswith('|'):end+=1
                block=lines[i:end]
                four_rows=all(any(re.match(r'^\|\s*'+str(n)+r'\s*\|',row) for row in block) for n in range(4))
                if OLD.search('\n'.join(block)) or (four_rows and re.search(r'LOD|档位|本体|三角', '\n'.join(block))):
                    removed.append(dict(line=i+1,text='\n'.join(block)))
                    result.extend([note,'']);i=end;continue
            if OLD.search(line):
                removed.append(dict(line=i+1,text=line))
                if front:
                    key=line.split(':',1)[0]
                    if key in ['summary','status_note','next_action']:
                        result.append(key+': 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。')
                    else:result.append(line)
                else:
                    # LOD paragraphs are replaced; all other historical paragraphs remain byte-for-byte.
                    result.append(note)
                i+=1;continue
            result.append(line);i+=1
        replacement='\n'.join(result)+'\n'
        replacement+=f'\n> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单]({os.path.relpath(ART/"Reports/document_erratum.json",path.parent).replace(chr(92),"/")})。冻结模型及原始机器回读不作为当前制作入口。\n'
        assert not OLD.search(replacement),relative
    if relative not in saved:archive.writestr(relative,raw);saved.add(relative)
    path.write_text(replacement,encoding='utf8',newline='\n')
    changes.append(dict(path=relative,before_sha256=hashlib.sha256(raw).hexdigest(),
        after_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),authority='2026-10-05 user explicitly requests correction of related archive paragraphs',
        old_lod_mentions=len(OLD.findall(text))))
archive.close()
manifest=ART/'Reports/document_erratum.json'
previous=json.loads(manifest.read_text(encoding='utf8')) if manifest.exists() else []
manifest.write_text(json.dumps(previous+changes,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(dict(changed=len(changes),backup=str(backup)),ensure_ascii=False))
