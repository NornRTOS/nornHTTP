"""Local HTTP/1.1 and HTTP/1.0 interoperability test with tools/demo_server.py."""
from http.server import ThreadingHTTPServer
from pathlib import Path
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from demo_server import Handler, Http10Handler, LARGE_DEFAULT  # noqa: E402

EXECUTABLE = ROOT / "build" / "posix_client"
CASES = [
    ("/large", "GET", bytes(i & 0xFF for i in range(LARGE_DEFAULT)), True),
    ("/large?n=2097152", "GET", bytes(i & 0xFF for i in range(2097152)), False),
    ("/chunked", "GET", b"abcde", False),
    ("/range", "GET", b"234", False),
    ("/api", "POST", b'{"item":42}', True),
    ("/hello", "GET", b'{"message":"Hello from the test server!"}', True),
]


def run(handler, cases):
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        for path, method, expected, _ in cases:
            result = subprocess.run([str(EXECUTABLE), "127.0.0.1", str(server.server_port), path, method],
                                    capture_output=True, check=True, timeout=30)
            assert result.stdout == expected, (handler.protocol_version, path, result.stderr)
            reusable = b"reusable=1" in result.stderr
            assert reusable == (handler.protocol_version == "HTTP/1.1"), (path, result.stderr)
            print(f"PASS {handler.protocol_version} {method} {path}: {len(expected)} bytes")
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


run(Handler, CASES)
run(Http10Handler, [c for c in CASES if c[3]])     # HTTP/1.0 cannot chunk or serve 206 here
