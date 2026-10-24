"""Loopback-only file service; opening the designer never contacts Unreal."""
from __future__ import annotations

import argparse
import json
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
import traceback
from urllib.parse import unquote, urlparse

from domain import density_statistics, validate
import storage

APP=Path(__file__).resolve().parent
LOCK=threading.RLock()
MAX_BODY=256*1024*1024


class Handler(BaseHTTPRequestHandler):
    server_version="GuLiMapDesigner/1.0"

    def log_message(self,fmt,*args):
        if args and str(args[0]).startswith("/api/health"):return
        super().log_message(fmt,*args)

    def send(self,status,value,content_type="application/json; charset=utf-8",headers=None):
        data=json.dumps(value,ensure_ascii=False,allow_nan=False,separators=(",",":")).encode("utf-8") if not isinstance(value,bytes) else value
        self.send_response(status);self.send_header("Content-Type",content_type);self.send_header("Content-Length",str(len(data)))
        self.send_header("Cache-Control","no-store" if self.path.startswith("/api/") else "no-cache")
        self.send_header("X-Content-Type-Options","nosniff");self.send_header("Referrer-Policy","no-referrer")
        for key,item in (headers or {}).items():self.send_header(key,item)
        self.end_headers()
        try:self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError):pass

    def body(self,binary=False):
        length=int(self.headers.get("Content-Length","0"))
        if not 0<length<=MAX_BODY:raise ValueError("请求长度无效或超过256MiB")
        data=self.rfile.read(length)
        if len(data)!=length:raise ValueError("请求未完整接收")
        return data if binary else json.loads(data,parse_constant=lambda v:(_ for _ in ()).throw(ValueError("数字无效: "+v)))

    def trusted(self):
        host=self.headers.get("Host","")
        if host not in {f"127.0.0.1:{self.server.server_port}",f"localhost:{self.server.server_port}"}:raise PermissionError("服务仅接受本机地址")
        origin=self.headers.get("Origin")
        if origin and origin not in {"http://"+host}:raise PermissionError("请求来源不匹配")

    def route(self,method):
        try:
            self.trusted();path=unquote(urlparse(self.path).path);parts=path.strip("/").split("/")
            if path=="/api/health":return self.send(200,{"ok":True,"app":"guli-map-designer","version":"1.0.0","data_directory":str(storage.DATA)})
            if method=="GET" and path=="/api/projects":return self.send(200,{"projects":storage.project_list()})
            if method=="GET" and len(parts)==3 and parts[:2]==["api","projects"]:
                with LOCK:value=storage.open_project(parts[2])
                return self.send(200,value)
            if method=="GET" and len(parts)==4 and parts[:2]==["api","projects"] and parts[3]=="download":
                with LOCK:p=storage.open_project(parts[2])["project"];data=storage.bundle(p)
                return self.send(200,data,"application/zip",{"Content-Disposition":f'attachment; filename="{p["id"]}.gmap"'})
            if method=="POST" and path=="/api/import":
                with LOCK:p=storage.unpack(self.body(True));saved=storage.save(p)
                return self.send(200,{"project":p,**saved})
            if method=="POST" and path=="/api/import-snapshot":
                request=self.body()
                with LOCK:value=storage.import_snapshot(storage.SNAPSHOTS/storage.ident(request["snapshot_id"]))
                return self.send(200,value)
            if method=="GET" and path=="/api/snapshots":
                result=[]
                for directory in storage.SNAPSHOTS.iterdir():
                    if directory.is_dir() and storage.valid_guid(directory.name) and (directory/"manifest.json").is_file():
                        m=json.loads((directory/"manifest.json").read_text(encoding="utf-8"));result.append({"id":directory.name,"map_package":m["map_package"],"captured_at":m.get("captured_at",""),"resolution":m.get("resolution"),"warnings":m.get("warnings",[])})
                return self.send(200,{"snapshots":sorted(result,key=lambda v:v["captured_at"],reverse=True)})
            if method=="PUT" and len(parts)==3 and parts[:2]==["api","projects"]:
                request=self.body();p=request["project"]
                if p["id"]!=parts[2]:raise ValueError("工程身份不一致")
                with LOCK:value=storage.save(p,bool(request.get("autosave")),request.get("revision"))
                return self.send(200,value)
            if method=="POST" and path=="/api/validate":
                p=self.body()["project"];issues=validate(p)
                density,warnings=(None,[]) if any(v["severity"]=="error" for v in issues) else density_statistics(p)
                return self.send(200,{"issues":issues+warnings,"territory_summary":density.get("territory_summary",[]) if density else [],"anomalies":density.get("anomalies",[]) if density else []})
            if method=="POST" and path=="/api/export":
                p=self.body()["project"]
                with LOCK:value=storage.export_project(p)
                return self.send(200,value)
            if method!="GET":return self.send(404,{"error":"接口不存在"})
            root=(APP/"dist").resolve();candidate=(root/path.lstrip("/")).resolve()
            if not candidate.is_relative_to(root):raise PermissionError("路径越界")
            if candidate.is_dir():candidate=candidate/"index.html"
            if not candidate.is_file():return self.send(404,{"error":"前端文件未构建，请运行 start.cmd"})
            return self.send(200,candidate.read_bytes(),mimetypes.guess_type(candidate.name)[0] or "application/octet-stream")
        except FileNotFoundError as exc:self.send(404,{"error":str(exc)})
        except FileExistsError as exc:self.send(409,{"error":str(exc)})
        except PermissionError as exc:self.send(403,{"error":str(exc)})
        except (ValueError,KeyError,TypeError,OverflowError) as exc:self.send(400,{"error":str(exc)})
        except Exception as exc:
            traceback.print_exc();self.send(500,{"error":str(exc)})

    def do_GET(self):self.route("GET")
    def do_POST(self):self.route("POST")
    def do_PUT(self):self.route("PUT")


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--port",type=int,default=4321);args=parser.parse_args()
    storage.init_storage();server=ThreadingHTTPServer(("127.0.0.1",args.port),Handler)
    print(f"GuLi Map Designer: http://127.0.0.1:{args.port}",flush=True)
    server.serve_forever()


if __name__=="__main__":main()
