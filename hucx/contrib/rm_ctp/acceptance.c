/* Two-node byte-verifying UCP acceptance. TCP exchanges addresses/rkeys only. */
#define _POSIX_C_SOURCE 200809L
#include <ucp/api/ucp.h>
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_TAG (8u*1024*1024)
#define RMA_BYTES (1024u*1024)
static ucp_worker_h worker;
static ucp_ep_h ep;
static unsigned side, iterations=5, timeout_sec=30;
static uint64_t verified;

/* All callbacks are progressed by the SINGLE worker used by this demo. */
typedef struct {
    ucs_status_t status;
    unsigned callback_count;
} acceptance_ep_error_t;

static acceptance_ep_error_t endpoint_error = {UCS_OK, 0};

static void on_endpoint_error(void *arg, ucp_ep_h failed_ep,
                              ucs_status_t error_status)
{
    acceptance_ep_error_t *error = arg;

    (void)failed_ep;
    ++error->callback_count;
    if (error->status == UCS_OK) {
        error->status = error_status;
    }

    /* Record the error here; exit only after worker_progress has returned. */
    fprintf(stderr, "RM_CTP_EP_ERROR side=%u status=%s callbacks=%u\n",
            side, ucs_status_string(error_status), error->callback_count);
}

static void die(const char *op) { fprintf(stderr,"FAIL side=%u %s\n",side,op); exit(1); }
static void status(ucs_status_t s,const char *op)
{ if(s!=UCS_OK) { fprintf(stderr,"FAIL %s: %s\n",op,ucs_status_string(s)); exit(1); } }
static double seconds(void)
{ struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)) die("clock"); return t.tv_sec+t.tv_nsec*1e-9; }
static void check_endpoint_error(const char *operation, double start_time)
{
    if (endpoint_error.status == UCS_OK) {
        return;
    }

    fprintf(stderr, "FAIL side=%u %s: endpoint_error=%s elapsed=%.6f "
                    "callbacks=%u\n",
            side, operation, ucs_status_string(endpoint_error.status),
            seconds() - start_time, endpoint_error.callback_count);
    exit(EXIT_FAILURE);
}

static void wait_request(void *request, const char *operation)
{
    double start_time;
    ucs_status_t request_status;

    start_time = seconds();
    check_endpoint_error(operation, start_time);
    if (UCS_PTR_IS_ERR(request)) {
        status(UCS_PTR_STATUS(request), operation);
    }
    if (request == NULL) {
        return;
    }

    for (;;) {
        check_endpoint_error(operation, start_time);
        request_status = ucp_request_check_status(request);
        if (request_status != UCS_INPROGRESS) {
            break;
        }

        ucp_worker_progress(worker);
        check_endpoint_error(operation, start_time);
        if (seconds() - start_time > timeout_sec) {
            die("UCP request deadline exceeded");
        }
    }

    ucp_request_free(request);
    status(request_status, operation);
}
static void put_tag(void *buf,size_t n,uint64_t tag)
{ ucp_request_param_t p={0}; wait_request(ucp_tag_send_nbx(ep,buf,n,tag,&p),"tag_send"); }
static void get_tag(void *buf,size_t n,uint64_t tag)
{ ucp_request_param_t p={0}; wait_request(ucp_tag_recv_nbx(worker,buf,n,tag,UINT64_MAX,&p),"tag_recv"); }
static void flush(void)
{ ucp_request_param_t p={0}; wait_request(ucp_ep_flush_nbx(ep,&p),"ep_flush"); }
static unsigned char byte_pattern(size_t i,unsigned seed)
{ return (i*131u+(i>>8)+seed*17u)&255; }
static void fill(unsigned char *p,size_t n,unsigned seed)
{ size_t i; for(i=0;i<n;i++) p[i]=byte_pattern(i,seed); }
static void check(const unsigned char *p,size_t n,unsigned seed,const char *op)
{
    size_t i;
    for(i=0;i<n;i++) if(p[i]!=byte_pattern(i,seed)) {
        fprintf(stderr,"FAIL %s side=%u offset=%zu expected=%u actual=%u\n",op,side,i,byte_pattern(i,seed),p[i]); exit(1);
    }
    verified+=n;
}
static void io_full(int fd,void *buf,size_t n,int writing)
{
    char *p=buf; ssize_t r;
    while(n) {
        r=writing ? send(fd,p,n,MSG_NOSIGNAL) : recv(fd,p,n,0);
        if(r<0 && errno==EINTR) continue;
        if(r<=0) die(writing?"TCP bootstrap send":"TCP bootstrap receive");
        p+=r; n-=r;
    }
}
static void send_blob(int fd,const void *buf,size_t n)
{ uint32_t length; if(!n||n>64u*1024*1024) die("bootstrap blob size"); length=htonl(n); io_full(fd,&length,4,1); io_full(fd,(void *)buf,n,1); }
static void *recv_blob(int fd,size_t *n)
{ uint32_t length; void *p; io_full(fd,&length,4,0); *n=ntohl(length); if(!*n||*n>64u*1024*1024) die("bootstrap blob size"); p=malloc(*n); if(!p) die("malloc"); io_full(fd,p,*n,0); return p; }
static int bootstrap(const char *host,const char *port)
{
    struct addrinfo hints={0},*list,*a; int fd=-1,peer,one=1;
    struct timeval timeout={.tv_sec=30};
    hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags=side ? AI_PASSIVE : 0;
    if(getaddrinfo(host,port,&hints,&list)) die("getaddrinfo");
    for(a=list;a;a=a->ai_next) {
        fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol); if(fd<0) continue;
        setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
        if(side ? !bind(fd,a->ai_addr,a->ai_addrlen) : !connect(fd,a->ai_addr,a->ai_addrlen)) break;
        close(fd); fd=-1;
    }
    freeaddrinfo(list); if(fd<0) die(side?"bind":"connect (start server first)");
    if(side) {
        if(listen(fd,1)) die("listen");
        puts("READY bootstrap TCP; UCP data uses selected UCX transport");
        peer=accept(fd,NULL,NULL); close(fd); if(peer<0) die("accept"); fd=peer;
    }
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)); return fd;
}
static void get_peer_label(int bootstrap_fd, char *peer_label,
                            size_t label_capacity)
{
    struct sockaddr_in peer_address;
    socklen_t address_length;

    memset(&peer_address, 0, sizeof(peer_address));
    address_length = sizeof(peer_address);
    if (getpeername(bootstrap_fd, (struct sockaddr *)&peer_address,
                    &address_length) != 0) {
        die("getpeername");
    }
    if ((peer_address.sin_family != AF_INET) ||
        (inet_ntop(AF_INET, &peer_address.sin_addr, peer_label,
                   label_capacity) == NULL)) {
        die("peer address label");
    }
}

