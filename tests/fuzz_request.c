/* libFuzzer harness for the request builder. The input is split at NUL bytes into
 * method, target, host and header name/value pairs; the first byte selects body
 * length, range and close. Whatever the builder accepts must be a well-formed
 * HTTP/1.1 request head: CRLF line ends only, token field names, no injected lines. */
#include "norn_http.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static const char *field(char **cursor, char *end) {
    char *s=*cursor;
    if(s>=end) return NULL;
    *cursor=s+strlen(s)+1;
    return s;
}
static int token(unsigned c) {
    return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c!=0&&strchr("!#$%&'*+-.^_`|~",(int)c));
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static const char *methods[]={"GET","POST","PUT","DELETE","PATCH","HEAD","OPTIONS","BREW"};
    char input[512], out[1024], *cursor, *end; norn_http_header slots[8];
    norn_http_request q; size_t n, lines=0; const char *method,*target,*host,*name,*value;
    unsigned mode; static const uint8_t body[300];
    if(size<2 || size>sizeof(input)-1) return 0;
    mode=data[0]; memcpy(input,data+1,size-1); input[size-1]='\0';
    cursor=input; end=input+size-1;
    method=methods[(mode>>5)&7u];
    target=field(&cursor,end); host=field(&cursor,end);
    if(!target || !host) return 0;
    norn_http_request_init(&q,method,target,host,slots,8);
    while((name=field(&cursor,end)) && (value=field(&cursor,end)))
        (void)norn_http_add_header(&q,name,value);
    if(mode&1u) (void)norn_http_set_body(&q,body,(mode>>1)&3u ? ((mode>>1)&3u)*100u : 0u);
    if(mode&8u) (void)norn_http_set_range(&q,mode,(uint64_t)mode*7u);
    q.close=(mode&16u)!=0;
    if(norn_http_build_request(&q,out,sizeof(out),&n)) return 0;
    /* Accepted: check the invariants of the bytes that would go on the wire. */
    assert(n>=4 && n<=sizeof(out) && !memcmp(out+n-4,"\r\n\r\n",4));
    assert(!memcmp(out,method,strlen(method)) && out[strlen(method)]==' ');
    {
        const char *s=out, *stop=out+n-2;      /* lines up to the final empty one */
        while(s<stop) {
            const char *eol=memchr(s,'\r',(size_t)(stop-s));
            assert(eol && eol+1<out+n && eol[1]=='\n' && !memchr(s,'\n',(size_t)(eol-s)));
            assert(eol>s);                       /* no empty line before the last */
            if(lines++) {                        /* header line: token ':' SP value */
                const char *colon=memchr(s,':',(size_t)(eol-s)), *a;
                assert(colon && colon>s && colon[1]==' ');
                for(a=s;a<colon;++a) assert(token((unsigned char)*a));
                for(a=colon+2;a<eol;++a) assert(((unsigned char)*a>=32u || *a=='\t') && (unsigned char)*a!=127u);
            }
            s=eol+2;
        }
        assert(lines>=3);                        /* request line, Host, Connection */
    }
    return 0;
}
