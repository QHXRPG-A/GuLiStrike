"""Parse only reflection-exported Unreal struct data; never execute it."""
import json
def parts(text):
 out=[];start=0;depth=0;quoted=False;escape=False
 for i,c in enumerate(text):
  if quoted:
   if escape:escape=False
   elif c=='\\':escape=True
   elif c=='"':quoted=False
  elif c=='"':quoted=True
  elif c=='(':depth+=1
  elif c==')':depth-=1
  elif c==',' and depth==0:out.append(text[start:i]);start=i+1
 out.append(text[start:]);return out
def parse(text):
 s=text.strip()
 if s.startswith('(') and s.endswith(')'):
  items=parts(s[1:-1])
  if any('=' in p and not p.startswith('(') for p in items):return {p.split('=',1)[0]:parse(p.split('=',1)[1]) for p in items if '=' in p}
  return [parse(p) for p in items if p]
 if s.startswith('"'):return json.loads(s)
 if s in ('True','False'):return s=='True'
 if not s:return None
 try:return float(s)
 except ValueError:return s
def split(text,key):
 root=parts(text[1:-1])
 return next((p.split('=',1)[1] for p in root if p.startswith(key+'=')),None)
