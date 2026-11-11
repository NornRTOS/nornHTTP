/* One GET over an already connected nornNET socket.
 * Compile with the application's nornNET OS/configuration include paths.
 * All buffers are caller-owned (they arrive in config). */
#include "norn_http.h"
#include "norn_net_transport.h"

static uint32_t clock_ms(void *unused)
{
    (void)unused;
    return nn_os_now();
}

int norn_http_example_get(nn_tcp_t *socket, const norn_http_config *config,
                          const char *host, const char *path,
                          norn_http_response *response)
{
    norn_http_net_context context = { socket, clock_ms, NULL };
    norn_http_transport transport;
    norn_http http;
    norn_http_request request;
    int result = norn_http_net_transport_init(&transport, &context);

    if(result)
        return NORN_HTTP_INVALID;
    result = norn_http_init(&http, config, &transport);
    if(result)
        return result;

    norn_http_request_init(&request, "GET", path, host, NULL, 0);
    result = norn_http_execute(&http, &request, response);

    /* The application owns the socket: abort after a failure (the message boundary is
       unknown), close when the server does not allow reuse, otherwise keep it. */
    if(result)
        (void)nn_tcpAbort(socket);
    else if(!response->reusable)
        (void)nn_tcpClose(socket);
    return result;
}
