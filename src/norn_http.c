#include "internal.h"
#include <limits.h>

int norn_http_init(norn_http *h, const norn_http_config *c, const norn_http_transport *t)
{
    if(!h  ||  !c  ||  !t  ||  !t->send  ||  !t->recv  ||  !t->now_ms  ||
       !c->receive_buffer  ||  !c->receive_capacity  ||  c->receive_capacity > INT32_MAX  ||
       !c->request_buffer  ||  !c->request_capacity  ||
       !c->timeout_ms  ||  c->timeout_ms > INT32_MAX  ||
       !c->parser.line_buffer  ||  !c->parser.line_capacity  ||
       !c->parser.max_headers  ||  !c->parser.max_header_bytes)
        return NORN_HTTP_INVALID;

    memset(h, 0, sizeof(*h));
    h->config = *c;
    h->transport = *t;
    return 0;
}

/* Milliseconds left of the total deadline, zero when it has passed. */
static uint32_t budget(norn_http *h, uint32_t start)
{
    uint32_t elapsed = h->transport.now_ms(h->transport.context) - start;

    return elapsed >= h->config.timeout_ms ? 0u : h->config.timeout_ms - elapsed;
}

static int io_error(int32_t n)
{
    return n == NORN_HTTP_IO_TIMEOUT ? NORN_HTTP_TIMEOUT : NORN_HTTP_TRANSPORT;
}

/* Sends the whole span, however many partial sends the transport needs. */
static int send_all(norn_http *h, const void *data, size_t len, uint32_t start)
{
    const uint8_t *p = data;

    while(len)
    {
        size_t count = len > INT32_MAX ? INT32_MAX : len;
        uint32_t left = budget(h, start);
        int32_t n;

        if(!left)
            return NORN_HTTP_TIMEOUT;
        n = h->transport.send(h->transport.context, p, count, left);
        if(n == NORN_HTTP_IO_AGAIN)
            continue;
        if(n <= 0)
            return io_error(n);
        if((size_t)n > count)
            return NORN_HTTP_TRANSPORT;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

/* Streamed request body: the source fills the request buffer (free once the head has
 * been sent) span by span, and each span goes out through the same partial-send loop. */
static int send_source(norn_http *h, const norn_http_request *q, uint32_t start)
{
    size_t left = q->body_length;

    while(left)
    {
        size_t cap = left < h->config.request_capacity ? left : h->config.request_capacity;
        size_t got = 0;
        int e;

        if(q->body_source(q->body_source_context, (uint8_t *)h->config.request_buffer, cap, &got)  ||
           !got  ||  got > cap)
            return NORN_HTTP_CALLBACK;
        e = send_all(h, h->config.request_buffer, got, start);
        if(e)
            return e;
        left -= got;
    }
    return 0;
}

int norn_http_execute(norn_http *h, const norn_http_request *q, norn_http_response *r)
{
    size_t length;
    uint32_t start;
    int e;

    if(!h  ||  !q  ||  !r)
        return NORN_HTTP_INVALID;
    if(h->failed)
        return NORN_HTTP_STALE;
    h->reusable = false;

    /* Validate and build everything before the first byte goes out. */
    e = norn_http_parser_init(&h->parser, &h->config.parser, q, r);
    if(e)
        return e;
    e = norn_http_build_request(q, h->config.request_buffer, h->config.request_capacity, &length);
    if(e)
        return e;

    /* Request: the head, then the body from the span or the source. */
    start = h->transport.now_ms(h->transport.context);
    e = send_all(h, h->config.request_buffer, length, start);
    if(!e)
        e = q->body_source ? send_source(h, q, start) : send_all(h, q->body, q->body_length, start);

    /* Response: receive and feed the parser until the message is complete. */
    while(!e)
    {
        uint32_t left = budget(h, start);
        int32_t n;
        size_t used;

        if(!left)
        {
            e = NORN_HTTP_TIMEOUT;
            break;
        }
        n = h->transport.recv(h->transport.context, h->config.receive_buffer, h->config.receive_capacity, left);
        if(n == NORN_HTTP_IO_AGAIN)
            continue;
        if(n < 0)
        {
            e = io_error(n);
            break;
        }
        if((size_t)n > h->config.receive_capacity)
        {
            e = NORN_HTTP_TRANSPORT;
            break;
        }
        if(!n)
        {
            e = norn_http_parser_eof(&h->parser);
            break;
        }
        e = norn_http_parser_feed(&h->parser, h->config.receive_buffer, (size_t)n, &used);
        if(e == NORN_HTTP_OK)
        {
            /* No pipelining: unexpected bytes cannot be silently discarded. */
            if(used != (size_t)n)
                e = NORN_HTTP_PROTOCOL;
            break;
        }
        if(e == NORN_HTTP_MORE)
            e = 0;
    }

    if(!e  &&  !budget(h, start))
        e = NORN_HTTP_TIMEOUT;
    if(e)
    {
        /* Any failure after I/O began leaves the connection in an unknown state. */
        r->reusable = false;
        h->failed = true;
    }
    h->reusable = r->reusable;
    return e;
}
