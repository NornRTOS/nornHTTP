/* Coverage-guided harness for the request builder: method, target, host, header names
 * and values, body length, range and capacity all come from the fuzz input. Checks that
 * every accepted request is well formed and that nothing is written past the capacity. */
#include "norn_http.h"
#include <stdio.h>
#include <string.h>
#define FAIL(why) do { fputs("fuzz_request: " why "\n",stderr); __builtin_trap(); } while(0)
static const char *const methods[8]={"GET","POST","PUT","DELETE","PATCH","HEAD","OPTIONS","BREW"};
/* Next NUL-separated field of the input, or an empty string when the input is used up. */
static const char *field(const uint8_t *data, size_t size, size_t *at, char *out, size_t cap) {
    size_t n=0;
    while(*at<size && data[*at]!=0 && n+1<cap) out[n++]=(char)data[(*at)++];
    while(*at<size && data[*at]!=0) ++*at;
    if(*at<size) ++*at;
    out[n]=0; return out;
}
static int32_t dummy_source(void *a, uint8_t *p, size_t n) { (void)a; (void)p; return (int32_t)n; }
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char target[96], host[96], names[4][32], values[4][64], out[600]; uint8_t body[64];
    norn_http_header storage[4]; norn_http_request q; size_t at=0,i,cap,written=0;
    uint8_t flags, nHeaders; int e;
    if(size<4) return 0;
    flags=data[0]; nHeaders=(uint8_t)(data[1]%5u); cap=(size_t)data[2]*2u+1u; at=4;
    if(cap>sizeof(out)-1u) cap=sizeof(out)-1u;
    norn_http_request_init(&q,methods[flags&7u],field(data,size,&at,target,sizeof(target)),
                           field(data,size,&at,host,sizeof(host)),storage,(flags&8u)?4u:0u);
    for(i=0;i<nHeaders;++i) {
        field(data,size,&at,names[i],sizeof(names[i])); field(data,size,&at,values[i],sizeof(values[i]));
        (void)norn_http_add_header(&q,names[i],values[i]);
    }
    if(flags&16u) { memset(body,'b',sizeof(body)); (void)norn_http_set_body(&q,body,(size_t)data[3]%(sizeof(body)+1u)); }
    if((flags&16u) && (data[1]&1u)) (void)norn_http_set_body_source(&q,dummy_source,NULL,(size_t)data[3]*300u);
    if(flags&32u) (void)norn_http_set_range(&q,(uint64_t)data[3],(uint64_t)data[3]+(uint64_t)data[1]*7u);
    q.close=(flags&64u)!=0;
    out[cap]=(char)0x7F;                                /* sentinel one past the capacity */
    e=norn_http_build_request(&q,out,cap,&written);
    if(out[cap]!=(char)0x7F) FAIL("wrote past the capacity");
    if(e==NORN_HTTP_OK) {
        if(written>cap || written<4u) FAIL("bad written count");
        if(memcmp(out+written-4u,"\r\n\r\n",4u)) FAIL("no final CRLF CRLF");
        if(!memmem(out,written,"\r\nHost: ",8u)) FAIL("no Host header");
        for(i=0;i<written;++i) { unsigned char c=(unsigned char)out[i]; if((c<32u && c!='\r' && c!='\n' && c!='\t') || c==127u) FAIL("control character in the output"); }
    } else if(written!=0) FAIL("failure left a written count");
    return 0;
}
