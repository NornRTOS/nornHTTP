#ifndef NORN_NET_TRANSPORT_H
#define NORN_NET_TRANSPORT_H
#include "norn_http.h"
#include "norn_http_transport.h"
#include "nn_tcp.h"
/* The caller establishes and owns the socket. now_ms receives clock_context. */
typedef struct {
    nn_tcp_t *socket;
    uint32_t (*now_ms)(void *);
    void *clock_context;
} norn_http_net_context;
int norn_http_net_transport_init(norn_http_transport *, norn_http_net_context *);

/* The common case in two steps, for an application that wants one request at a time
   without choosing every buffer and callback itself. The library stays buffer-free; this
   is the adapter's convenience and costs nothing to an application that does not use it.

   NORN_HTTP_BUFFERS(name, lineLen, rxLen, reqLen) declares the three buffers (the parser's
   line, the receive buffer, the request buffer) and a norn_http_config `name` over them
   with the library's defaults for headers and a 5 s timeout; the application may change a
   field before use. norn_http_net_simple() then fills the transport over the given
   (connected) nornNET socket with nornNET's clock, sets the body buffer as the parser's
   body sink and its capacity as the limit, and runs norn_http_init(). */
#define NORN_HTTP_BUFFERS(name, lineLen, rxLen, reqLen)                                 \
    static char name##_line[lineLen];                                                   \
    static uint8_t name##_rx[rxLen];                                                    \
    static char name##_req[reqLen];                                                     \
    static norn_http_config name =                                                      \
    {                                                                                   \
        { name##_line, (lineLen), NORN_HTTP_DEFAULT_HEADER_BYTES, NORN_HTTP_DEFAULT_HEADERS, 0, NULL, NULL, NULL }, \
        name##_rx, (rxLen), name##_req, (reqLen), 5000                                  \
    }

int norn_http_net_simple(norn_http *http, norn_http_config *config, norn_http_transport *transport,
                         norn_http_net_context *net, nn_tcp_t *socket, norn_http_body_buffer *body);
#endif
