#include "norn_http.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
typedef struct { uint8_t body[1024]; size_t n, headers, trailers; int reject; } sink;
static int body(void *arg, const uint8_t *p, size_t n) {
    sink *s=arg; if(s->reject) return 1;
    assert(n<=sizeof(s->body)-s->n); memcpy(s->body+s->n,p,n); s->n+=n; return 0;
}
static int header(void *arg, const char *name, size_t nn, const char *v, size_t vn, bool trailer) {
    sink *s=arg; assert(name && nn && v); (void)vn;
    ++s->headers; if(trailer) ++s->trailers; return s->reject;
}
static void setup(norn_http_parser *p, norn_http_response *r, norn_http_request *q,
                  norn_http_parser_config *c, char *buf, size_t cap, sink *s) {
    memset(s,0,sizeof(*s));
    norn_http_request_init(q,"GET","/","example.test",NULL,0);
    norn_http_parser_config_init(c,buf,cap); c->on_body=body; c->on_header=header; c->callback_context=s;
    assert(!norn_http_parser_init(p,c,q,r));
}
static void fragmented(const char *wire, const char *expected, unsigned status, bool reusable) {
    size_t len=strlen(wire),split;
    for(split=0;split<=len;++split) {
        char buf[256]; sink s; norn_http_parser p; norn_http_response r;
        norn_http_request q; norn_http_parser_config c; size_t used; int e;
        setup(&p,&r,&q,&c,buf,sizeof(buf),&s);
        e=norn_http_parser_feed(&p,wire,split,&used); assert(used==split && e>=0);
        e=norn_http_parser_feed(&p,wire+split,len-split,&used); assert(used==len-split && e>=0);
        if(e==NORN_HTTP_MORE) assert(!norn_http_parser_eof(&p));
        assert(s.n==strlen(expected) && !memcmp(s.body,expected,s.n));
        assert(r.status==status && r.reusable==reusable);
    }
    /* Pause after every single byte in the same message. */
    {
        char buf[256]; sink s; norn_http_parser p; norn_http_response r;
        norn_http_request q; norn_http_parser_config c; size_t used;
        setup(&p,&r,&q,&c,buf,sizeof(buf),&s);
        for(split=0;split<len;++split) assert(norn_http_parser_feed(&p,wire+split,1,&used)>=0 && used==1);
        assert(!norn_http_parser_eof(&p)); assert(s.n==strlen(expected));
    }
}
static void parser_tests(void) {
    static const char *bad[]={
        "HTTP/1.2 200 OK\r\n\r\n", "HTTP/1.1 600 Bad\r\n\r\n", "HTTP/1.1 200X\r\n\r\n",
        "HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nConnection: close x\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5 x\r\n",
        "HTTP/1.1 200 OK\n\n", "HTTP/1.1 200 OK\rX", "HTTP/1.1 200 OK\r\n bad: x\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1, 1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 101 Switching Protocols\r\n\r\n",
        "HTTP/1.1 100 Continue\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nX: a\001\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n-1\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10000000000000000\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;foo=\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;foo=\"x\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx!",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 0\r\n\r\n"
    };
    size_t i,j;
    fragmented("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello","hello",200,true);
    fragmented("HTTP/1.1 200 OK\r\ncOnTeNt-LeNgTh: 5\r\nContent-Length: 5\r\nConnection: Keep-Alive, close\r\n\r\nhello","hello",200,false);
    fragmented("HTTP/1.1 103 Early Hints\r\nLink: </a>\r\n\r\nHTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n2;foo=\"a\\\"b\"\r\nhe\r\n3\r\nllo\r\n0\r\nX-Checksum: 123\r\n\r\n","hello",200,true);
    fragmented("HTTP/1.1 200 OK\r\n\r\nhello","hello",200,false);
    fragmented("HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello","hello",200,false);
    fragmented("HTTP/1.0 200 OK\r\n\r\nhello","hello",200,false);
    fragmented("HTTP/1.1 200\r\nContent-Length: 5\r\n\r\nhello","hello",200,true);
    fragmented("HTTP/1.1 200 OK\r\nConnection: ,close,\r\nContent-Length: 0\r\n\r\n","",200,false);
    fragmented("HTTP/1.1 200 OK\r\nConnection: keep-alive, ,\r\nContent-Length: 0\r\n\r\n","",200,true);
    fragmented("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5 \r\nhello\r\n0\t\r\n\r\n","hello",200,true);
    fragmented("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */10\r\nContent-Length: 0\r\n\r\n","",416,true);
    fragmented("HTTP/1.1 204 No Content\r\n\r\n","",204,true);
    fragmented("HTTP/1.1 304 Not Modified\r\nContent-Length: 500\r\n\r\n","",304,true);
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        for(j=0;j<=strlen(bad[i]);++j) {
            char buf[256]; sink s; norn_http_parser p; norn_http_response r; norn_http_request q; norn_http_parser_config c; size_t used; int e;
            setup(&p,&r,&q,&c,buf,sizeof(buf),&s);
            e=norn_http_parser_feed(&p,bad[i],j,&used);
            if(e>=0) e=norn_http_parser_feed(&p,bad[i]+j,strlen(bad[i])-j,&used);
            assert(e<0 && !r.reusable); assert(norn_http_parser_eof(&p)==e);
        }
    }
    {
        const char *wire="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\nX: y\r\n\r\n";
        for(i=0;i<strlen(wire);++i) {
            char buf[256]; sink s; norn_http_parser p; norn_http_response r; norn_http_request q; norn_http_parser_config c; size_t used;
            setup(&p,&r,&q,&c,buf,sizeof(buf),&s);
            assert(norn_http_parser_feed(&p,wire,i,&used)==NORN_HTTP_MORE);
            assert(norn_http_parser_eof(&p)==NORN_HTTP_TRUNCATED);
        }
    }
}
static void buffer_test(void) {
    uint8_t data[3]; norn_http_body_buffer b={data,sizeof(data),0};
    assert(!norn_http_buffer_body(&b,(const uint8_t *)"a\0b",3));
    assert(b.length==3 && data[1]==0);
    assert(norn_http_buffer_body(&b,(const uint8_t *)"c",1));
    assert(b.length==3);
}
static void boundaries(void) {
    char buf[256]; sink s; norn_http_parser p; norn_http_response r; norn_http_request q; norn_http_parser_config c; size_t used;
    const char *wire="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\nNEXT";
    setup(&p,&r,&q,&c,buf,sizeof(buf),&s);
    assert(!norn_http_parser_feed(&p,wire,strlen(wire),&used)); assert(used==strlen(wire)-4);
    setup(&p,&r,&q,&c,buf,sizeof(buf),&s); q.method="HEAD"; assert(!norn_http_parser_init(&p,&c,&q,&r));
    wire="HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nBODY";
    assert(!norn_http_parser_feed(&p,wire,strlen(wire),&used) && used==strlen(wire)-4 && !s.n);
    wire="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    c.line_capacity=strlen("Content-Length: 0"); c.max_header_bytes=strlen(wire); c.max_headers=1;
    assert(!norn_http_parser_init(&p,&c,&q,&r)); assert(!norn_http_parser_feed(&p,wire,strlen(wire),&used));
    --c.line_capacity; assert(!norn_http_parser_init(&p,&c,&q,&r)); assert(norn_http_parser_feed(&p,wire,strlen(wire),&used)==NORN_HTTP_LIMIT);
    ++c.line_capacity; --c.max_header_bytes; assert(!norn_http_parser_init(&p,&c,&q,&r)); assert(norn_http_parser_feed(&p,wire,strlen(wire),&used)==NORN_HTTP_LIMIT);
    setup(&p,&r,&q,&c,buf,sizeof(buf),&s); c.max_body_bytes=2; assert(!norn_http_parser_init(&p,&c,&q,&r));
    wire="HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    assert(norn_http_parser_feed(&p,wire,strlen(wire),&used)==NORN_HTTP_LIMIT && !s.n);
    setup(&p,&r,&q,&c,buf,sizeof(buf),&s); s.reject=1;
    assert(norn_http_parser_feed(&p,wire,strlen(wire),&used)==NORN_HTTP_CALLBACK);
    setup(&p,&r,&q,&c,buf,sizeof(buf),&s); c.on_header=NULL; s.reject=1;
    assert(!norn_http_parser_init(&p,&c,&q,&r)); assert(norn_http_parser_feed(&p,wire,strlen(wire),&used)==NORN_HTTP_CALLBACK);
    setup(&p,&r,&q,&c,buf,sizeof(buf),&s); c.max_headers=1; assert(!norn_http_parser_init(&p,&c,&q,&r));
    wire="HTTP/1.1 200 OK\r\nX: 1\r\nY: 2\r\n\r\n";
    assert(norn_http_parser_feed(&p,wire,strlen(wire),&used)==NORN_HTTP_LIMIT);
}
static void ranges(void) {
    const char *wire[]={
        "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 2-4/10\r\nContent-Length: 3\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc",
        "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 1-3/10\r\nContent-Length: 3\r\n\r\nabc",
        "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 2-4/10\r\nContent-Length: 4\r\n\r\nabcd",
        "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 2-4/4\r\n\r\nabc"
    }; size_t i;
    for(i=0;i<sizeof(wire)/sizeof(wire[0]);++i) {
        char buf[256]; sink s; norn_http_parser p; norn_http_response r; norn_http_request q; norn_http_parser_config c; size_t used; int e;
        setup(&p,&r,&q,&c,buf,sizeof(buf),&s); assert(!norn_http_set_range(&q,2,4)); assert(!norn_http_parser_init(&p,&c,&q,&r));
        e=norn_http_parser_feed(&p,wire[i],strlen(wire[i]),&used);
        assert(i ? e==NORN_HTTP_RANGE && s.n==0 : e==0 && s.n==3);
    }
}
static void requests(void) {
    const char *methods[]={"GET","POST","PUT","DELETE","PATCH","HEAD","OPTIONS"};
    norn_http_request q; norn_http_header h[2]; char buf[512]; size_t n,i;
    for(i=0;i<sizeof(methods)/sizeof(methods[0]);++i) {
        norn_http_request_init(&q,methods[i],"/api?a=1","example.test:8080",h,2);
        assert(!norn_http_add_header(&q,"Content-Type","application/octet-stream"));
        assert(!norn_http_set_body(&q,"a\0b",3));
        assert(!norn_http_build_request(&q,buf,sizeof(buf),&n)); buf[n]=0;
        assert(strstr(buf,"Content-Length: 3\r\n")); assert(!memcmp(buf,methods[i],strlen(methods[i])));
        assert(!norn_http_build_request(&q,buf,n,&n));
        assert(norn_http_build_request(&q,buf,n-1,&n)==NORN_HTTP_LIMIT);
        assert(norn_http_add_header(&q,"X","a\r\nInjected: yes")==NORN_HTTP_INVALID);
        assert(norn_http_add_header(&q,"Content-Length","3")==NORN_HTTP_INVALID);
        assert(norn_http_add_header(&q,"bad name","x")==NORN_HTTP_INVALID);
        assert(!norn_http_add_header(&q,"Authorization","Bearer token"));
        assert(norn_http_add_header(&q,"X","y")==NORN_HTTP_LIMIT);
    }
    /* No Content-Length without a body for the bodyless methods; always for the others. */
    for(i=0;i<sizeof(methods)/sizeof(methods[0]);++i) {
        bool always=!strcmp(methods[i],"POST") || !strcmp(methods[i],"PUT") || !strcmp(methods[i],"PATCH");
        norn_http_request_init(&q,methods[i],"/","example.test",NULL,0);
        assert(!norn_http_build_request(&q,buf,sizeof(buf),&n)); buf[n]=0;
        assert((strstr(buf,"Content-Length: 0\r\n")!=NULL)==always);
        assert(!memcmp(buf+n-4,"\r\n\r\n",4));
    }
    norn_http_request_init(&q,"GET","/","example.test",h,2);
    assert(!norn_http_set_range(&q,0,UINT64_MAX-1)); assert(!norn_http_build_request(&q,buf,sizeof(buf),&n));
    buf[n]=0; assert(strstr(buf,"Range: bytes=0-18446744073709551614"));
    assert(norn_http_set_range(&q,0,UINT64_MAX)==NORN_HTTP_INVALID);
    q.target="/\r\n"; assert(norn_http_build_request(&q,buf,sizeof(buf),&n)==NORN_HTTP_INVALID);
    q.target="/"; q.host="host\n"; assert(norn_http_build_request(&q,buf,sizeof(buf),&n)==NORN_HTTP_INVALID);
}
typedef struct { const char *wire; size_t at, sent, step; uint32_t now; int mode; } mock;
static uint32_t now(void *arg) { mock *m=arg; return m->now++; }
static int32_t send_mock(void *arg,const void *p,size_t n,uint32_t timeout) {
    mock *m=arg; assert(p && timeout); if(m->mode==1) return NORN_HTTP_IO_TIMEOUT;
    if(m->mode==2) return NORN_HTTP_IO_AGAIN;
    if(m->mode==3) return 0;
    if(n>m->step) n=m->step;
    m->sent+=n; return (int32_t)n;
}
static int32_t recv_mock(void *arg,void *p,size_t n,uint32_t timeout) {
    mock *m=arg; size_t left=strlen(m->wire)-m->at; assert(timeout);
    if(m->mode==4) return NORN_HTTP_IO_ERROR;
    if(n>left) n=left;
    if(n>m->step) n=m->step;
    memcpy(p,m->wire+m->at,n); m->at+=n; return (int32_t)n;
}
static void transport_tests(void) {
    char linebuf[128],requestbuf[512]; uint8_t rx[128]; norn_http h; norn_http_config c;
    norn_http_request q; norn_http_response r; sink s; int mode;
    mock m={"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc",0,0,2,UINT32_MAX-50u,0};
    norn_http_transport t={&m,send_mock,recv_mock,now};
    memset(&c,0,sizeof(c)); memset(&s,0,sizeof(s));
    norn_http_parser_config_init(&c.parser,linebuf,sizeof(linebuf)); c.parser.on_body=body; c.parser.callback_context=&s;
    c.receive_buffer=rx; c.receive_capacity=sizeof(rx); c.request_buffer=requestbuf; c.request_capacity=sizeof(requestbuf); c.timeout_ms=1000;
    assert(!norn_http_init(&h,&c,&t)); norn_http_request_init(&q,"POST","/","test",NULL,0); assert(!norn_http_set_body(&q,"x\0y",3));
    assert(!norn_http_execute(&h,&q,&r)); assert(h.reusable && r.reusable && s.n==3 && m.sent>3);
    m.at=0; s.n=0; assert(!norn_http_execute(&h,&q,&r));
    for(mode=1;mode<=4;++mode) {
        m.mode=mode; m.at=0; s.n=0;
        assert(norn_http_execute(&h,&q,&r)==(mode<=2 ? NORN_HTTP_TIMEOUT : NORN_HTTP_TRANSPORT)); assert(!h.reusable && !r.reusable);
        /* The context is poisoned until it is initialised again. */
        assert(h.failed && norn_http_execute(&h,&q,&r)==NORN_HTTP_STALE);
        assert(!norn_http_init(&h,&c,&t) && !h.failed);
    }
    m.mode=0; m.at=0; s.n=0; m.wire="HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na";
    assert(norn_http_execute(&h,&q,&r)==NORN_HTTP_TRUNCATED);
    assert(norn_http_execute(&h,&q,&r)==NORN_HTTP_STALE); assert(!norn_http_init(&h,&c,&t));
    m.at=0; s.n=0; m.step=128; m.wire="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\nextra";
    assert(norn_http_execute(&h,&q,&r)==NORN_HTTP_PROTOCOL && !r.reusable && h.failed);
    /* An argument error before any I/O does not poison the context. */
    assert(!norn_http_init(&h,&c,&t)); q.method="BREW";
    assert(norn_http_execute(&h,&q,&r)==NORN_HTTP_INVALID && !h.failed);
    q.method="POST"; m.at=0; s.n=0; m.wire="HTTP/1.0 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    assert(!norn_http_execute(&h,&q,&r) && !r.reusable && !h.failed && s.n==3);
}
int main(void) {
    buffer_test(); parser_tests(); boundaries(); ranges(); requests(); transport_tests();
    printf("PASS: requests, fragmentation, framing, ranges, limits, callbacks, transport\n");
    printf("sizeof parser=%zu, context=%zu, request=%zu, response=%zu\n",sizeof(norn_http_parser),sizeof(norn_http),sizeof(norn_http_request),sizeof(norn_http_response));
    return 0;
}
