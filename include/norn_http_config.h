/**
 * @file norn_http_config.h
 * Defaults that norn_http_parser_config_init fills in; an application overrides them
 * in its own config fields after the call. Nothing here is a hard compile-time limit.
 */
#ifndef NORN_HTTP_CONFIG_H
#define NORN_HTTP_CONFIG_H
#define NORN_HTTP_DEFAULT_HEADERS      32u      /**< Response headers accepted per message, trailers included. */
#define NORN_HTTP_DEFAULT_HEADER_BYTES 8192u    /**< Bytes of response metadata accepted per message. */
#define NORN_HTTP_DEFAULT_TIMEOUT_MS   30000u   /**< A reasonable total deadline for one execute. */
#endif
