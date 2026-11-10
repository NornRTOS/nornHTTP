#include "norn_net_transport.h"
static int32_t map_result(int32_t n) {
    if(n>=0) return n;
    if(n==NN_ERR_TIMEOUT) return NORN_HTTP_IO_TIMEOUT;
    if(n==NN_ERR_PENDING) return NORN_HTTP_IO_AGAIN;
    return NORN_HTTP_IO_ERROR;
}
static int32_t net_send(void *arg, const void *p, size_t n, uint32_t ms) {
    norn_http_net_context *c=arg;
    int32_t result=nn_tcpSend(c->socket,p,n,ms);
    /* nornNET send returns a short count (including zero) on timeout. */
    return result==0 ? NORN_HTTP_IO_TIMEOUT : map_result(result);
}
static int32_t net_recv(void *arg, void *p, size_t n, uint32_t ms) {
    norn_http_net_context *c=arg; return map_result(nn_tcpRecv(c->socket,p,n,ms));
}
static uint32_t net_now(void *arg) {
    norn_http_net_context *c=arg; return c->now_ms(c->clock_context);
}
int norn_http_net_transport_init(norn_http_transport *t, norn_http_net_context *c) {
    if(!t || !c || !c->socket || !c->now_ms) return -1;
    *t=(norn_http_transport){c,net_send,net_recv,net_now}; return 0;
}
