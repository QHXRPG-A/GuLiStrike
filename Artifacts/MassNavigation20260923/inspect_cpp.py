"""Static grammar inspection only; does not build, execute game code, or run tests."""
import sys,re,json,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'static_parser'))
import tree_sitter,tree_sitter_cpp
parser=tree_sitter.Parser(tree_sitter.Language(tree_sitter_cpp.language()))
base=Path(__file__).parent/'baseline'
paths=[]
for folder in ['Source/GuLiStrike/Commander','Source/GuLiStrike/Gameplay/Navigation','Source/GuLiStrike/Gameplay/Stronghold']:
 for p in Path(folder).rglob('*'):
  if p.suffix in ['.cpp','.h'] and '/Tests/' not in p.as_posix():
   old=base/p
   if not old.exists() or old.read_bytes()!=p.read_bytes(): paths.append(p)
def sanitize(src):
 src=re.sub(r'^#include UE_INLINE_GENERATED_CPP_BY_NAME.*$', '', src, flags=re.M)
 src=re.sub(r'^#(?:if|ifdef|ifndef|endif|else|elif).*$', '', src, flags=re.M)
 src=src.replace('enum : uint16','enum InspectionMask : uint16')
 src=re.sub(r'\b[A-Z][A-Z0-9_]*_API\b',lambda m:' '*len(m[0]),src)
 pattern=r'\b(?:UCLASS|USTRUCT|UENUM|UPROPERTY|UFUNCTION|GENERATED_BODY|ENUM_CLASS_FLAGS|DECLARE_[A-Za-z0-9_]+|DEFINE_[A-Z0-9_]+|CSV_DECLARE_[A-Za-z0-9_]+|CSV_DEFINE_[A-Z0-9_]+|UE_INLINE_GENERATED_CPP_BY_NAME)\s*\('
 for m in list(re.finditer(pattern,src))[::-1]:
  i=m.end(); depth=1
  while i<len(src) and depth:
   if src[i]=='(': depth+=1
   elif src[i]==')': depth-=1
   i+=1
  src=src[:m.start()]+''.join('\n' if c=='\n' else ' ' for c in src[m.start():i])+src[i:]
 return src
out=[]
trees=[]
for p in paths:
 print(str(p),flush=True)
 data=sanitize(p.read_text(encoding='utf-8-sig')).encode()
 tree=parser.parse(data); trees.append(tree); errors=[]
 def visit(n):
  if n.type=='ERROR' or n.is_missing: errors.append({'line':n.start_point.row+1,'type':n.type,'source':data[n.start_byte:n.end_byte].decode()[:150]})
  for c in n.children: visit(c)
 visit(tree.root_node)
 out.append({'file':p.as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'errors':errors})
print('parsed',len(out),flush=True)
Path(__file__).with_name('static-grammar.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'files':len(out),'issues':[x for x in out if x['errors']]},ensure_ascii=False,indent=2))
