/**
 * @file norn_http.h
 * nornHTTP: a small HTTP/1.1 client for microcontrollers.
 *
 * The library builds a request into a caller-owned buffer, sends it through a
 * caller-supplied transport (norn_http_transport.h), and parses the response
 * incrementally, handing body bytes to a callback as they arrive. It allocates
 * nothing, keeps no global state and never recurses: every buffer and every
 * context is owned by the application and may be static.
 *
 * Two levels are offered. norn_http_init / norn_http_execute run one complete
 * request and response synchronously against the transport, with a total
 * deadline. Below that, norn_http_build_request and the norn_http_parser_*
 * functions are usable on their own, for an event loop or another transport
 * model. Every function returns a norn_http_result; check each one.
 *
 * The behaviour the application can rely on, including the failure modes, is
 * spelled out in docs/contracts.md of the module.
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

/* ---- results ------------------------------------------------------------------------- */

/**
 * Return values of every function. Zero is success, positive asks for more input
 * (parser only), negative is an error. After a negative result from
 * norn_http_execute the connection must be closed, and the norn_http context is
 * STALE until norn_http_init is called again.
 */
typedef enum {
    NORN_HTTP_OK = 0,           /**< Done. */
    NORN_HTTP_MORE = 1,         /**< The parser needs more bytes. */
    NORN_HTTP_INVALID = -1,     /**< An argument or a request field is wrong; nothing was sent. */
    NORN_HTTP_LIMIT = -2,       /**< A buffer or a configured limit was exceeded. */
    NORN_HTTP_PROTOCOL = -3,    /**< The server's response violates HTTP, or carried extra bytes. */
    NORN_HTTP_TRUNCATED = -4,   /**< The connection ended before the message was complete. */
    NORN_HTTP_TIMEOUT = -5,     /**< The total deadline passed. */
    NORN_HTTP_TRANSPORT = -6,   /**< The transport reported an error. */
    NORN_HTTP_CALLBACK = -7,    /**< A body, header or body-source callback returned nonzero. */
    NORN_HTTP_RANGE = -8,       /**< A range request was not answered with exactly that range. */
    NORN_HTTP_STALE = -9        /**< An earlier execute failed: call norn_http_init again. */
} norn_http_result;

/* ---- callbacks ----------------------------------------------------------------------- */

/** One application header: both strings must stay valid until the request is sent. */
typedef struct { const char *name, *value; } norn_http_header;

/** Storage for norn_http_buffer_body: @c data has @c capacity bytes, @c length are used. */
typedef struct { uint8_t *data; size_t capacity, length; } norn_http_body_buffer;

/**
 * Ready-made body callback that appends every span to a norn_http_body_buffer
 * (pass the buffer as the callback context). Overflow returns nonzero, which the
 * parser reports as NORN_HTTP_CALLBACK. Reset @c length before each response. No
 * terminating NUL is added: the body is a byte span, not a string.
 */
int norn_http_buffer_body(void *, const uint8_t *, size_t);

/**
 * Body callback: called with each span of decoded body bytes (chunk framing already
 * removed), in order, zero or more times per response. The span is valid only
 * during the call. Return 0 to continue, nonzero to abort with NORN_HTTP_CALLBACK.
 */
typedef int (*norn_http_body_fn)(void *, const uint8_t *, size_t);

/**
 * Header callback: name and value as byte spans (not NUL-terminated), valid only
 * during the call. Called for every header of every status line, informational
 * responses included, and for trailers with @c trailer true. The status is in the
 * response passed to the parser. Return nonzero to abort with NORN_HTTP_CALLBACK.
 */
typedef int (*norn_http_header_fn)(void *, const char *, size_t, const char *, size_t, bool trailer);

/**
 * Request-body source for a body that is not in RAM as a whole (a file, a sensor
 * log). norn_http_execute calls it after the request head has been sent, with the
 * request buffer as destination: fill 1..capacity bytes, store the count in
 * @c *written and return 0. It is called until body_length bytes have been
 * supplied; the length must be known in advance (it is the Content-Length), chunked
 * upload is not offered. A negative return, zero bytes before the end or a count
 * above the capacity fails the request with NORN_HTTP_CALLBACK, which poisons the
 * connection like any error after I/O began.
 */
typedef int (*norn_http_body_source_fn)(void *, uint8_t *destination, size_t capacity, size_t *written);

