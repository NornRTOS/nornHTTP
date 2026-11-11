/* Shared by the parser and the request builder; not part of the public API. */
#ifndef NORN_HTTP_INTERNAL_H
#define NORN_HTTP_INTERNAL_H
#include "norn_http.h"
#include <string.h>

static inline unsigned http_lower(unsigned c)
{
    return c >= 'A'  &&  c <= 'Z' ? c + 32u : c;
}

/* Case-insensitive comparison of a byte span with a NUL-terminated name. */
static inline bool http_equal(const char *a, size_t n, const char *b)
{
    size_t i;

    if(strlen(b) != n)
        return false;
    for(i = 0; i < n; ++i)
        if(http_lower((unsigned char)a[i]) != http_lower((unsigned char)b[i]))
            return false;
    return true;
}

/* RFC 9110 token characters: header names, chunk extension names, Connection options. */
static inline bool http_token(unsigned c)
{
    return (c >= '0'  &&  c <= '9')  ||  (c >= 'a'  &&  c <= 'z')  ||
           (c >= 'A'  &&  c <= 'Z')  ||  (c != 0  &&  strchr("!#$%&'*+-.^_`|~", (int)c) != NULL);
}
#endif
