/**
 * @file norn_http_transport.h
 * The transport nornHTTP talks through: three functions over a connected byte stream.
 *
 * The application supplies them (plain TCP, TLS, a test mock); the adapter in
 * adapters/norn_net_transport.c does so for a connected nornNET socket. Each call must
 * return within its timeout. Positive returns are bytes transferred, zero is an orderly
 * EOF from recv (never valid from send), negative is one of the NORN_HTTP_IO_ codes.
 */
#ifndef NORN_HTTP_TRANSPORT_H
#define NORN_HTTP_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>

#define NORN_HTTP_IO_ERROR   (-1)   /**< The connection failed; nornHTTP reports TRANSPORT. */
#define NORN_HTTP_IO_TIMEOUT (-2)   /**< Nothing within the timeout; nornHTTP reports TIMEOUT. */
#define NORN_HTTP_IO_AGAIN   (-3)   /**< Interrupted or not ready after waiting; nornHTTP retries.
                                         Return it only after yielding or waiting, never from a busy spin. */

/** A connected transport. @c context is passed to each function. */
typedef struct {
    void *context;
    int32_t (*send)(void *, const void *, size_t, uint32_t timeout_ms);   /**< Sends up to the given count; may send less. */
    int32_t (*recv)(void *, void *, size_t, uint32_t timeout_ms);         /**< Receives up to the given count; 0 is EOF. */
    uint32_t (*now_ms)(void *);                                           /**< Monotonic milliseconds; may wrap. */
} norn_http_transport;
#endif
