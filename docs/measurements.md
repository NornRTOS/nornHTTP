# Measurements and verification

Measured 2026-11-10 using GCC 14.2.0 on x86-64 Linux and
xtensa-esp32-elf GCC 16.1.0, `-Os -ffunction-sections -fdata-sections`.
Reproduce with `make size`, `make esp32-check`, and `tests/footprint.c`.

| Resource | Linux x86-64 | ESP32 Xtensa |
| --- | ---: | ---: |
| Core object text, including constant data | approximately 8.0 KB | approximately 5.4 KB |
| Mutable static data / BSS | 0 | 0 |
| Parser context | 136 bytes | 96 bytes |
| Full synchronous HTTP context (includes parser) | 280 bytes | 184 bytes |
| Request | 88 bytes | 56 bytes |
| Response | 56 bytes | 56 bytes |
| Heap allocations | 0 | 0 |

These are object-file sizes, **not final linked firmware flash measurements**.
The core needs `memchr`, `memcmp`, `memcpy`, `memset`, `strchr`, `strcmp` and
`strlen` from libc, and the 64-bit lengths pull `__udivdi3` and `__umoddi3` from
libgcc on Xtensa (no hardware 64-bit division); these add linked code that the
table above does not include. There is no printf/allocator dependency in the core.
TCP/IP and TLS memory/code are excluded; their cost depends on the application
and has not been measured here.

The POSIX example separately supplies a 512-byte line buffer, 1024-byte request
buffer, 1024-byte receive buffer and two header slots. It streams 2 MiB to stdout
without a full-body allocation. Other applications choose their own limits.

`-fstack-usage` reports a largest individual core function frame of 128 bytes
on ESP32 (request builder); the largest parser frame is 80 bytes. Frames add
along call chains and external callbacks/libc/transport have their own costs.
These reports are not runtime task stack high-water measurements. Hardware
stack measurements and final linked image sizes remain release work.

## Linked on the ESP32

Measured 2026-11-10 on the nornRTOS example `examples/wifi_http` (NodeMCU-32S,
xtensa-esp-elf GCC 16.1.0, `-Os`, `--gc-sections`, nornHTTP placed in flash by
`app.mk`). The same application was linked once as committed and once with the HTTP
session replaced by one raw `nn_tcpSend`/`nn_tcpRecv` exchange, so that nornWIFI,
nornNET, the TCP rings and the receive buffer stay in both images and the difference
is HTTP alone. The numbers come from `size` on the two ELF files and from the two
link maps.

| Flash | Bytes |
| --- | ---: |
| Whole image, difference with minus without HTTP | 6 912 |
| of which the library: `norn_http.o`, `norn_http_parser.o`, `norn_http_request.o`, `norn_http_stream.o` | approximately 5 300 |
| of which `norn_net_transport.o` (the nornNET adapter) | 169 |
| of which the example's own HTTP glue (`sink`, `on_body`, `run`, `rest_session`) | 805 |
| of which libc `memchr` (only nornHTTP uses it) and new string literals | approximately 350 |
| `__udivdi3` + `__umoddi3` from libgcc, 64-bit lengths | 506 + 606 = 1 112, **already present** in this image |

The libgcc helpers were linked in both images because nornNET and the Wi-Fi driver
already divide 64-bit values, so they cost this application nothing extra. An
application that has no other 64-bit division pays the 1 112 bytes once.
The parser's string literals share the linker's merged string pool with the rest of
the image, which is why the map attributes the whole pool to `norn_http_parser.o`;
the library figure above is the pool-free size.

| RAM | Bytes |
| --- | ---: |
| Library static data and BSS | 0 |
| `norn_http` context (`static norn_http http` in the example) | 184 |
| `norn_http_request` + `norn_http_response`, locals of `rest_session` | 56 + 56 |
| Example buffers: header line 256, request 256, receive 512, small body 128, sink 24 | 1 176 |
| Whole image, BSS + data difference | 816 + 24 |

