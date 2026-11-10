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
    puts("PASS: nornNET adapter result mapping"); return 0;
}
