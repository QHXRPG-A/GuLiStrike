import urllib.request, json, sys
from pathlib import Path
sys.stdout.reconfigure(encoding="utf-8")
code = sys.stdin.read() if sys.argv[1] == '-' else Path(sys.argv[1]).read_text(encoding='utf-8')
request = urllib.request.Request('http://127.0.0.1:8088/mcp', data=json.dumps({'jsonrpc':'2.0','id':2,'method':'tools/call','params':{'name':'execute_python_code','arguments':{'code':'import unreal\n'+code}}}).encode(),headers={'Content-Type':'application/json'})
print(urllib.request.urlopen(request,timeout=55).read().decode()[:5000])
