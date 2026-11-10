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
typedef enum {
    NORN_HTTP_OK = 0, NORN_HTTP_MORE = 1,
    NORN_HTTP_INVALID = -1, NORN_HTTP_LIMIT = -2,
    NORN_HTTP_PROTOCOL = -3, NORN_HTTP_TRUNCATED = -4,
    NORN_HTTP_TIMEOUT = -5, NORN_HTTP_TRANSPORT = -6,
    NORN_HTTP_CALLBACK = -7, NORN_HTTP_RANGE = -8,
    NORN_HTTP_STALE = -9 /* an earlier execute failed: call norn_http_init again */
} norn_http_result;
typedef struct { const char *name, *value; } norn_http_header;
typedef struct { uint8_t *data; size_t capacity, length; } norn_http_body_buffer;
/* Ready-made on_body callback. Overflow returns nonzero (CALLBACK from parser).
 * Reset length before each response. No terminating NUL is added. */
int norn_http_buffer_body(void *, const uint8_t *, size_t);
typedef int (*norn_http_body_fn)(void *, const uint8_t *, size_t);
/* Views are valid only during this callback. Includes informational headers and
 * trailers; status is available through the response passed to parser_init. */
typedef int (*norn_http_header_fn)(void *, const char *, size_t, const char *, size_t, bool trailer);
typedef struct {
    const char *method, *target, *host;
    norn_http_header *headers;
    size_t header_count, header_capacity;
    const void *body;
    size_t body_length;
    bool close, range;
    uint64_t range_start, range_end;
} norn_http_request;
typedef struct {
    unsigned status;
    uint64_t content_length, body_received, range_start, range_end, range_total;
    bool has_length, chunked, reusable, has_range;
} norn_http_response;
typedef struct {
    char *line_buffer;
    size_t line_capacity, max_header_bytes, max_headers;
    uint64_t max_body_bytes; /* zero: unlimited */
    norn_http_body_fn on_body;
    norn_http_header_fn on_header;
    void *callback_context;
} norn_http_parser_config;
/* Public for static allocation; fields below are implementation state. */
typedef struct {
    norn_http_parser_config config;
    norn_http_response *response;
    size_t line_length, header_bytes, headers;
    uint64_t remaining, expected_start, expected_end;
    int state, error;
    bool cr, head, range, close, seen_te, seen_cl, seen_range, http10;
} norn_http_parser;
typedef struct {
    norn_http_parser_config parser;
    uint8_t *receive_buffer;
    size_t receive_capacity;
    char *request_buffer;
    size_t request_capacity;
    uint32_t timeout_ms; /* total execute deadline, 1..INT32_MAX */
} norn_http_config;
typedef struct {
    norn_http_config config;
    norn_http_transport transport;
    norn_http_parser parser;
    bool reusable, failed;
} norn_http;
void norn_http_request_init(norn_http_request *, const char *method, const char *target,
                            const char *host, norn_http_header *storage, size_t capacity);
int norn_http_add_header(norn_http_request *, const char *, const char *);
int norn_http_set_body(norn_http_request *, const void *, size_t);
int norn_http_set_range(norn_http_request *, uint64_t start, uint64_t end);
int norn_http_build_request(const norn_http_request *, char *, size_t, size_t *written);
void norn_http_parser_config_init(norn_http_parser_config *, char *, size_t);
int norn_http_parser_init(norn_http_parser *, const norn_http_parser_config *,
                          const norn_http_request *, norn_http_response *);
/* consumed stops at the message boundary. Retain excess bytes in event loops.
 * EOF must be signalled separately. Errors are sticky; connection must be closed. */
int norn_http_parser_feed(norn_http_parser *, const void *, size_t, size_t *consumed);
int norn_http_parser_eof(norn_http_parser *);
int norn_http_init(norn_http *, const norn_http_config *, const norn_http_transport *);
int norn_http_execute(norn_http *, const norn_http_request *, norn_http_response *);
#ifdef __cplusplus
}
#endif
#endif
