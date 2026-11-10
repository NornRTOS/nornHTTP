/* libFuzzer harness for the response parser. The first byte selects the request the
 * response answers (HEAD, a range, close) and a small body limit; the rest is the wire,
 * fed in spans whose lengths come from the data so every boundary is exercised. */
#include "norn_http.h"
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char line[128]; norn_http_parser p; norn_http_parser_config c;
    norn_http_request q; norn_http_response r; size_t used,at=1; unsigned mode;
    if(!size) return 0;
    mode=data[0];
    norn_http_parser_config_init(&c,line,sizeof(line)); c.max_body_bytes=(mode&8u) ? 16u : 65536u;
    norn_http_request_init(&q,(mode&1u) ? "HEAD" : "GET","/","test",NULL,0);
    if(mode&2u) (void)norn_http_set_range(&q,2,4);
    q.close=(mode&4u)!=0;
    if(norn_http_parser_init(&p,&c,&q,&r)) return 0;
    while(at<size) {
        size_t n=(size_t)data[at]%31u+1u;
        if(n>size-at) n=size-at;
        if(norn_http_parser_feed(&p,data+at,n,&used)!=NORN_HTTP_MORE) break;
        at+=used;
    }
    (void)norn_http_parser_eof(&p); return 0;
}
