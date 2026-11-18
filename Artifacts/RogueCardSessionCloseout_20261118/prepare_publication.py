"""Prepare an isolated, explicit publication index without changing local HEAD/index."""
import ast
import hashlib
import json
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path
from urllib.parse import unquote

ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
SCRATCH=ROOT/'.codex-tmp/rogue-card-publish-20261118'
TEXT={'.md','.txt','.json','.py','.html','.css','.js','.csv','.yaml','.yml'}
BOOKS={'data/Excel/GuLiStrikeCardArtwork.xlsx','data/Excel/GuLiStrikeIcons.xlsx','data/Excel/GuLiStrikeRogueCards.xlsx'}
ROOTS=['.agents/skills/guli-rogue-card-production',
 'Artifacts/CardArtworkPipeline/EnhancedExplosive_20261117',
 'Artifacts/GroundRogueCardIcons_20261118',
 'ArtSource/UI/GroundMechRogueCards/EnhancedExplosive_Card_Production_20261117_v1',
 'ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1']
SCRIPTS=['Scripts/Cards/author_registered_six_material.py','Scripts/Cards/card_artwork_review.py',
 'Scripts/Cards/card_artwork_pipeline.py','Scripts/Cards/install_enhanced_explosive_artwork.py',
 'Scripts/Cards/register_enhanced_explosive_artwork.py','Scripts/Cards/author_ground_four_card_artwork.py',
 'Scripts/Cards/install_ground_four_card_artwork.py','Scripts/GroundMech/icon_assets.py',
 'Scripts/GroundMech/install_rogue_card_icons_20261118.py','Scripts/GroundMech/register_rogue_card_icons_20261118.py',
 'Tools/DataPipeline/card_artwork_catalog.py']
ARCHIVES=['20261117-强化爆炸弹导弹表面与轴线修订.md','20261117-强化爆炸弹正式牌面入库与图标清单.md',
 '20261117-强化爆炸弹地面光环断口补绘.md','20261118-游骑兵五张肉鸽牌独立图标接入.md',
 '20261118-游骑兵图标提示词研究与V2轴线修订.md','20261118-爆破套餐三枚导弹图标轴线修订.md',
 '20261118-游骑兵图标V2_1数据管线同步.md','20261118-强化爆炸弹与游骑兵图标会话清理结项.md']


