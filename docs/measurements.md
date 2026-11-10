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
- nornNET adapter compiled against the actual sibling headers, with mocked
  nornNET send/receive result mapping tests.
- tools/demo_server.py as HTTP/1.1: 64 KiB and 2 MiB GET, bytewise chunked
  response with extension and whitespace, Range GET, JSON POST, small JSON GET;
  the same handler as HTTP/1.0 for the non-chunked, non-range paths.
- ESP32 core cross-compilation with warnings treated as errors.

## Remaining validation

No physical ESP32, nornNET end-to-end HTTP session or TLS session was run.
Clang/libFuzzer and CMake are not installed in the current environment, so their
provided build paths have not been executed. The deterministic mutation suite
is a smoke test, not a substitute for sustained coverage-guided fuzzing.
There is no claim of exhaustive error-path coverage or stable-release signoff.