static void control_send(uint64_t tag)
{ uint64_t value=tag; put_tag(&value,sizeof(value),tag); }
static void control_recv(uint64_t tag)
{ uint64_t value=0; get_tag(&value,sizeof(value),tag); if(value!=tag) die("control bytes mismatch"); }
int main(int argc,char **argv)
{
    const char *host=NULL,*port; int tag_only=0,fd,arg;
    char peer_label[INET_ADDRSTRLEN];
    ucp_config_t *cfg; ucp_context_h context; ucp_params_t cp={0}; ucp_worker_params_t wp={0};
    ucp_ep_params_t epp={0}; ucp_address_t *address; void *peer_address; size_t length;
    unsigned char *tx,*rx,*remote; unsigned i,j,d,seed;
    size_t sizes[]={64,1024,4000,4008,4096,7259,7260,65536,1048576,MAX_TAG},n;
    ucp_mem_h mem=NULL; ucp_rkey_h key=NULL; ucp_request_param_t rp={0};
    uint64_t remote_addr=0,wire_addr; void *packed,*peer_packed; size_t packed_len;
    setvbuf(stdout,NULL,_IONBF,0); alarm(300);
    if(argc>=3 && !strcmp(argv[1],"--server")) { side=1; port=argv[2]; arg=3; }
    else if(argc>=4 && !strcmp(argv[1],"--client")) { side=0; host=argv[2]; port=argv[3]; arg=4; }
    else { fprintf(stderr,"usage: %s --server PORT | --client HOST PORT [--tag-only] [--iters N]\n",argv[0]); return 2; }
    while(arg<argc) {
        if(!strcmp(argv[arg],"--tag-only")) { tag_only=1; arg++; }
        else if(!strcmp(argv[arg],"--iters") && arg+1<argc) {
            char *end; unsigned long v=strtoul(argv[arg+1],&end,10);
            if(*end||!v||v>1000) die("--iters must be 1..1000");
            iterations=v; arg+=2;
        } else die("unknown argument");
    }
    status(ucp_config_read(NULL,NULL,&cfg),"config_read");
    cp.field_mask=UCP_PARAM_FIELD_FEATURES; cp.features=UCP_FEATURE_TAG|(tag_only?0:UCP_FEATURE_RMA);
    status(ucp_init(&cp,cfg,&context),"ucp_init"); ucp_config_release(cfg);
    wp.field_mask=UCP_WORKER_PARAM_FIELD_THREAD_MODE; wp.thread_mode=UCS_THREAD_MODE_SINGLE;
    status(ucp_worker_create(context,&wp,&worker),"worker_create");
    status(ucp_worker_get_address(worker,&address,&length),"worker_address");
    fd=bootstrap(host,port);
    if(side) { peer_address=recv_blob(fd,&n); send_blob(fd,address,length); }
    else { send_blob(fd,address,length); peer_address=recv_blob(fd,&n); }
    get_peer_label(fd, peer_label, sizeof(peer_label));
    epp.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS |
                     UCP_EP_PARAM_FIELD_ERR_HANDLER |
                     UCP_EP_PARAM_FIELD_PEER_HOST_ADDR;
    epp.address = peer_address;
    epp.err_handler.cb = on_endpoint_error;
    epp.err_handler.arg = &endpoint_error;
    /* HUCX copies this bounded IPv4 label during endpoint creation. */
    epp.peer_hostname = peer_label;
    epp.vpid = side ? 0 : 1;
    status(ucp_ep_create(worker, &epp, &ep), "ep_create");
    free(peer_address);
    ucp_worker_release_address(worker, address);
    tx=malloc(MAX_TAG); rx=malloc(MAX_TAG); remote=calloc(1,RMA_BYTES);
    if(!tx||!rx||!remote) die("malloc data");
    if(!tag_only) {
        ucp_mem_map_params_t mp={0};
        mp.field_mask=UCP_MEM_MAP_PARAM_FIELD_ADDRESS|UCP_MEM_MAP_PARAM_FIELD_LENGTH; mp.address=remote; mp.length=RMA_BYTES;
        status(ucp_mem_map(context,&mp,&mem),"mem_map"); status(ucp_rkey_pack(context,mem,&packed,&packed_len),"rkey_pack");
        wire_addr=htobe64((uintptr_t)remote);
        if(side) {
            io_full(fd,&remote_addr,8,0); peer_packed=recv_blob(fd,&n);
            io_full(fd,&wire_addr,8,1); send_blob(fd,packed,packed_len);
        } else {
            io_full(fd,&wire_addr,8,1); send_blob(fd,packed,packed_len);
            io_full(fd,&remote_addr,8,0); peer_packed=recv_blob(fd,&n);
        }
        remote_addr=be64toh(remote_addr); status(ucp_ep_rkey_unpack(ep,peer_packed,&key),"rkey_unpack");
        free(peer_packed); ucp_rkey_buffer_release(packed);
    }
    close(fd); /* All transfers and test coordination below use UCP. */
    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        n=sizes[i];
        for(j=0;j<iterations;j++) {
            seed=31+i*19+j*7;
            fill(tx,n,seed+side); memset(rx,0,n);
            if(side) { get_tag(rx,n,10); put_tag(tx,n,11); }
            else { put_tag(tx,n,10); get_tag(rx,n,11); }
            check(rx,n,seed+1-side,"TAG"); flush();
        }
        printf("PASS TAG side=%u size=%zu checked_bytes=%zu iterations=%u\n",side,n,n,iterations);
    }
    if(!tag_only) for(d=0;d<2;d++) for(j=0;j<iterations;j++) {
        seed=301+d*19+j*7;
        if(side==d) {
            control_recv(100); fill(tx,RMA_BYTES,seed);
            wait_request(ucp_put_nbx(ep,tx,RMA_BYTES,remote_addr,key,&rp),"PUT"); flush();
            control_send(101); control_recv(102);
            control_recv(103); memset(rx,0,RMA_BYTES);
            wait_request(ucp_get_nbx(ep,rx,RMA_BYTES,remote_addr,key,&rp),"GET");
            check(rx,RMA_BYTES,seed+1,"GET"); control_send(104);
            printf("PASS GET side=%u checked_bytes=%u iter=%u\n",side,RMA_BYTES,j);
        } else {
            memset(remote,0,RMA_BYTES); control_send(100); control_recv(101);
            check(remote,RMA_BYTES,seed,"PUT"); control_send(102);
            fill(remote,RMA_BYTES,seed+1); control_send(103); control_recv(104);
            printf("PASS PUT side=%u checked_bytes=%u iter=%u\n",side,RMA_BYTES,j);
        }
    }
    /* Last handshake prevents one peer tearing down before the other's flush. */
    flush();
    if(side) { control_recv(200); control_send(201); }
    else { control_send(200); control_recv(201); }
    flush();
    if(key) ucp_rkey_destroy(key);
    wait_request(ucp_ep_close_nbx(ep,&rp),"ep_close");
    if(mem) status(ucp_mem_unmap(context,mem),"mem_unmap");
    ucp_worker_destroy(worker); ucp_cleanup(context);
    free(tx); free(rx); free(remote);
    printf("PASS SUMMARY side=%u verified_bytes=%llu tag_only=%d\n",side,(unsigned long long)verified,tag_only);
    return 0;
}