def git(*args,data=None,env=None):
    p=subprocess.run(['git',*args],cwd=ROOT,input=data,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
    if p.returncode:raise RuntimeError(p.stderr.decode('utf-8','replace'))
    return p.stdout


def dump(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def main():
    assert not SCRATCH.exists(), 'Do not overwrite another publication workspace'
    SCRATCH.mkdir(parents=True)
    env=os.environ.copy();env['GIT_INDEX_FILE']=str(SCRATCH/'publish.index')
    base=git('rev-parse','origin/main').decode().strip()
    local_head=git('rev-parse','HEAD').decode().strip()
    index_path=Path(git('rev-parse','--git-path','index').decode().strip())
    if not index_path.is_absolute():index_path=ROOT/index_path
    index_sha=hashlib.sha256(index_path.read_bytes()).hexdigest()
    paths=set(SCRIPTS)|BOOKS|{'ArtSource/UI/GroundMech/Icons/catalog.json','data/Json/manifest.json',
        'Progress/DevelopmentDocumentation/20261118-强化爆炸弹牌面与游骑兵图标会话结项.md'}
    paths.update('Progress/Archive/'+name for name in ARCHIVES)
    for folder in ROOTS:
        paths.update(p.relative_to(ROOT).as_posix() for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix.lower() in TEXT)
    for table in ['GuLiStrikeIcons_Icons','GuLiStrikeRogueCards_Cards','GuLiStrikeCardArtwork_Artworks',
                  'GuLiStrikeCardArtwork_Templates','GuLiStrikeCardArtwork_Layers','GuLiStrikeCardArtwork_Parameters']:
        paths.update('data/Json/DT_'+table+ext for ext in ('.json','.csv'))
    paths.update(p.relative_to(ROOT).as_posix() for p in OUT.iterdir() if p.is_file() and
                 p.name in {'README.md','cleanup-report.json','prepare_publication.py'})
    remote={}
    for item in git('ls-tree','-rz',base).split(b'\0'):
        if not item:continue
        meta,name=item.split(b'\t',1);mode,kind,oid=meta.decode().split()
        remote[name.decode('utf-8')]=(mode,oid)
    # Include only missing Markdown dependencies of this session, not unrelated changed documents.
    added=[];queue=[p for p in paths if p.endswith('.md')];visited=set()
    while queue:
        relative=queue.pop()
        if relative in visited:continue
        visited.add(relative);doc=ROOT/relative
        for link in re.findall(r'\[[^\]]*\]\(([^)]+)\)',doc.read_text(encoding='utf-8-sig')):
            target=unquote(link.split('#')[0].strip('<>'))
            if not target.endswith('.md') or target.startswith(('http:','https:','mailto:')):continue
            p=(Path(target) if Path(target).is_absolute() else doc.parent/target).resolve()
            if not p.is_relative_to(ROOT) or not p.is_file():continue
            key=p.relative_to(ROOT).as_posix()
            if key not in paths and key not in remote:
                assert not key.startswith(('Content/','Saved/','Intermediate/','Binaries/','Downloads/','DerivedDataCache/'))
                paths.add(key);added.append(key);queue.append(key)
    payloads={}
    for relative in sorted(paths):
        p=ROOT/relative
        assert p.is_file(),relative
        assert not relative.startswith('Content/')
        assert p.suffix.lower() in TEXT or relative in BOOKS
        value=p.read_bytes()
        assert len(value)<5_000_000,relative
        if relative not in BOOKS:
            decoded=value.decode('utf-8-sig');assert '\0' not in decoded,relative
            assert not decoded.startswith('version https://git-lfs.github.com/spec/'),relative
            if p.suffix=='.py':ast.parse(decoded,filename=relative)
            if p.suffix=='.json':json.loads(decoded)
        payloads[relative]=value
    attr=git('show',base+':.gitattributes').decode('utf-8')
    rules=['.agents/skills/guli-rogue-card-production/references/prompts/ground-icons-20261118/*.txt -text',
        'ArtSource/UI/GroundMechRogueCards/EnhancedExplosive_Card_Production_20261117_v1/** -text',
        'ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1/** -text',
        'Artifacts/CardArtworkPipeline/EnhancedExplosive_20261117/** -text',
        'Artifacts/GroundRogueCardIcons_20261118/** -text',
        'data/Json/DT_GuLiStrikeIcons_Icons.* -text','data/Json/DT_GuLiStrikeRogueCards_Cards.* -text']
    payloads['.gitattributes']=(attr.rstrip()+'\n\n# Preserve this card/icon session\'s recorded source and evidence bytes.\n'+'\n'.join(r for r in rules if r not in attr.splitlines())+'\n').encode('utf-8')
    git('read-tree',base,env=env)
    for relative,value in payloads.items():
        oid=git('hash-object','-w','--stdin','--path='+relative,data=value,env=env).decode().strip()
        mode=remote.get(relative,('100644',''))[0]
        git('update-index','--add','--cacheinfo',mode,oid,relative,env=env)
    review=SCRATCH/'review'
    # Materialize Markdown and the index tool only; never check out resources.
    for item in git('ls-files','--stage','-z',env=env).split(b'\0'):
        if not item:continue
        meta,name=item.split(b'\t',1);mode,oid,stage=meta.decode().split();relative=name.decode('utf-8')
        if relative.startswith(('Content/','Saved/','Intermediate/','Binaries/','Downloads/','DerivedDataCache/')):continue
        if relative.endswith('.md') or relative.startswith('.agents/skills/gulistrike-progress/scripts/') and relative.endswith('.py'):
            destination=review/relative;destination.parent.mkdir(parents=True,exist_ok=True)
            destination.write_bytes(git('cat-file','blob',oid))
    subprocess.run([sys.executable,'-X','utf8',str(review/'.agents/skills/gulistrike-progress/scripts/progress_docs.py'),'build'],cwd=review,check=True,stdout=subprocess.PIPE)
    for p in (review/'Progress/_Index').iterdir():
        if p.is_file():
            relative=p.relative_to(review).as_posix();value=p.read_bytes()
            oid=git('hash-object','-w','--stdin','--no-filters',data=value).decode().strip()
            git('update-index','--add','--cacheinfo','100644',oid,relative,env=env)
            payloads[relative]=value
    changed=git('diff','--cached','--name-only','-z',base,env=env).decode('utf-8').split('\0')
    changed=[p for p in changed if p]
    plan=dict(base_commit=base,local_head_preserved=local_head,local_index_sha256=index_sha,
        branch='codex/rogue-card-closeout-20261118',scratch=str(SCRATCH),index=str(SCRATCH/'publish.index'),
        destination='origin/main',excel_configuration_sources=sorted(BOOKS),missing_markdown_dependencies=added,
        files=[dict(path=p,bytes=len(payloads[p]),sha256=hashlib.sha256(payloads[p]).hexdigest()) for p in changed],
        resource_files_excluded=True,lfs_objects_added=0,
        source_tree='local session files plus prerequisite card pipeline; remote-only Markdown dependencies preserved',
        checks=dict(python_ast=True,json_parse=True,resource_path_and_extension_guard=True))
    dump(OUT/'publication-plan.json',plan)
    plan_path=(OUT/'publication-plan.json').relative_to(ROOT).as_posix()
    oid=git('hash-object','-w','--stdin','--no-filters',data=(OUT/'publication-plan.json').read_bytes()).decode().strip()
    git('update-index','--add','--cacheinfo','100644',oid,plan_path,env=env)
    assert git('rev-parse','HEAD').decode().strip()==local_head
    assert hashlib.sha256(index_path.read_bytes()).hexdigest()==index_sha
    print(json.dumps(dict(base=base,files=len(changed)+1,bytes=sum(x['bytes'] for x in plan['files']),
        by_extension=dict(Counter(Path(p).suffix for p in changed)),markdown_dependencies=added,
        resource_files=0,local_head_and_index_preserved=True),ensure_ascii=False))


if __name__=='__main__':main()
