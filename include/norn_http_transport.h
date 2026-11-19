#ifndef NORN_HTTP_TRANSPORT_H
#define NORN_HTTP_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
/* Positive: bytes transferred, zero: orderly EOF. Negative values below.
 * Calls must return within timeout_ms. now_ms is a monotonic wrapping clock.
 * AGAIN may be returned only after yielding/waiting, never a busy spin. */
#define NORN_HTTP_IO_ERROR   (-1)
#define NORN_HTTP_IO_TIMEOUT (-2)
#define NORN_HTTP_IO_AGAIN   (-3)
typedef struct {
    void *context;
    int32_t (*send)(void *, const void *, size_t, uint32_t timeout_ms);
    int32_t (*recv)(void *, void *, size_t, uint32_t timeout_ms);
    uint32_t (*now_ms)(void *);
} norn_http_transport;
#endif
