# nornHTTP

Standalone C99 HTTP/1.1 client for nornRTOS and other embedded environments.
No RTOS, socket, TLS or third-party parser dependency in the core. No heap,
mutable global state, recursion or internal receive buffers. Licensed under the
nornRTOS license in [LICENSE.md](LICENSE.md).

Implemented: GET, POST, PUT, DELETE, PATCH, HEAD and OPTIONS; custom headers;
binary request bodies in RAM or streamed from a source callback; incremental
response parsing; HTTP/1.1 and HTTP/1.0
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
For coverage-guided fuzzing with Clang: `make fuzz` builds `build/fuzz_parser`
(the response parser, seeded from `tests/corpus`) and `build/fuzz_request` (the
request builder: method, target, host, headers, body and capacity from the input);
run one with `-max_total_time=<s>` and a writable corpus copy. CMake is also provided:

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
```

On nornNET the adapter has the common case in two steps: `NORN_HTTP_BUFFERS(config,
lineLen, rxLen, reqLen)` declares the three buffers and a `norn_http_config` over them
with the defaults (change a field before use if needed), and `norn_http_net_simple(&http,
&config, &transport, &net, &socket, &output)` fills the transport over the connected
socket with nornNET's clock, sets the body buffer as the sink and runs `norn_http_init`.
The library stays buffer-free; an application that does not use the two links nothing of
them.

```c
NORN_HTTP_BUFFERS(config, 256, 512, 256);
norn_http_body_buffer output = { body, sizeof(body), 0 };
norn_http_net_context net; norn_http_transport transport;
int result = norn_http_net_simple(&http, &config, &transport, &net, &socket, &output);
if (result == NORN_HTTP_OK) {
    norn_http_request_init(&request, "GET", "/api/items", "example.org", headers, 4);
    result = norn_http_execute(&http, &request, &response);
}
/* Inspect result and HTTP status separately. body is binary, not NUL terminated.
 * Close/abort the connection on errors or when response.reusable is false. After
 * an error the context returns NORN_HTTP_STALE until norn_http_init runs again. */
```

For REST POST, use `norn_http_add_header(&request, "Content-Type",
"application/json")` and `norn_http_set_body(&request, json, json_length)`, or
`norn_http_set_body_source(&request, source, context, length)` when the body is
not in RAM as a whole: the source fills the request buffer span by span while the
head announces the length. For large downloads, replace `on_body` with a callback
that writes each byte span to storage; no full-body buffer is needed. Check every
API return value.
The runnable [POSIX example](examples/posix_client.c) demonstrates GET, JSON POST,
Range and streaming output, and is used by the integration test against
`tools/demo_server.py`, which also serves the ESP32 example in nornRTOS
(`examples/wifi_http`). The road to the first stable version is in
nornInternal/nornHTTP/plan.md.

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

Linux (GCC 14, Clang 18): unit tests with ASan and UBSan, 100,000 deterministic
parser mutations, the GCC analyzer, the nornNET adapter mapping, and the local
server as HTTP/1.1 and HTTP/1.0 (64 KiB and 2 MiB downloads, bytewise chunked,
Range, JSON POST streamed from a source); libFuzzer on the response parser and on
the request builder without a finding (run times in
nornInternal/nornHTTP/measurements.md).

ESP32 (NodeMCU-32S, nornCore with nornNET and nornWIFI, `examples/wifi_http` in
nornRTOS, 2026-11-19): GET, a JSON POST, a 64 KiB download streamed through a
512-byte receive buffer with every byte checked, and keep-alive across the four
requests on one TCP connection, then Connection: close. Linked size, static RAM
and the task's stack high-water are in the measurements.

Not provided: TLS (a transport the application would supply; none has been
written or run), redirects, compression, cookies, HTTP/2, a server, DNS, and
chunked request bodies.
