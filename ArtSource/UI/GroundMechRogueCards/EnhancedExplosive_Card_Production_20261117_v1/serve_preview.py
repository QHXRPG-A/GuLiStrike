from functools import partial
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

if __name__ == '__main__':
    root = Path(__file__).resolve().parent
    handler = partial(QuietHandler, directory=str(root))
    ThreadingHTTPServer(('127.0.0.1', 8197), handler).serve_forever()
