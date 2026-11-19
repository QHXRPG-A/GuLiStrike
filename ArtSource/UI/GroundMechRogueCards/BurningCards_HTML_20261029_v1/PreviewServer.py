"""Read-only local preview server; no image generation or gameplay endpoint."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import logging

ROOT=Path(__file__).resolve().parent
logging.basicConfig(filename=ROOT/'preview-server.log',encoding='utf-8',level=logging.INFO,format='%(asctime)s %(message)s')


class Handler(SimpleHTTPRequestHandler):
    def __init__(self,*args,**kwargs):super().__init__(*args,directory=str(ROOT),**kwargs)
    def log_message(self,fmt,*args):logging.info(fmt,*args)
    def do_GET(self):
        if self.path=='/_health':
            manifest=json.loads((ROOT/'manifest.json').read_text(encoding='utf-8'))
            payload=json.dumps({'ready':True,'cards':2,'layers':sum(len(c['layers']) for c in manifest['cards'].values())}).encode('utf-8')
            self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
        else:super().do_GET()
    def end_headers(self):
        self.send_header('Cache-Control','no-cache')
        super().end_headers()


if __name__=='__main__':
    logging.info('Starting card preview at http://127.0.0.1:8192/')
    ThreadingHTTPServer(('127.0.0.1',8192),Handler).serve_forever()
