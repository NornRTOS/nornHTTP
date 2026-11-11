# API and protocol contracts

All contexts, buffers, header arrays, strings, bodies and transports belong to
the application. Keep them alive for the operation. Buffers must not overlap;
callbacks must not re-enter or mutate the active context. Independent contexts
can run concurrently; shared-context synchronization is the caller's job.

## Memory and callbacks

`line_capacity` is the maximum line length excluding CRLF; the parser never
adds a terminating NUL. Header callbacks receive case-preserving name/value
views valid only for that invocation, with surrounding value whitespace
removed. Informational headers and trailers are also delivered. Header callbacks
are observational: framing and range validation finish at the end of headers.
Do not act on a single header as proof that the response is valid.

`on_body` receives decoded chunk payload, or fixed/close-delimited payload,
directly from the incoming span. A missing callback discards data while counting
bytes. Nonzero callback returns abort with `NORN_HTTP_CALLBACK`. The provided
`norn_http_buffer_body` callback uses caller-owned storage, rejects overflow,
and requires length to be reset for a new response. No NUL is appended.

Streaming data is provisional until execute/feed reports successful completion;
truncation, invalid trailers, timeout or callback failure can follow earlier
body callbacks. Downloads requiring atomicity must stage data and commit after
success (and verify a checksum/signature when applicable).

Limits: line size is caller-selected, header bytes default to 8192, header count
to 32. Counts include all informational responses and trailers, preventing
unbounded 1xx chains. Chunk metadata has a line-size limit; set a transaction
deadline and optionally `max_body_bytes` for an application-specific bound.
No hidden allocations, recursion, global HTTP state or large stack buffers.

## Requests

Requests use origin-form targets beginning `/`; OPTIONS also accepts `*`.
Targets must be ASCII without spaces, controls or fragments; percent-encode
other characters beforehand. Supply a nonempty Host (including a nondefault
port and IPv6 brackets when needed). No URI parsing or hostname resolution.

Host, Content-Length, Connection and Range are generated from typed fields;
attempts to add them through custom headers are rejected. Transfer-Encoding,
Expect and Upgrade are reserved and unsupported. Custom values reject control
characters other than HTAB; names must be HTTP tokens. The builder revalidates
all fields, including directly modified header arrays. It writes headers only,
returns the byte count, and adds no NUL. Its partial output on failure must not
be sent. Content-Length is written when there is a body and always for POST, PUT
and PATCH; GET, HEAD, DELETE and OPTIONS without a body carry none. Execute
builds/validates headers before sending any bytes, then sends the body, handling
partial sends.

A request body is either a contiguous caller-owned span (`norn_http_set_body`) or
a body source (`norn_http_set_body_source`): a callback that fills the request
buffer with 1..capacity bytes per call until the declared length has been sent.
The two exclude each other; each setter replaces the other, and a request carrying
both is INVALID. The length is declared in advance and written as Content-Length;
there is no chunked upload. The source is first called after the head has been
sent and is never called for a zero-length body. A negative return, zero bytes
before the end or a count above the capacity is CALLBACK, and because bytes have
already gone out the context is poisoned (STALE until re-init) and the connection
must be closed. A transport failure while sending the body is reported as that
failure, not as CALLBACK.

## Response framing and ranges

HTTP/1.1 and HTTP/1.0 status lines with codes 100..599 are accepted; the reason
phrase may be absent. An HTTP/1.0 response may not use Transfer-Encoding, ends on
Content-Length or on EOF, and never leaves the connection reusable. Informational
responses are followed until the final response; 101 protocol switching is
rejected. HEAD, 204 and 304 have no body. Content-Length on 204 and informational
responses is rejected. HEAD/304 may carry representation length metadata.

A single `Transfer-Encoding: chunked` is supported, case-insensitively. Other
transfer codings and repeated Transfer-Encoding are rejected. TE plus CL is
always rejected. Identical separate Content-Length fields are allowed;
conflicting values, comma lists, signs and overflow are rejected. Empty elements
in the Connection list are ignored. Without TE/CL,
a body ends only on orderly EOF and the connection cannot be reused.

Chunk sizes have checked hexadecimal arithmetic, whitespace after the size is
tolerated, extensions are syntactically validated and ignored, and CRLF is
mandatory. Trailer fields use the same
syntax and limits as headers; framing/routing and selected representation or
authentication fields cannot override earlier headers. No obsolete folded
headers or bare LF is accepted. Application-specific trailer semantics remain
the application's responsibility.

`norn_http_set_range` supports a closed GET range `bytes=start-end`; there is
no multipart, suffix or open-ended range API. Before any body callback, a range
request requires status 206, an exact matching Content-Range with numeric total,
and a matching Content-Length if present. The body must have exactly the
expected length when completed. A server that clamps the requested end is
rejected: callers must request a known interval. A 200 fallback and a 416 are
range errors. Ordinary requests can parse `Content-Range: bytes */total` on 416.
Content-Encoding is not decoded; use identity encoding for byte downloads.

## Transport, deadlines and connection lifetime

Transport functions return a positive count no larger than requested, zero on
orderly EOF, or IO_ERROR/IO_TIMEOUT/IO_AGAIN. Send returning zero is a transport
error. AGAIN means a temporary lack of progress; the adapter must wait or yield
before returning it. The monotonic uint32 clock may wrap. Execute uses a single
total deadline across all sends and receives (1..INT32_MAX milliseconds).
Callbacks and transport calls must return promptly; the library cannot preempt
a blocked callback or a transport that ignores its supplied timeout.

EOF in an incomplete status/header/fixed/chunked message gives TRUNCATED;
connection-close framing succeeds on EOF. Parser errors are sticky and mark
the connection nonreusable. After any error once I/O has begun, the application
must close/abort the connection; execute does not own or close transports. The
context remembers the failure: every later `norn_http_execute` returns
`NORN_HTTP_STALE` until `norn_http_init` is called again, so a connection whose
message boundaries are unknown cannot be reused by mistake. Argument errors
before any I/O do not poison the context.
For nornNET this normally means `nn_tcpAbort` on failure, and the application's
normal `nn_tcpClose` lifecycle for orderly completion.

Successful framing permits keep-alive unless the request/response says close
or the body is close-delimited. This is permission to reuse, not a guarantee
that the peer will stay connected. There is no automatic reconnect or retry.
The synchronous API is sequential, without pipelining: bytes beyond the final
response in the same receive call are a protocol error. The incremental API
instead exposes the precise message boundary through `consumed`.

HTTPS uses an external TLS adapter. It must verify the server's certificate
chain and hostname/server identity, and map TLS shutdown/errors correctly.
TCP, TLS state, DNS, authentication policy and certificate storage remain
outside nornHTTP.
