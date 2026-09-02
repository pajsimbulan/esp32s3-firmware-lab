import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

class H(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0))
        raw = self.rfile.read(n)
        try:
            obj = json.loads(raw); ok = True
        except Exception:
            obj = raw.decode('utf-8', 'replace'); ok = False
        print(f"{self.client_address[0]} {self.path}  {obj}", flush=True)
        body = json.dumps({"ok": ok}).encode()
        self.send_response(200 if ok else 400)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)
    def log_message(self, *a): pass

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    print(f"telemetry receiver on :{port}  (ctrl-c to stop)", flush=True)
    HTTPServer(('0.0.0.0', port), H).serve_forever()