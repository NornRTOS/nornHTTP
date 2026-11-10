#include "norn_http.h"
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char line[128]; norn_http_parser p; norn_http_parser_config c;
    norn_http_request q; norn_http_response r; size_t used,at=0;
    norn_http_parser_config_init(&c,line,sizeof(line)); c.max_body_bytes=65536;
    norn_http_request_init(&q,"GET","/","test",NULL,0);
    (void)norn_http_parser_init(&p,&c,&q,&r);
    while(at<size) {
        size_t n=(size_t)data[at]%31u+1u;
        if(n>size-at) n=size-at;
        if(norn_http_parser_feed(&p,data+at,n,&used)!=NORN_HTTP_MORE) break;
        at+=used;
    }
    (void)norn_http_parser_eof(&p); return 0;
}