Stack, read back with `rtos_getStackFree` (RTOS_CFG_STACK_CHECK 2) on the task that
joins Wi-Fi, runs DHCP, connects and then runs the four requests (stack 4 096 bytes):

| Stack high-water of the main task | Bytes |
| --- | ---: |
| After Wi-Fi join, DHCP and TCP connect, before HTTP (three runs, identical) | 1 312 |
| After the four requests (three runs) | 1 472, 1 328, 1 472 |
| Depth already used when `rest_session` is entered | 136 |
| The HTTP session below its entry, including nornNET's send and receive path and the kernel's context frames | at most 1 336 |

The run-to-run difference of 144 bytes is a context frame that lands on the task
stack when a tick or a Wi-Fi event preempts the deepest point of the chain, not a
property of nornHTTP. nornHTTP's own frames are small (`-fstack-usage`: 128 bytes for
the request builder, 80 for the header parser, 64 for `norn_http_execute`; the deepest
chain inside the library is about 240 bytes before libc and the transport); most of
the 1.3 KB is nornNET's TCP path and the kernel running on the caller's stack. A task
that does what this example does is safe with 2 KB; the example keeps 4 KB.

## Checks completed

- All seven methods and binary request-body length, injected controls, reserved
  headers, exact output capacity and one-byte overflow.
- Parser splits at every possible boundary and byte-at-a-time input.
- Fixed, chunked (extensions/trailers), close-delimited, 1xx, HEAD, 204, 304,
  keep-alive boundaries and Range/206/416 handling.
- HTTP/1.0 responses, status lines without a reason phrase, empty Connection list
  elements, whitespace after chunk sizes, Content-Length omitted for bodyless
  methods, and the poisoned context after a failed execute (2026-11-10).
- Truncation at every prefix of a chunked response; malformed status/framing,
  duplicate/conflicting lengths, numeric overflow, malformed chunks/trailers,
  header/body/line limits and callback failure.
- Mock transport partial I/O, timeout, repeated AGAIN, abrupt EOF, I/O errors,
  clock wrap, connection reuse and unexpected trailing data.
- Linux ASan + UBSan, GCC analyzer, 100,000 deterministic parser mutations.
  LeakSanitizer disabled because this execution environment uses ptrace.
- Coverage-guided fuzzing 2026-11-10 with Clang 19 libFuzzer, ASan and UBSan, one
  hour per harness: the response parser with four workers (1.86 billion runs,
  775 edges) and the request builder with two workers (1.53 billion runs,
  426 edges). No crash, no sanitizer report, no timeout. The minimised corpora in
  `tests/corpus` (634 and 353 inputs) replay the full coverage; llvm-cov on them
  shows 99.5 % of the parser's lines (the one miss is the unreachable default at
  the end of `line()`) and 100 % of the request builder's lines, 97.7 % and 91.7 %
  of the regions.
- nornNET adapter compiled against the actual sibling headers, with mocked
  nornNET send/receive result mapping tests.
- tools/demo_server.py as HTTP/1.1: 64 KiB and 2 MiB GET, bytewise chunked
  response with extension and whitespace, Range GET, JSON POST, small JSON GET;
  the same handler as HTTP/1.0 for the non-chunked, non-range paths.
- ESP32 core cross-compilation with warnings treated as errors.
- Real ESP32 (NodeMCU-32S) over Wi-Fi with nornWIFI and nornNET, 2026-11-10:
  nornRTOS `examples/wifi_http` against `tools/demo_server.py`; GET, JSON POST,
  a 64 KiB download streamed through a 512-byte receive buffer with every byte
  verified, and a fourth request on the same connection (keep-alive), then
  Connection: close. Two runs, all pass. Nothing in nornCore, nornNET or nornWIFI
  changed.

## Remaining validation

No TLS session was run. The ESP32 numbers above are for one application and one
toolchain version; repeat them when the parser or request builder changes. CMake is
not installed, so that build path has not been executed. The fuzzing above is a
first hour per harness; rerun `make fuzz-run` after every parser or builder change
and `make fuzz-merge` to keep the corpus. There is no stable-release signoff yet;
see `plan.md`.
