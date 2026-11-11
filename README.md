# nornHTTP

Standalone C99 HTTP/1.1 client for nornRTOS and other embedded environments.
No RTOS, socket, TLS or third-party parser dependency in the core. No heap,
mutable global state, recursion or internal receive buffers. Licensed under the
nornRTOS license in [LICENSE.md](LICENSE.md).

Implemented: GET, POST, PUT, DELETE, PATCH, HEAD and OPTIONS; custom headers;
binary request bodies, contiguous or streamed from a source callback with a known
length; incremental response parsing; HTTP/1.1 and HTTP/1.0
responses; informational responses;
Content-Length, chunked and connection-close framing; trailers; streaming;
closed byte ranges; keep-alive; and a synchronous transport API with a total
transaction deadline. A nornNET adapter is optional and lives in this module.

## Build and verify

```sh
make                         # build/libnornHTTP.a
make test                    # Linux tests with ASan + UBSan
make fuzz-smoke               # 100,000 deterministic parser mutations
make analyze                  # GCC static analyzer
make integration              # tools/demo_server.py as HTTP/1.1 and HTTP/1.0, no Internet
make adapter-test             # nornNET API mapping, sibling nornNET headers
make size
make esp32-check              # ESP compiler on PATH, or set ESP_CC and ESP_SIZE
```

If LeakSanitizer cannot run because the environment uses ptrace, run tests with
`ASAN_OPTIONS=detect_leaks=0`; address and undefined-behaviour checks remain active.
Coverage-guided fuzzing needs Clang: `make fuzz` builds `build/fuzz_parser` (the
response parser; the first input byte selects HEAD, range, close and a small body
limit) and `build/fuzz_request` (the request builder, checking that everything it
accepts is a well-formed request head). `make fuzz-run FUZZ_SECONDS=3600` runs both
on a copy of `tests/corpus`, and `make fuzz-merge` folds the new entries back.
CMake is also provided:

```sh
cmake -S . -B build/cmake -DNORN_HTTP_TESTS=ON -DNORN_HTTP_SANITIZE=ON
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

To embed, compile `src/*.c` with `include/` on the include path, or link the
`nornHTTP` CMake target. Parser-only applications may compile just
`src/norn_http_parser.c`; the static archive also supports selective linking.
Nothing in nornCore, nornNET or nornWIFI needs changing.

## Use

The application first opens a connection and supplies `send`, `recv` and a
monotonic millisecond clock in `norn_http_transport`. Example (inside application
code, with an established `transport`):

```c
char line[512], request_bytes[1024];
uint8_t receive[1024], body[256];
norn_http_body_buffer output = { body, sizeof(body), 0 };
norn_http_config config = {0};
norn_http http;
norn_http_request request;
norn_http_response response;
norn_http_header headers[4];

norn_http_parser_config_init(&config.parser, line, sizeof(line));
config.parser.on_body = norn_http_buffer_body;
config.parser.callback_context = &output;
config.receive_buffer = receive;
config.receive_capacity = sizeof(receive);
config.request_buffer = request_bytes;
config.request_capacity = sizeof(request_bytes);
config.timeout_ms = 30000;
int result = norn_http_init(&http, &config, &transport);
if (result == NORN_HTTP_OK) {
    norn_http_request_init(&request, "GET", "/api/items", "example.org", headers, 4);
    result = norn_http_execute(&http, &request, &response);
}
/* Inspect result and HTTP status separately. body is binary, not NUL terminated.
 * Close/abort the connection on errors or when response.reusable is false. After
 * an error the context returns NORN_HTTP_STALE until norn_http_init runs again. */
```

For REST POST, use `norn_http_add_header(&request, "Content-Type",
"application/json")` and `norn_http_set_body(&request, json, json_length)`.
For large downloads, replace `on_body` with a callback that writes each byte
span to storage; no full-body buffer is needed. For a large upload, give
`norn_http_set_body_source` a callback that fills the request buffer span by span
and the total length; the body never has to be in RAM as a whole. Check every API
return value.
The runnable [POSIX example](examples/posix_client.c) demonstrates GET, JSON POST,
Range and streaming output, and is used by the integration test against
`tools/demo_server.py`, which also serves the ESP32 example in nornRTOS
(`examples/wifi_http`). The road to the first stable version is in
[docs/plan.md](docs/plan.md).

For an application-driven event loop, call `norn_http_parser_init`, then
`norn_http_parser_feed` for each received span. `MORE` means more input is
needed; `OK` means exactly one final response completed. Preserve bytes after
`consumed` for the next message. Call `norn_http_parser_eof` on orderly EOF.
The parser performs no network operations and does not implement its own clock;
the event loop must enforce its deadline.

For nornNET, compile `adapters/norn_net_transport.c` with your existing nornNET
configuration and OS include paths. Create `norn_http_net_context` containing an
already connected `nn_tcp_t`, a clock callback and its argument, then call
`norn_http_net_transport_init`. Do not call blocking execute from nornNET's
network task or socket event callback. See [contracts](docs/contracts.md).

## Validation status

Linux unit, sanitizer, deterministic mutation, one hour of coverage-guided fuzzing
per harness, adapter mapping and local-server tests have passed. On a real ESP32
the nornRTOS example `examples/wifi_http` runs GET, JSON POST, a 64 KiB streamed
download and keep-alive over nornWIFI and nornNET against `tools/demo_server.py`;
linked there, HTTP costs 6.9 KB of flash (about 5.3 KB library), no static RAM
beyond the 184-byte context, and at most 160 bytes more task stack than the Wi-Fi
join and TCP connect already use. See [measurements](docs/measurements.md) for
sizes, coverage and limitations.

TLS communication is still needed before declaring the design document's complete
stable-release criteria met; the steps are in [docs/plan.md](docs/plan.md).
A streamed request body must have a known length; chunked upload
(Transfer-Encoding: chunked on the request) is left for a later version if an
appliance needs it. No redirects, compression, cookies, HTTP/2, server, DNS or TLS
engine.
