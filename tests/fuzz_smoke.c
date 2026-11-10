/* Reproducible mutation smoke test; complements the coverage-guided harness. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
int LLVMFuzzerTestOneInput(const uint8_t *,size_t);
static uint32_t state=0x68747470u;
static uint32_t random32(void) { state^=state<<13; state^=state>>17; state^=state<<5; return state; }
int main(void) {
    /* The first byte is the harness mode (HEAD, range, close, small body limit). */
    static const char *seeds[]={
        "\0HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello",
        "\0HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
        "\0HTTP/1.1 103 Early Hints\r\nLink: x\r\n\r\nHTTP/1.1 204 No Content\r\n\r\n",
        "\2HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 2-4/5\r\n\r\nabc"
    }; uint8_t data[512]; unsigned i,j;
    for(i=0;i<100000u;++i) {
        const char *s=seeds[i%4u]; size_t n=strlen(s+1)+1u; memcpy(data,s,n);
        for(j=0;j<i%16u;++j) data[1u+random32()%(uint32_t)(n-1u)]=(uint8_t)random32();
        if(i%3u==0) n=1u+random32()%(uint32_t)n;
        if(i%5u==0) { n=random32()%sizeof(data); for(j=0;j<n;++j) data[j]=(uint8_t)random32(); }
        LLVMFuzzerTestOneInput(data,n);
    }
    puts("PASS: 100000 deterministic parser mutations"); return 0;
}
