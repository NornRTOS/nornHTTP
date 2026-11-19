#include "norn_http.h"
#include <string.h>
int norn_http_buffer_body(void *context, const uint8_t *data, size_t length) {
    norn_http_body_buffer *b=context;
    if(!b || (!data && length) || (!b->data && length) ||
       b->length>b->capacity || length>b->capacity-b->length) return -1;
    if(length) memcpy(b->data+b->length,data,length);
    b->length+=length; return 0;
}
