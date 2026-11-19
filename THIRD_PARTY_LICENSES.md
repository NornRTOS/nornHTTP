# Third-party components

nornHTTP contains no third-party code. The HTTP/1.1 parser and request builder are
written for this module. Everything in this repository is part of the Licensed Work
under LICENSE.md.

When linked into an ESP32 or AVR image, the toolchain's libc and libgcc are used for
`memcpy`, `strlen` and 64-bit division; their terms are listed in the nornRTOS
repository's THIRD_PARTY_LICENSES.md.
