#include "internal.h"
enum { STATUS, HEADERS, FIXED, UNTIL_CLOSE, CHUNK_SIZE, CHUNK_DATA, CHUNK_CR, CHUNK_LF, TRAILERS, DONE, FAILED };
static int fail(norn_http_parser *p, int e) {
    p->state=FAILED; p->error=e; p->response->reusable=false; return e;
}
static bool decimal(const char **s, const char *end, uint64_t *n) {
    const char *start=*s; *n=0;
    while (*s<end && **s>='0' && **s<='9') {
        unsigned d=(unsigned)(**s-'0');
        if (*n>(UINT64_MAX-d)/10u) return false;
        *n=*n*10u+d; ++*s;
    }
    return *s!=start;
}
static int finish(norn_http_parser *p) {
    norn_http_response *r=p->response;
    if (p->range && r->body_received != r->range_end-r->range_start+1u) return fail(p,NORN_HTTP_RANGE);
    p->state=DONE; r->reusable=!p->close; return NORN_HTTP_OK;
}
void norn_http_parser_config_init(norn_http_parser_config *c, char *buf, size_t n) {
    if (!c) return;
    memset(c,0,sizeof(*c)); c->line_buffer=buf; c->line_capacity=n;
    c->max_headers=NORN_HTTP_DEFAULT_HEADERS; c->max_header_bytes=NORN_HTTP_DEFAULT_HEADER_BYTES;
}
int norn_http_parser_init(norn_http_parser *p, const norn_http_parser_config *c,
                          const norn_http_request *q, norn_http_response *r) {
    if (!p || !c || !q || !q->method || !r || !c->line_buffer || !c->line_capacity ||
        !c->max_headers || !c->max_header_bytes ||
        (q->range && (strcmp(q->method,"GET") || q->range_end<q->range_start || q->range_end-q->range_start==UINT64_MAX))) return NORN_HTTP_INVALID;
    memset(p,0,sizeof(*p)); memset(r,0,sizeof(*r)); p->config=*c; p->response=r;
    p->head=strcmp(q->method,"HEAD")==0; p->close=q->close;
    p->range=q->range; p->expected_start=q->range_start; p->expected_end=q->range_end;
    return 0;
}
static int connection(norn_http_parser *p, const char *s, const char *end) {
    /* Comma list; empty elements are ignored as RFC 9110 asks of recipients. */
    while (s<end) {
        const char *a;
        while(s<end && (*s==' ' || *s=='\t')) ++s;
        if(s==end) return 0;
        if(*s==',') { ++s; continue; }
        a=s; while(s<end && http_token((unsigned char)*s)) ++s;
        if(s==a) return NORN_HTTP_PROTOCOL;
        if(http_equal(a,(size_t)(s-a),"close")) p->close=true;
        while(s<end && (*s==' ' || *s=='\t')) ++s;
        if(s<end && *s++!=',') return NORN_HTTP_PROTOCOL;
    }
    return 0;
}
static int header(norn_http_parser *p, const char *s, size_t n, bool trailer) {
    const char *end=s+n,*colon=memchr(s,':',n),*v,*ve,*a;
    norn_http_response *r=p->response; size_t name;
    if (!colon || colon==s) return NORN_HTTP_PROTOCOL;
    for(a=s;a<colon;++a) if(!http_token((unsigned char)*a)) return NORN_HTTP_PROTOCOL;
    name=(size_t)(colon-s); v=colon+1; ve=end;
    while(v<ve && (*v==' ' || *v=='\t')) ++v;
    while(ve>v && (ve[-1]==' ' || ve[-1]=='\t')) --ve;
    if (++p->headers>p->config.max_headers) return NORN_HTTP_LIMIT;
    if (http_equal(s,name,"Content-Length")) {
        uint64_t length; a=v;
        if(trailer || !decimal(&a,ve,&length) || a!=ve || (p->seen_cl && r->content_length!=length)) return NORN_HTTP_PROTOCOL;
        p->seen_cl=true; r->has_length=true; r->content_length=length;
    } else if(http_equal(s,name,"Transfer-Encoding")) {
        if(trailer || p->seen_te || !http_equal(v,(size_t)(ve-v),"chunked")) return NORN_HTTP_PROTOCOL;
        p->seen_te=true; r->chunked=true;
    } else if(http_equal(s,name,"Connection")) {
        if(trailer) return NORN_HTTP_PROTOCOL;
        if(connection(p,v,ve)) return NORN_HTTP_PROTOCOL;
    } else if(http_equal(s,name,"Content-Range")) {
        uint64_t start,stop,total;
        if(trailer || p->seen_range || ve-v<6 || memcmp(v,"bytes ",6)) return NORN_HTTP_RANGE;
        a=v+6;
        if (ve-a>=2 && a[0]=='*' && a[1]=='/') {
            a+=2;
            if(r->status!=416u || !decimal(&a,ve,&total) || a!=ve) return NORN_HTTP_RANGE;
            p->seen_range=true; r->range_total=total;
        } else {
        if(!decimal(&a,ve,&start) || a==ve || *a++!='-' || !decimal(&a,ve,&stop) ||
           a==ve || *a++!='/' || !decimal(&a,ve,&total) || a!=ve || stop<start || stop>=total) return NORN_HTTP_RANGE;
        p->seen_range=true; r->has_range=true; r->range_start=start; r->range_end=stop; r->range_total=total;
        }
    } else if(trailer && (http_equal(s,name,"Host") || http_equal(s,name,"Trailer") ||
              http_equal(s,name,"Content-Encoding") || http_equal(s,name,"Content-Type") ||
              http_equal(s,name,"Authorization"))) return NORN_HTTP_PROTOCOL;
    if(p->config.on_header && p->config.on_header(p->config.callback_context,s,name,v,(size_t)(ve-v),trailer)) return NORN_HTTP_CALLBACK;
    return 0;
}
static int end_headers(norn_http_parser *p) {
    norn_http_response *r=p->response;
    if(p->seen_cl && p->seen_te) return NORN_HTTP_PROTOCOL;
    if(p->http10 && p->seen_te) return NORN_HTTP_PROTOCOL;
    if(r->status<200u) {
        if(r->status==101u || p->seen_cl || p->seen_te) return NORN_HTTP_PROTOCOL;
        /* Keep cumulative metadata limits across informational responses. */
        memset(r,0,sizeof(*r)); p->seen_range=false; p->state=STATUS; return 0;
    }
    if(r->status==204u && (p->seen_cl || p->seen_te)) return NORN_HTTP_PROTOCOL;
    if(p->range) {
        if(r->status!=206u || !r->has_range || r->range_start!=p->expected_start ||
           r->range_end!=p->expected_end || (r->has_length && r->content_length!=r->range_end-r->range_start+1u)) return NORN_HTTP_RANGE;
    }
    if(p->head || r->status==204u || r->status==304u) return finish(p);
    if(r->has_length && p->config.max_body_bytes && r->content_length>p->config.max_body_bytes) return NORN_HTTP_LIMIT;
    if(r->chunked) p->state=CHUNK_SIZE;
    else if(r->has_length) {
        p->remaining=r->content_length; p->state=FIXED;
        if(!p->remaining) return finish(p);
    } else { p->state=UNTIL_CLOSE; p->close=true; }
    return 0;
}
/* Chunk extensions: token names and optional token/quoted-string values. */
static bool extensions(const char *s, const char *end) {
    while(s<end) {
        const char *a;
        while(s<end && (*s==' ' || *s=='\t')) ++s;
        if(s==end) return true;              /* whitespace after the size: tolerated */
        if(*s++!=';') return false;
        while(s<end && (*s==' ' || *s=='\t')) ++s;
        a=s; while(s<end && http_token((unsigned char)*s)) ++s;
        if(a==s) return false;
        while(s<end && (*s==' ' || *s=='\t')) ++s;
        if(s<end && *s=='=') {
            ++s; while(s<end && (*s==' ' || *s=='\t')) ++s;
            if(s<end && *s=='"') {
                ++s;
                while(s<end && *s!='"') { if(*s=='\\' && ++s==end) return false; ++s; }
                if(s==end) return false;
                ++s;
            } else {
                a=s; while(s<end && http_token((unsigned char)*s)) ++s;
                if(a==s) return false;
            }
        }
    }
    return true;
}
static int line(norn_http_parser *p) {
    const char *s=p->config.line_buffer,*end=s+p->line_length; size_t n=p->line_length;
    if(p->state==STATUS) {
        /* HTTP/1.0 or HTTP/1.1, three-digit status 100..599, the reason phrase optional.
         * A 1.0 peer may not chunk and the connection is not reused afterwards. */
        if(n<12 || memcmp(s,"HTTP/1.",7) || (s[7]!='0' && s[7]!='1') || s[8]!=' ' ||
           s[9]<'1' || s[9]>'5' || s[10]<'0' || s[10]>'9' || s[11]<'0' || s[11]>'9' ||
           (n>12 && s[12]!=' ')) return NORN_HTTP_PROTOCOL;
        p->response->status=(unsigned)(s[9]-'0')*100u+(unsigned)(s[10]-'0')*10u+(unsigned)(s[11]-'0');
        if(s[7]=='0') { p->http10=true; p->close=true; }
        p->state=HEADERS; return 0;
    }
    if(p->state==HEADERS) return n ? header(p,s,n,false) : end_headers(p);
    if(p->state==TRAILERS) return n ? header(p,s,n,true) : finish(p);
    if(p->state==CHUNK_SIZE) {
        uint64_t size=0; const char *a=s;
        while(s<end) {
            unsigned d=http_lower((unsigned char)*s);
            if(d>='0' && d<='9') d-='0';
            else if(d>='a' && d<='f') d=d-'a'+10u;
            else break;
            if(size>(UINT64_MAX-d)/16u) return NORN_HTTP_PROTOCOL;
            size=size*16u+d; ++s;
        }
        if(s==a || !extensions(s,end)) return NORN_HTTP_PROTOCOL;
        p->remaining=size; p->state=size ? CHUNK_DATA : TRAILERS;
        return 0;
    }
    return NORN_HTTP_PROTOCOL;
}
int norn_http_parser_feed(norn_http_parser *p, const void *data, size_t n, size_t *used) {
    const uint8_t *s=data; size_t i=0;
    if(used) *used=0;
    if(!p || !p->response || !used || (!data && n)) return NORN_HTTP_INVALID;
    if(p->state==FAILED) return p->error;
    while(i<n && p->state!=DONE) {
        int e=0;
        if(p->state==FIXED || p->state==UNTIL_CLOSE || p->state==CHUNK_DATA) {
            size_t count=n-i;
            if(p->state!=UNTIL_CLOSE && p->remaining<(uint64_t)count) count=(size_t)p->remaining;
            if((uint64_t)count>UINT64_MAX-p->response->body_received) e=NORN_HTTP_LIMIT;
            else {
                uint64_t total=p->response->body_received+(uint64_t)count;
                if(p->config.max_body_bytes && total>p->config.max_body_bytes) e=NORN_HTTP_LIMIT;
                else if(p->range && total>p->expected_end-p->expected_start+1u) e=NORN_HTTP_RANGE;
                else if(p->config.on_body && p->config.on_body(p->config.callback_context,s+i,count)) e=NORN_HTTP_CALLBACK;
                else {
                    p->response->body_received=total; i+=count;
                    if(p->state!=UNTIL_CLOSE) {
                        p->remaining-=count;
                        if(!p->remaining) { if(p->state==FIXED) e=finish(p); else p->state=CHUNK_CR; }
                    }
                }
            }
        } else if(p->state==CHUNK_CR || p->state==CHUNK_LF) {
            if(s[i++]!=(p->state==CHUNK_CR ? '\r' : '\n')) e=NORN_HTTP_PROTOCOL;
            else p->state=p->state==CHUNK_CR ? CHUNK_LF : CHUNK_SIZE;
        } else {
            unsigned c=s[i++];
            /* Chunk-size lines have a per-line limit; all other metadata has a
             * cumulative budget, including 1xx and trailers. */
            if(p->state!=CHUNK_SIZE && p->header_bytes++>=p->config.max_header_bytes) e=NORN_HTTP_LIMIT;
            else if(p->cr) {
                p->cr=false;
                if(c!='\n') e=NORN_HTTP_PROTOCOL;
                else { e=line(p); p->line_length=0; }
            } else if(c=='\r') p->cr=true;
            else if(c=='\n' || (c<32u && c!='\t') || c==127u) e=NORN_HTTP_PROTOCOL;
            else if(p->line_length==p->config.line_capacity) e=NORN_HTTP_LIMIT;
            else p->config.line_buffer[p->line_length++]=(char)c;
        }
        if(e<0) { *used=i; return fail(p,e); }
    }
    *used=i; return p->state==DONE ? NORN_HTTP_OK : NORN_HTTP_MORE;
}
int norn_http_parser_eof(norn_http_parser *p) {
    if(!p || !p->response) return NORN_HTTP_INVALID;
    if(p->state==FAILED) return p->error;
    if(p->state==DONE) { p->response->reusable=false; return 0; }
    if(p->state==UNTIL_CLOSE) return finish(p);
    return fail(p,NORN_HTTP_TRUNCATED);
}
