#include "internal.h"
static bool value_ok(const char *s) {
    if (!s) return false;
    for (; *s; ++s) if (((unsigned char)*s < 32u && *s != '\t') || (unsigned char)*s == 127u) return false;
    return true;
}
static bool name_ok(const char *s) {
    if (!s || !*s) return false;
    for (; *s; ++s) if (!http_token((unsigned char)*s)) return false;
    return true;
}
static bool reserved(const char *s) {
    size_t n = strlen(s);
    return http_equal(s,n,"Host") || http_equal(s,n,"Content-Length") ||
        http_equal(s,n,"Transfer-Encoding") || http_equal(s,n,"Connection") ||
        http_equal(s,n,"Range") || http_equal(s,n,"Expect") || http_equal(s,n,"Upgrade");
}
void norn_http_request_init(norn_http_request *r, const char *m, const char *t,
                            const char *h, norn_http_header *s, size_t n) {
    if (!r) return;
    memset(r,0,sizeof(*r)); r->method=m; r->target=t; r->host=h; r->headers=s; r->header_capacity=n;
}
int norn_http_add_header(norn_http_request *r, const char *n, const char *v) {
    if (!r || !name_ok(n) || !value_ok(v) || reserved(n)) return NORN_HTTP_INVALID;
    if (!r->headers || r->header_count >= r->header_capacity) return NORN_HTTP_LIMIT;
    r->headers[r->header_count++] = (norn_http_header){n,v}; return 0;
}
int norn_http_set_body(norn_http_request *r, const void *p, size_t n) {
    if (!r || (!p && n)) return NORN_HTTP_INVALID;
    r->body=p; r->body_length=n; r->body_source=NULL; r->body_source_context=NULL; return 0;
}
int norn_http_set_body_source(norn_http_request *r, norn_http_body_source_fn f, void *ctx, size_t n) {
    if (!r || !f) return NORN_HTTP_INVALID;
    r->body=NULL; r->body_length=n; r->body_source=f; r->body_source_context=ctx; return 0;
}
int norn_http_set_range(norn_http_request *r, uint64_t a, uint64_t b) {
    if (!r || b < a || b-a == UINT64_MAX) return NORN_HTTP_INVALID;
    r->range=true; r->range_start=a; r->range_end=b; return 0;
}
static char *number_end(char *p, uint64_t n) {
    char *first=p, *last;
    do { *p++=(char)('0'+n%10u); n/=10u; } while(n);
    *p='\0'; last=p-1;
    while(first<last) { char c=*first; *first++=*last; *last--=c; }
    return p;
}
static int append(char *p, size_t cap, size_t *at, const char *s) {
    size_t n=strlen(s);
    if (n > cap-*at) return NORN_HTTP_LIMIT;
    memcpy(p+*at,s,n); *at+=n; return 0;
}
int norn_http_build_request(const norn_http_request *r, char *p, size_t cap, size_t *written) {
    size_t at=0,i; const char *s; char number[64];
    if (written) *written=0;
    if (!r || !p || !written || !r->method || !r->target || !r->host || !*r->host ||
        (!r->body && !r->body_source && r->body_length) || (r->body && r->body_source) ||
        r->header_count > r->header_capacity ||
        (r->header_count && !r->headers)) return NORN_HTTP_INVALID;
    if (strcmp(r->method,"GET") && strcmp(r->method,"POST") && strcmp(r->method,"PUT") &&
        strcmp(r->method,"DELETE") && strcmp(r->method,"PATCH") && strcmp(r->method,"HEAD") &&
        strcmp(r->method,"OPTIONS")) return NORN_HTTP_INVALID;
    if (r->target[0]!='/' && !(strcmp(r->method,"OPTIONS")==0 && strcmp(r->target,"*")==0)) return NORN_HTTP_INVALID;
    for (s=r->target; *s; ++s) if ((unsigned char)*s<=32u || (unsigned char)*s>=127u || *s=='#') return NORN_HTTP_INVALID;
    for (s=r->host; *s; ++s) if ((unsigned char)*s<=32u || (unsigned char)*s>=127u || strchr("/@?#\\",*s)) return NORN_HTTP_INVALID;
    if (r->range && (strcmp(r->method,"GET") || r->range_end<r->range_start || r->range_end-r->range_start==UINT64_MAX)) return NORN_HTTP_INVALID;
#define ADD(s_) do { int e=append(p,cap,&at,(s_)); if(e) return e; } while(0)
    ADD(r->method); ADD(" "); ADD(r->target); ADD(" HTTP/1.1\r\nHost: "); ADD(r->host);
    ADD(r->close ? "\r\nConnection: close\r\n" : "\r\nConnection: keep-alive\r\n");
    for (i=0;i<r->header_count;++i) {
        if (!name_ok(r->headers[i].name) || !value_ok(r->headers[i].value) || reserved(r->headers[i].name)) return NORN_HTTP_INVALID;
        ADD(r->headers[i].name); ADD(": "); ADD(r->headers[i].value); ADD("\r\n");
    }
    if (r->range) {
        char *end=number_end(number,r->range_start);
        *end++='-'; (void)number_end(end,r->range_end);
        ADD("Range: bytes="); ADD(number); ADD("\r\n");
    }
    /* RFC 9110 8.6: no Content-Length when the method expects no content and there is
     * none. POST, PUT and PATCH always say how long the body is, even when empty. */
    if (r->body_length || strcmp(r->method,"POST")==0 || strcmp(r->method,"PUT")==0 ||
        strcmp(r->method,"PATCH")==0) {
        (void)number_end(number,(uint64_t)r->body_length);
        ADD("Content-Length: "); ADD(number); ADD("\r\n");
    }
    ADD("\r\n");
#undef ADD
    *written=at; return 0;
}