/* ---- request and response ------------------------------------------------------------ */

/**
 * A request. Fill it with norn_http_request_init and the setters; the builder
 * validates every field again, so the fields may also be set directly. All strings
 * are borrowed and must stay valid until the request has been sent.
 */
typedef struct {
    const char *method, *target, *host;         /**< Method, path with query, Host header value. */
    norn_http_header *headers;                  /**< Caller-owned storage for custom headers. */
    size_t header_count, header_capacity;       /**< Headers in use and slots available. */
    const void *body;                           /**< Contiguous body, or NULL when body_source is used. */
    size_t body_length;                         /**< Bytes in body, or bytes body_source will supply. */
    norn_http_body_source_fn body_source;       /**< Streamed body, see norn_http_set_body_source. */
    void *body_source_context;                  /**< Passed to body_source. */
    bool close, range;                          /**< Ask the server to close; a Range header is set. */
    uint64_t range_start, range_end;            /**< Closed byte range, inclusive. */
} norn_http_request;

/**
 * What the parser learned about a response. Filled while parsing; complete when
 * execute or the parser returns NORN_HTTP_OK. Informational responses (1xx) are
 * consumed and the fields describe the final one.
 */
typedef struct {
    unsigned status;                            /**< The final status code, 200..599. */
    uint64_t content_length;                    /**< Valid when has_length. */
    uint64_t body_received;                     /**< Decoded body bytes delivered to on_body. */
    uint64_t range_start, range_end, range_total;   /**< From Content-Range, valid when has_range (total also on 416). */
    bool has_length;                            /**< Content-Length was present. */
    bool chunked;                               /**< Transfer-Encoding: chunked framed the body. */
    bool reusable;                              /**< The connection may carry another request. */
    bool has_range;                             /**< Content-Range with a byte range was present. */
} norn_http_response;

/* ---- parser -------------------------------------------------------------------------- */

/**
 * Parser configuration: the line buffer and the limits. norn_http_parser_config_init
 * fills in the defaults from norn_http_config.h; set the callbacks afterwards.
 */
typedef struct {
    char *line_buffer;                  /**< Holds one header line at a time; its size bounds the longest accepted header. */
    size_t line_capacity;               /**< Bytes in line_buffer. */
    size_t max_header_bytes;            /**< Cumulative metadata bytes per message, 1xx and trailers included. */
    size_t max_headers;                 /**< Headers accepted per message, trailers included. */
    uint64_t max_body_bytes;            /**< Largest body accepted; zero: unlimited. */
    norn_http_body_fn on_body;          /**< Receives the body; NULL discards it. */
    norn_http_header_fn on_header;      /**< Sees every header; NULL ignores them. */
    void *callback_context;             /**< Passed to both callbacks. */
} norn_http_parser_config;

/**
 * The response parser. Public so that it can be allocated statically; the fields
 * after @c response are implementation state and must not be touched.
 */
typedef struct {
    norn_http_parser_config config;
    norn_http_response *response;
    size_t line_length, header_bytes, headers;
    uint64_t remaining, expected_start, expected_end;
    int state, error;
    bool cr, head, range, close, seen_te, seen_cl, seen_range, http10;
} norn_http_parser;

/* ---- synchronous client -------------------------------------------------------------- */

/** Everything norn_http_execute needs besides the transport. All memory is the caller's. */
typedef struct {
    norn_http_parser_config parser;     /**< Line buffer, limits and callbacks for the response. */
    uint8_t *receive_buffer;            /**< One recv() at a time lands here; any size from 1 byte up. */
    size_t receive_capacity;            /**< Bytes in receive_buffer. */
    char *request_buffer;               /**< The request head is built here; a body source fills it too. */
    size_t request_capacity;            /**< Bytes in request_buffer; bounds the longest request head. */
    uint32_t timeout_ms;                /**< Total deadline for one execute, 1..INT32_MAX. */
} norn_http_config;

/**
 * The client context: one per connection. Public for static allocation; the
 * configuration and the transport are copied in by norn_http_init.
 */
typedef struct {
    norn_http_config config;
    norn_http_transport transport;
    norn_http_parser parser;
    bool reusable;      /**< After a successful execute: the connection may be used again. */
    bool failed;        /**< An execute failed; the context is STALE until norn_http_init. */
} norn_http;

/* ---- building a request -------------------------------------------------------------- */

