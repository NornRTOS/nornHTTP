/* Host demonstration: ./posix_client 127.0.0.1 8080 /path [GET|POST|PUT|...]
 * Plain TCP only. Numeric IPv4 deliberately avoids introducing DNS in the core. */
#define _POSIX_C_SOURCE 200809L
#include "norn_http.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static int ready(int fd, short events, uint32_t timeout) {
    struct pollfd p={fd,events,0}; int n=poll(&p,1,(int)timeout);
    if(n==0) return NORN_HTTP_IO_TIMEOUT;
    if(n<0) return errno==EINTR ? NORN_HTTP_IO_AGAIN : NORN_HTTP_IO_ERROR;
    return 0;
}
static int32_t tx(void *arg,const void *p,size_t n,uint32_t timeout) {
    int fd=*(int *)arg,e=ready(fd,POLLOUT,timeout); ssize_t sent;
    if(e) return e;
    sent=send(fd,p,n,MSG_NOSIGNAL|MSG_DONTWAIT);
    return sent<0 ? (errno==EAGAIN || errno==EINTR ? NORN_HTTP_IO_AGAIN : NORN_HTTP_IO_ERROR) : (int32_t)sent;
}
static int32_t rx(void *arg,void *p,size_t n,uint32_t timeout) {
    int fd=*(int *)arg,e=ready(fd,POLLIN,timeout); ssize_t received;
    if(e) return e;
    received=recv(fd,p,n,MSG_DONTWAIT);
    return received<0 ? (errno==EAGAIN || errno==EINTR ? NORN_HTTP_IO_AGAIN : NORN_HTTP_IO_ERROR) : (int32_t)received;
}
static uint32_t now_ms(void *arg) {
    struct timespec t; (void)arg; (void)clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint32_t)((uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u);
}
static int output(void *arg,const uint8_t *p,size_t n) { (void)arg; return fwrite(p,1,n,stdout)==n ? 0 : -1; }
int main(int argc,char **argv) {
    int fd,e; struct sockaddr_in addr; char line[512],request[1024],host[128]; uint8_t receive[1024];
    norn_http h; norn_http_config c={0}; norn_http_request q; norn_http_response r={0}; norn_http_header headers[2]; norn_http_transport t;
    if(argc<4) { fprintf(stderr,"usage: %s IPv4 port path [method]\n",argv[0]); return 2; }
    memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons((uint16_t)strtoul(argv[2],NULL,10));
    if(inet_pton(AF_INET,argv[1],&addr.sin_addr)!=1) return 2;
    fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return 2;
    if(connect(fd,(struct sockaddr *)&addr,sizeof(addr))) { perror("connect"); close(fd); return 2; }
    t=(norn_http_transport){&fd,tx,rx,now_ms};
    norn_http_parser_config_init(&c.parser,line,sizeof(line)); c.parser.on_body=output;
    c.receive_buffer=receive; c.receive_capacity=sizeof(receive); c.request_buffer=request; c.request_capacity=sizeof(request); c.timeout_ms=5000;
    (void)snprintf(host,sizeof(host),"%s:%s",argv[1],argv[2]);
    norn_http_request_init(&q,argc>4 ? argv[4] : "GET",argv[3],host,headers,2);
    if(!strcmp(q.method,"POST")) {
        (void)norn_http_add_header(&q,"Content-Type","application/json");
        (void)norn_http_set_body(&q,"{\"item\":42}",11);
    }
    if(!strcmp(argv[3],"/range")) (void)norn_http_set_range(&q,2,4);
    e=norn_http_init(&h,&c,&t); if(!e) e=norn_http_execute(&h,&q,&r);
    fprintf(stderr,"result=%d status=%u bytes=%llu reusable=%d\n",e,r.status,(unsigned long long)r.body_received,r.reusable);
    close(fd); return e ? 1 : 0;
}
