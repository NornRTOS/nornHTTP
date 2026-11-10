#!/usr/bin/env python3
"""HTTP/1.1 demo server for nornHTTP: Python standard library only.

Serves the paths the examples and tests use:
  GET  /hello          small JSON body
  GET  /chunked        chunked body with an extension, whitespace after a size, a trailer
  GET  /range          206 for Range: bytes=2-4 of a ten-byte resource
  GET  /large[?n=N]    N bytes (default 65536) where byte i is i & 0xFF
  POST /api            echoes the JSON body with status 201

tests/integration.py imports Handler and binds an ephemeral port; run this file to
serve the ESP32 example (default port 18080 on every interface).
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

LARGE_DEFAULT = 65536
CHUNKED_BODY = b'3 ;name="value"\r\nabc\r\n2 \r\nde\r\n0\r\nX-Checksum: ok\r\n\r\n'
RANGE_RESOURCE = b"0123456789"


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def reply(self, status, body, content_type="application/octet-stream"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path == "/hello":
            self.reply(200, b'{"message":"Hello from the test server!"}', "application/json")
        elif url.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for b in CHUNKED_BODY:          # one byte per write: fragments every boundary
                self.wfile.write(bytes([b]))
                self.wfile.flush()
        elif url.path == "/range":
            if self.headers.get("Range") != "bytes=2-4":
                self.reply(200, RANGE_RESOURCE)
                return
            self.send_response(206)
            self.send_header("Content-Range", "bytes 2-4/%d" % len(RANGE_RESOURCE))
            self.send_header("Content-Length", "3")
            self.end_headers()
            self.wfile.write(RANGE_RESOURCE[2:5])
        elif url.path == "/large":
            n = int(parse_qs(url.query).get("n", [LARGE_DEFAULT])[0])
            self.reply(200, bytes(i & 0xFF for i in range(n)))
        else:
            self.reply(404, b'{"error":"not found"}', "application/json")

    def do_POST(self):
        payload = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        if self.path == "/api":
            self.reply(201, payload, self.headers.get("Content-Type", "application/json"))
        else:
            self.reply(404, b'{"error":"not found"}', "application/json")


class Http10Handler(Handler):
    """The same paths spoken by an HTTP/1.0 server: no chunking, close after the response."""
    protocol_version = "HTTP/1.0"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--http10", action="store_true", help="answer as an HTTP/1.0 server")
    args = parser.parse_args()
    handler = Http10Handler if args.http10 else Handler
    with ThreadingHTTPServer((args.bind, args.port), handler) as server:
        print("nornHTTP demo server on http://%s:%d/hello (%s)"
              % (args.bind, server.server_port, handler.protocol_version), flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
