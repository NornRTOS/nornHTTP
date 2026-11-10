#ifndef NORN_NET_TRANSPORT_H
#define NORN_NET_TRANSPORT_H
#include "norn_http_transport.h"
#include "nn_tcp.h"
/* The caller establishes and owns the socket. now_ms receives clock_context. */
typedef struct {
    nn_tcp_t *socket;
    uint32_t (*now_ms)(void *);
    void *clock_context;
} norn_http_net_context;
int norn_http_net_transport_init(norn_http_transport *, norn_http_net_context *);
#endif
