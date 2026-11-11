/**
 * @file norn_net_transport.h
 * nornHTTP over a nornNET TCP socket. The application opens and connects the socket,
 * owns it, and closes or aborts it afterwards; nornHTTP only sends and receives.
 * Compile with the application's nornNET include paths.
 */
#ifndef NORN_NET_TRANSPORT_H
#define NORN_NET_TRANSPORT_H
#include "norn_http_transport.h"
#include "nn_tcp.h"

/** What the adapter needs: the connected socket and a millisecond clock. */
typedef struct {
    nn_tcp_t *socket;               /**< Connected with nn_tcpConnect (or accepted). */
    uint32_t (*now_ms)(void *);     /**< Monotonic milliseconds, e.g. nn_os_now wrapped. */
    void *clock_context;            /**< Passed to now_ms. */
} norn_http_net_context;

/**
 * Fills a norn_http_transport for the socket. The context must stay valid for as long
 * as the transport is used. Returns 0, or -1 when an argument is NULL.
 */
int norn_http_net_transport_init(norn_http_transport *, norn_http_net_context *);
#endif
