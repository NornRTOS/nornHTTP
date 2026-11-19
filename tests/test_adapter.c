#include "norn_net_transport.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
static int32_t result;
int32_t nn_tcpSend(nn_tcp_t *s,const void *p,size_t n,nn_ms_t ms) {
    assert(s && p && n==4 && ms==123); return result;
}
int32_t nn_tcpRecv(nn_tcp_t *s,void *p,size_t n,nn_ms_t ms) {
    assert(s && p && n==4 && ms==123); return result;
}
static uint32_t clock_ms(void *p) { return *(uint32_t *)p; }
nn_ms_t nn_os_now(void) { return 7; }           /* nornNET's clock, for norn_http_net_simple */
NORN_HTTP_BUFFERS(simple_config, 128, 256, 128);
static uint8_t simple_body[64];
static void test_simple(nn_tcp_t *socket) {
    norn_http http; norn_http_transport t; norn_http_net_context c;
    norn_http_body_buffer body={simple_body,sizeof simple_body,3};
    assert(norn_http_net_simple(NULL,&simple_config,&t,&c,socket,&body)==NORN_HTTP_INVALID);
    assert(norn_http_net_simple(&http,&simple_config,&t,&c,socket,&body)==NORN_HTTP_OK);
    assert(c.socket==socket && t.now_ms(t.context)==7);
    assert(simple_config.parser.on_body==norn_http_buffer_body && simple_config.parser.callback_context==&body);
    assert(simple_config.parser.max_body_bytes==sizeof simple_body && body.length==0);
    assert(simple_config.parser.line_capacity==128 && simple_config.receive_capacity==256);
    assert(simple_config.request_capacity==128 && simple_config.timeout_ms==5000);
    assert(simple_config.parser.max_headers==NORN_HTTP_DEFAULT_HEADERS);
}
int main(void) {
    nn_tcp_t socket; uint32_t clock=42; char buf[4]={0}; norn_http_transport t;
    norn_http_net_context c={&socket,clock_ms,&clock};
    const int32_t inputs[]={4,1,0,NN_ERR_TIMEOUT,NN_ERR_PENDING,NN_ERR_RESET,NN_ERR_CLOSED};
    const int32_t expected[]={4,1,0,NORN_HTTP_IO_TIMEOUT,NORN_HTTP_IO_AGAIN,NORN_HTTP_IO_ERROR,NORN_HTTP_IO_ERROR};
    size_t i;
    assert(norn_http_net_transport_init(NULL,&c)<0);
    assert(!norn_http_net_transport_init(&t,&c)); assert(t.now_ms(t.context)==42);
    for(i=0;i<sizeof(inputs)/sizeof(inputs[0]);++i) {
        result=inputs[i]; assert(t.send(t.context,buf,4,123)==(result==0 ? NORN_HTTP_IO_TIMEOUT : expected[i]));
        assert(t.recv(t.context,buf,4,123)==expected[i]);
    }
    test_simple(&socket);
    puts("PASS: nornNET adapter result mapping, net_simple"); return 0;
}
