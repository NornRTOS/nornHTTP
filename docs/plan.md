# Plan to the first stable version

Measured against the Definition of Done in `local_docs/design.txt`, section 12, on
2026-11-10. Items 1–5 (methods, fragmented framing, chunked and Content-Length,
streaming with small caller buffers, no heap) are met by the Linux tests. The steps
below close items 6–10. Each step says which item it closes and what it needs.

| step | what | closes | needs | status |
|---|---|---|---|---|
| 1 | housekeeping: repository form, demo server in the repo, sanitised wifi_http README, third-party licence file | — | decision on the repository | done 2026-11-10, repository decision open |
| 2 | interoperability fixes with tests, poisoned context after an error | 6 | — | done 2026-11-10 |
| 3 | coverage-guided fuzzing with libFuzzer, request-builder harness | 6 | `sudo apt install clang` | open |
| 4 | run on a real ESP32: REST example with GET, POST, streamed GET and keep-alive over nornNET | 7, 8 | the board on /dev/ttyUSB0 | open |
| 5 | measurements on the target: linked flash with libgcc helpers, stack high-water, static RAM | 9 | step 4 | open |
| 6 | decide request-body streaming: implement or document as deferred | 10 | decision | open |
| 7 | documentation: contracts, README validation status, nornDocs chapter, Doxygen comments | 10 | steps 2–6 | open |
| 8 | final host suite, examples build, tag v0.1.0, point the submodule | — | steps 1–7 | open |

## 1. Housekeeping

- Repository form: the other modules are their own repositories registered as
  submodules of nornRTOS. nornHTTP is a plain directory today. Proposal: create
  `NornRTOS/nornHTTP`, commit this directory there, and register it with
  `tools/submodules.sh` (the .gitmodules entry and the script loop already name it).
- The demo server used by the ESP32 example lives in the repository as
  `tools/demo_server.py`, and `tests/integration.py` runs the same handler, so the
  Linux test and the board test see the same server.
- `examples/wifi_http/README.md` names no site-specific address and no path outside
  the repository.
- `THIRD_PARTY_LICENSES.md` states that the module has no third-party code.

## 2. Interoperability and the poisoned context

Accepted since 2026-11-10, each with a parser or builder test:

- `HTTP/1.0` status lines; the response is then close-delimited unless it carries
  Content-Length, Transfer-Encoding is rejected, and the connection is never reusable.
- A status line without a reason phrase (`HTTP/1.1 200\r\n`).
- Empty elements in the Connection list (`close,`, `, keep-alive`).
- Whitespace after a chunk size (`5 \r\n`), as the lenient mode of common parsers.
- The request builder omits `Content-Length: 0` for GET, HEAD, DELETE and OPTIONS
  without a body; POST, PUT and PATCH always carry it.
- `norn_http_execute` returns `NORN_HTTP_STALE` once an earlier execute on the same
  context failed after I/O began, until `norn_http_init` is called again. The design's
  rule "no reuse after a protocol error" is thereby code, not only documentation.

## 3. Fuzzing

```
sudo apt install clang
make fuzz && cp -r tests/corpus build/corpus
build/fuzz_parser -max_total_time=3600 build/corpus
```

Add `tests/fuzz_request.c` (method, target, host and header values from the fuzz input
through `norn_http_build_request`), keep the interesting cases in `tests/corpus`, and
record run time and result in `docs/measurements.md`.

## 4. Real ESP32

Extend `examples/wifi_http` to the REST example the design's phase 4 asks for: GET
`/hello`, POST JSON to `/api`, a streamed GET of 64 KiB from `/large` with a 512-byte
receive buffer verified by a checksum in `on_body`, and two requests on one socket to
test keep-alive over nornNET. Serve with `python3 modules/nornHTTP/tools/demo_server.py`
on the development machine and run from the nornRTOS root:

```
make APP=wifi_http BOARD=esp32 upload PORT=/dev/ttyUSB0
make APP=wifi_http BOARD=esp32 monitor PORT=/dev/ttyUSB0
```

## 5. Measurements on the target

- Linked flash: build wifi_http with and without the HTTP call and compare `size` of
  the ELF, plus `nm --size-sort` filtered on `norn_http*`, `norn_net_transport*`,
  `__udivdi3` and `__umoddi3`. The 64-bit lengths pull these libgcc helpers in on
  Xtensa; they are not in the object sizes reported today.
- Stack high-water of the task with and without the HTTP operation, with nornCore's
  stack check as tc11 does it.
- Static RAM of context, request, response and the example's buffers with `nm -S`.
- Write the table into `docs/measurements.md` with date and toolchain version, the
  library separate from the application's buffers and from nornNET, as design
  section 6 asks.

## 6. Request-body streaming

The design says it should be included if it stays simple. Either a source callback
that fills the request buffer span by span while Content-Length is known, fed through
the existing partial-send loop, with a test against the demo server; or a sentence in
README and contracts.md that it is planned for the next version. Decide before 1.0.

## 7. Documentation

Update `docs/contracts.md` and README after steps 2 and 4, rewrite "Validation status"
once the board has run, add a nornHTTP section to chapter 8 of the user guide in
nornDocs, put `include/norn_http.h` into the nornDocs Doxyfile, and complete the
header's documentation comments while doing so.

## 8. Closing

Run the host suite once more (`make test fuzz-smoke analyze adapter-test integration`),
build the nornRTOS examples, tag v0.1.0 and point the nornRTOS submodule at it. The
kernel is untouched, so the QEMU regression is not needed for this.