/**
 * Clears the request and sets the method, target, host and the storage for custom
 * headers (@c storage may be NULL with @c capacity 0 when no header is added).
 * Methods: GET, POST, PUT, DELETE, PATCH, HEAD and OPTIONS. The target starts with
 * '/' (or is "*" for OPTIONS); the host is the Host header value, "name:port".
 */
void norn_http_request_init(norn_http_request *, const char *method, const char *target,
                            const char *host, norn_http_header *storage, size_t capacity);

/**
 * Adds a custom header. The name must be a token, the value free of control
 * characters other than HTAB. Host, Content-Length, Transfer-Encoding, Connection,
 * Range, Expect and Upgrade are reserved and rejected with NORN_HTTP_INVALID; a
 * full header array gives NORN_HTTP_LIMIT.
 */
int norn_http_add_header(norn_http_request *, const char *, const char *);

/**
 * Sets a contiguous body (binary allowed). Content-Length is written from the
 * length, which is also written for POST, PUT and PATCH without a body. Replaces a
 * body source.
 */
int norn_http_set_body(norn_http_request *, const void *, size_t);

/**
 * Streams the body from a source callback instead of a buffer: @c length is the
 * total and becomes the Content-Length. Replaces a contiguous body. See
 * norn_http_body_source_fn for the callback's contract.
 */
int norn_http_set_body_source(norn_http_request *, norn_http_body_source_fn, void *context, size_t length);

/**
 * Asks for the closed byte range start..end (inclusive) with a Range header; GET
 * only. The response must then be 206 with exactly that range, else NORN_HTTP_RANGE.
 */
int norn_http_set_range(norn_http_request *, uint64_t start, uint64_t end);

/**
 * Writes the request head (request line, headers, the empty line) into the buffer
 * and stores the byte count in @c *written. No NUL is added and the body is not
 * included. Every field is validated; on failure the partial output must not be
 * sent. norn_http_execute calls this itself.
 */
int norn_http_build_request(const norn_http_request *, char *, size_t, size_t *written);

/* ---- parsing a response -------------------------------------------------------------- */

/** Clears the configuration, sets the line buffer and the defaults from norn_http_config.h. */
void norn_http_parser_config_init(norn_http_parser_config *, char *, size_t);

/**
 * Prepares the parser for the response to one request (it needs the method and the
 * range to know what to expect) and clears the response. The configuration is
 * copied; the response pointer is kept and written to while parsing.
 */
int norn_http_parser_init(norn_http_parser *, const norn_http_parser_config *,
                          const norn_http_request *, norn_http_response *);

/**
 * Feeds received bytes. Returns NORN_HTTP_MORE while the message is incomplete,
 * NORN_HTTP_OK when it is complete, or an error. @c *consumed is how many of the
 * bytes were used: it stops at the message boundary, so an event loop must retain
 * the excess for the next message (norn_http_execute treats excess as PROTOCOL).
 * EOF must be signalled separately with norn_http_parser_eof. Errors are sticky and
 * the connection must then be closed.
 */
int norn_http_parser_feed(norn_http_parser *, const void *, size_t, size_t *consumed);

/**
 * The connection ended. Completes a close-delimited body (NORN_HTTP_OK), marks a
 * completed message as not reusable, and is NORN_HTTP_TRUNCATED anywhere else.
 */
int norn_http_parser_eof(norn_http_parser *);

/* ---- running a request --------------------------------------------------------------- */

/**
 * Binds the context to a configuration and a transport (both copied) and clears the
 * STALE state. Every buffer and limit in the configuration must be set; the transport
 * must have all three functions. Returns NORN_HTTP_INVALID otherwise.
 */
int norn_http_init(norn_http *, const norn_http_config *, const norn_http_transport *);

/**
 * Runs one request and parses its response, within config.timeout_ms in total.
 * The request is validated and built before anything is sent, so NORN_HTTP_INVALID
 * and NORN_HTTP_LIMIT from the builder leave the connection untouched. Any failure
 * after that leaves the connection in an unknown state: close it, and call
 * norn_http_init before using the context again (it answers NORN_HTTP_STALE until
 * then). On NORN_HTTP_OK, response->reusable says whether the same connection may
 * carry the next request; the HTTP status is in response->status and is not an
 * error to the library.
 */
int norn_http_execute(norn_http *, const norn_http_request *, norn_http_response *);

#ifdef __cplusplus
}
#endif
#endif
