/**
 * @file norn_http.h
 * nornHTTP: an HTTP/1.1 client in portable C99 with nothing underneath it. No heap, no
 * global state, no recursion, no socket: the application owns every buffer and context
 * and hands the library three functions (send, receive, a millisecond clock) in a
 * norn_http_transport. Two levels: norn_http_execute() runs one request and response on
 * a connected transport; norn_http_parser_feed() is the incremental parser alone, for
 * an application with its own event loop. The contracts behind every rule here are in
 * docs/contracts.md.
 */
#ifndef NORN_HTTP_H
#define NORN_HTTP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "norn_http_transport.h"
#include "norn_http_config.h"
#ifdef __cplusplus
extern "C" {
#endif

/** Result of every call. Zero is success; the HTTP status is a separate matter
    (norn_http_response::status). Negative results from norn_http_execute() after I/O
    began leave the connection in an unknown state: abort it and call norn_http_init()
    again. */
typedef enum {
    NORN_HTTP_OK = 0,           /**< Done. */
    NORN_HTTP_MORE = 1,         /**< Parser only: more input is needed. */
    NORN_HTTP_INVALID = -1,     /**< An argument or a field of the request is wrong. */
    NORN_HTTP_LIMIT = -2,       /**< A caller-chosen capacity or limit was reached. */
    NORN_HTTP_PROTOCOL = -3,    /**< The response violates HTTP, or bytes followed it. */
    NORN_HTTP_TRUNCATED = -4,   /**< EOF inside a message. */
    NORN_HTTP_TIMEOUT = -5,     /**< The transaction deadline passed. */
    NORN_HTTP_TRANSPORT = -6,   /**< The transport reported an error or misbehaved. */
    NORN_HTTP_CALLBACK = -7,    /**< A body, header or body source callback failed. */
    NORN_HTTP_RANGE = -8,       /**< A range request was not answered as asked. */
    NORN_HTTP_STALE = -9        /**< An earlier execute failed: call norn_http_init() again. */
} norn_http_result;

/** One request header: both strings must outlive the request. */
typedef struct { const char *name, *value; } norn_http_header;

/** Caller-owned storage for norn_http_buffer_body(). */
typedef struct { uint8_t *data; size_t capacity, length; } norn_http_body_buffer;

/** Ready-made body callback: appends to a norn_http_body_buffer given as the callback
    context. Overflow returns nonzero (the parser then reports NORN_HTTP_CALLBACK). Reset
    `length` before each response. No terminating NUL is added. */
int norn_http_buffer_body(void *, const uint8_t *, size_t);

/** Receives each span of decoded body bytes as it arrives (chunk payload, or fixed or
    close-delimited payload). The span is valid only during the call. Return nonzero to
    abort with NORN_HTTP_CALLBACK. Data is provisional until the call that delivered it
    reports NORN_HTTP_OK. */
typedef int (*norn_http_body_fn)(void *, const uint8_t *, size_t);

/** Receives each header as (name, name length, value, value length, trailer). The
    views are valid only during the call; names keep their case, values have their
    surrounding whitespace removed. Informational headers and trailers are included; the
    status is in the response passed to norn_http_parser_init(). Observational only:
    framing and range checks finish at the end of the headers. */
typedef int (*norn_http_header_fn)(void *, const char *, size_t, const char *, size_t, bool trailer);

/** Fills a span of the request body when the body is streamed from a source rather
    than kept in RAM as a whole (norn_http_set_body_source()). Called with the request
    buffer and the number of bytes wanted, which is never more than what remains of the
    announced length; returns the number of bytes written, 1 to the number wanted.
    Anything else aborts the request with NORN_HTTP_CALLBACK. */
typedef int32_t (*norn_http_body_source_fn)(void *, uint8_t *, size_t);

/** A request. Fill it with norn_http_request_init() and the setters; the builder
    revalidates every field, so direct edits are allowed. */
typedef struct {
    const char *method, *target, *host;     /**< Method, origin-form target, Host value. */
    norn_http_header *headers;              /**< Caller's array of custom headers. */
    size_t header_count, header_capacity;
    const void *body;                       /**< A body in RAM, or NULL. */
    size_t body_length;                     /**< Length of the body, in RAM or streamed. */
    norn_http_body_source_fn body_source;   /**< A streamed body, or NULL. */
    void *body_source_context;
    bool close;                             /**< Ask the server to close after this one. */
    bool range;                             /**< Set by norn_http_set_range(). */
    uint64_t range_start, range_end;
} norn_http_request;

/** What the response said, filled by the parser. */
typedef struct {
    unsigned status;                        /**< The final status, 100..599. */
    uint64_t content_length;                /**< Valid when has_length. */
    uint64_t body_received;                 /**< Decoded body bytes delivered so far. */
    uint64_t range_start, range_end, range_total;   /**< Valid when has_range. */
    bool has_length, chunked;
    bool reusable;                          /**< The connection may carry another request. */
    bool has_range;
} norn_http_response;

/** How the parser works: its line buffer, its limits and its callbacks. */
typedef struct {
    char *line_buffer;                      /**< Longest header or chunk line, without CRLF. */
    size_t line_capacity, max_header_bytes, max_headers;
    uint64_t max_body_bytes;                /**< Zero: unlimited. */
    norn_http_body_fn on_body;              /**< NULL discards the body while counting it. */
    norn_http_header_fn on_header;          /**< NULL ignores the headers. */
    void *callback_context;
} norn_http_parser_config;

/** The incremental parser. Public for static allocation; the fields below `response`
    are implementation state. */
typedef struct {
    norn_http_parser_config config;
    norn_http_response *response;
    size_t line_length, header_bytes, headers;
    uint64_t remaining, expected_start, expected_end;
    int state, error;
    bool cr, head, range, close, seen_te, seen_cl, seen_range, http10;
} norn_http_parser;

/** Configuration of the synchronous client: the parser's, the two buffers and the
    deadline. The receive buffer takes one transport read at a time (it bounds nothing
    about the response); the request buffer holds the request head, and the spans of a
    streamed body. */
typedef struct {
    norn_http_parser_config parser;
    uint8_t *receive_buffer;
    size_t receive_capacity;
    char *request_buffer;
    size_t request_capacity;
    uint32_t timeout_ms;                    /**< Total deadline of one execute, 1..INT32_MAX. */
} norn_http_config;

/** The synchronous client. Public for static allocation. */
typedef struct {
    norn_http_config config;
    norn_http_transport transport;
    norn_http_parser parser;
    bool reusable;                          /**< After an execute: the same as the response's. */
    bool failed;                            /**< Poisoned: see NORN_HTTP_STALE. */
} norn_http;

/** Starts a request: method (GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS), target in
    origin form (`/path?query`, or `*` for OPTIONS), the Host value (with a nondefault
    port), and the caller's header array of `capacity` slots (may be NULL, 0). */
void norn_http_request_init(norn_http_request *, const char *method, const char *target,
                            const char *host, norn_http_header *storage, size_t capacity);
/** Adds a custom header. Host, Content-Length, Transfer-Encoding, Connection, Range,
    Expect and Upgrade are the library's and are rejected (INVALID); a full array is
    LIMIT. */
int norn_http_add_header(norn_http_request *, const char *, const char *);
/** A body in RAM, sent after the head. Replaces a body source. */
int norn_http_set_body(norn_http_request *, const void *, size_t);
/** A body of `length` bytes streamed from `source` through the request buffer, span by
    span, while the head announces the length. Replaces a body in RAM. */
int norn_http_set_body_source(norn_http_request *, norn_http_body_source_fn source, void *context, size_t length);
/** A closed byte range `start-end` for a GET; the response must be a matching 206. */
int norn_http_set_range(norn_http_request *, uint64_t start, uint64_t end);
/** Writes the request head (through the final CRLF CRLF, no NUL) into `buffer`, or
    returns INVALID or LIMIT with `*written` zero; a partial head must not be sent. */
int norn_http_build_request(const norn_http_request *, char *, size_t, size_t *written);

/** Fills a parser configuration with the defaults of norn_http_config.h over the given
    line buffer; set the callbacks afterwards. */
void norn_http_parser_config_init(norn_http_parser_config *, char *, size_t);
/** Prepares the parser for the response to `request` (HEAD and ranges change the
    framing), writing into `response`. */
int norn_http_parser_init(norn_http_parser *, const norn_http_parser_config *,
                          const norn_http_request *, norn_http_response *);
/** Feeds received bytes. MORE wants more; OK means one final response completed and
    `*consumed` stops at its boundary (keep the rest for the next message); a negative
    result is sticky and the connection cannot be reused. Signal EOF separately. */
int norn_http_parser_feed(norn_http_parser *, const void *, size_t, size_t *consumed);
/** Orderly EOF from the peer: completes a close-delimited body, or is TRUNCATED. */
int norn_http_parser_eof(norn_http_parser *);

/** Binds a client to its configuration and transport, and clears a poisoned state.
    Every pointer and capacity is checked (INVALID). */
int norn_http_init(norn_http *, const norn_http_config *, const norn_http_transport *);
/** One request and its response within `timeout_ms`, body callbacks included. A
    negative result after I/O began poisons the context (STALE) and means the connection
    must be aborted; INVALID or LIMIT from the request itself is reported before anything
    is sent and leaves both alone. */
int norn_http_execute(norn_http *, const norn_http_request *, norn_http_response *);
#ifdef __cplusplus
}
#endif
#endif
