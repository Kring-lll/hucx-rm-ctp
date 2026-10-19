/* Real UCP/UCT protocol selection over an in-process fake URMA provider/MD. */
#define RM_CTP_PROVIDER_ONLY
#include "test_transport.c"
#include <ucp/api/ucp.h>
#include <time.h>

static unsigned next_context;
static ucs_status_t fake_resources(uct_component_t *c, uct_md_resource_desc_t **out, unsigned *count)
{ *out=calloc(1,sizeof(**out)); assert(*out); strcpy((*out)->md_name,"fake_ctp"); *count=1; return UCS_OK; }
static void fake_md_close(uct_md_h tl)
{ uct_ub_md_t *md=(void *)tl; free(md->dev.urma_ctx); free(md); }
static ucs_status_t fake_md_query(uct_md_h tl, uct_md_attr_v2_t *a)
{
    a->max_alloc=ULONG_MAX; a->max_reg=ULONG_MAX;
    a->flags=UCT_MD_FLAG_REG|UCT_MD_FLAG_NEED_MEMH|UCT_MD_FLAG_NEED_RKEY;
    a->reg_mem_types=a->access_mem_types=UCS_BIT(UCS_MEMORY_TYPE_HOST);
    a->cache_mem_types=UCS_MASK(UCS_MEMORY_TYPE_LAST);
    a->rkey_packed_size=UCT_UB_MD_PACKED_RKEY_SIZE; a->reg_cost=ucs_linear_func_make(0,0);
    memset(&a->local_cpus,255,sizeof(a->local_cpus)); return UCS_OK;
}
static ucs_status_t fake_mem_reg(uct_md_h tl, void *addr, size_t n, const uct_md_mem_reg_params_t *p, uct_mem_h *out)
{
    uct_ub_md_t *md=(void *)tl; uct_ub_mem_t *m=calloc(1,sizeof(*m)); urma_seg_cfg_t cfg={0};
    assert(m); cfg.va=(uintptr_t)addr; cfg.len=n;
    m->tseg=urma_register_seg(md->dev.urma_ctx,&cfg); assert(m->tseg); *out=m; return UCS_OK;
}
static ucs_status_t fake_mem_dereg(uct_md_h tl, const uct_md_mem_dereg_params_t *p)
{ uct_ub_mem_t *m=p->memh; urma_unregister_seg(m->tseg); free(m); return UCS_OK; }
static ucs_status_t fake_pack(uct_md_h tl, uct_mem_h mh, const uct_md_mkey_pack_params_t *p, void *buf)
{
    uct_ub_mem_t *m=mh; uct_ub_seg_info_t *k=buf;
    k->eid=m->tseg->seg.ubva.eid; k->uasid=m->tseg->seg.ubva.uasid;
    k->seg_va=m->tseg->seg.ubva.va; k->seg_len=m->tseg->seg.len;
    k->seg_flag=m->tseg->seg.attr.value; k->seg_token_id=m->tseg->seg.token_id; return UCS_OK;
}
static uct_md_ops_t fake_md_ops={.close=fake_md_close,.query=fake_md_query,.mem_reg=fake_mem_reg,.mem_dereg=fake_mem_dereg,.mkey_pack=fake_pack};
static ucs_status_t fake_md_open(uct_component_t *c,const char *name,const uct_md_config_t *cfg,uct_md_h *out)
{
    uct_ub_md_t *md=calloc(1,sizeof(*md)); assert(md);
    md->dev.urma_ctx=calloc(1,sizeof(urma_context_t)); assert(md->dev.urma_ctx);
    ((unsigned char *)&md->dev.urma_ctx->eid)[15]=++next_context;
    md->dev.urma_ctx->uasid=next_context; md->dev.dev_attr.dev_cap.rm_tp_cap.bs.ctp=1;
    md->dev.dev_attr.dev_cap.max_read_size=md->dev.dev_attr.dev_cap.max_write_size=1048576;
    md->super.component=c; md->super.ops=&fake_md_ops; strcpy(md->dev.dev_name,"fake_ctp");
    *out=&md->super; return UCS_OK;
}
ucs_status_t uct_ub_device_query_ports(uct_ub_device_t *dev,unsigned flags,uct_tl_device_resource_t **out,unsigned *count)
{
    *out=calloc(1,sizeof(**out)); assert(*out); strcpy((*out)->name,"fake_ctp:1");
    (*out)->type=UCT_DEVICE_TYPE_NET; (*out)->sys_device=UCS_SYS_DEVICE_ID_UNKNOWN; *count=1; return UCS_OK;
}
static ucp_worker_h workers[2];
static void wait_req(void *r)
{
    uint64_t start=ucs_get_time(); ucs_status_t s;
    assert(!UCS_PTR_IS_ERR(r)); if (!r) return;
    while ((s=ucp_request_check_status(r))==UCS_INPROGRESS) {
        ucp_worker_progress(workers[0]); ucp_worker_progress(workers[1]);
        assert(ucs_get_time()-start<ucs_time_from_sec(10));
    }
    assert(s==UCS_OK); ucp_request_free(r);
}
static unsigned char pattern(size_t i,unsigned salt) { return (i*131u+(i>>8)+salt*17u)&255; }
static void roundtrip(ucp_ep_h *eps,size_t n,unsigned iter)
{
    unsigned char *src=malloc(n),*dst=malloc(n); unsigned i,side; void *r,*s;
    ucp_request_param_t param={0};
    assert(src&&dst);
    for(side=0;side<2;side++) {
        for(i=0;i<n;i++) src[i]=pattern(i,iter+side);
        memset(dst,0,n);
        r=ucp_tag_recv_nbx(workers[1-side],dst,n,100+side,UINT64_MAX,&param);
        s=ucp_tag_send_nbx(eps[side],src,n,100+side,&param);
        wait_req(s); wait_req(r); assert(!memcmp(src,dst,n));
        wait_req(ucp_ep_flush_nbx(eps[side],&param));
    }
    printf("PASS UCP TAG size=%zu checked_bytes=%zu directions=2\n",n,n); free(src); free(dst);
}
static void run(const char *threshold,int native)
{
    ucp_config_t *cfg; ucp_context_h ctx[2]; ucp_ep_h ep[2]; ucp_address_t *addr[2]; size_t addr_len[2];
    ucp_params_t p={0}; ucp_worker_params_t wp={0}; ucp_ep_params_t epp={0};
    size_t sizes[]={64,1024,4000,4008,4096,7259,7260,65536,1048576,8388608}; unsigned i, before_reads, before_writes;
    ucp_request_param_t rp={0};
    assert(setenv("UCX_RM_URMA_NATIVE_RMA",native?"y":"n",1)==0);
    assert(ucp_config_read(NULL,NULL,&cfg)==UCS_OK);
    assert(ucp_config_modify(cfg,"TLS","rm_urma")==UCS_OK);
    assert(ucp_config_modify(cfg,"NET_DEVICES","all")==UCS_OK);
    assert(ucp_config_modify(cfg,"PROTO_ENABLE","n")==UCS_OK);
    assert(ucp_config_modify(cfg,"RNDV_THRESH",threshold)==UCS_OK);
    assert(ucp_config_modify(cfg,"RNDV_SCHEME","get_zcopy")==UCS_OK);
    p.field_mask=UCP_PARAM_FIELD_FEATURES; p.features=UCP_FEATURE_TAG|UCP_FEATURE_RMA;
    wp.field_mask=UCP_WORKER_PARAM_FIELD_THREAD_MODE; wp.thread_mode=UCS_THREAD_MODE_SINGLE;
    for(i=0;i<2;i++) {
        assert(ucp_init(&p,cfg,&ctx[i])==UCS_OK); assert(ucp_worker_create(ctx[i],&wp,&workers[i])==UCS_OK);
        assert(ucp_worker_get_address(workers[i],&addr[i],&addr_len[i])==UCS_OK);
    }
    for(i=0;i<2;i++) {
        epp.field_mask=UCP_EP_PARAM_FIELD_REMOTE_ADDRESS; epp.address=addr[1-i];
        assert(ucp_ep_create(workers[i],&epp,&ep[i])==UCS_OK);
        ucp_worker_release_address(workers[1-i],addr[1-i]);
    }
    before_reads=read_posts;
    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) roundtrip(ep,sizes[i],i);
    if(native && strcmp(threshold,"inf")) assert(read_posts>before_reads);
    else assert(read_posts==before_reads);
    printf("PASS UCP TAG path threshold=%s native=%d native_read_posts=%u\n",threshold,native,read_posts-before_reads);
    drop_data=1; drop_ack=1; roundtrip(ep,65536,99);
    before_reads=read_posts; before_writes=write_posts;
    {
        size_t n=1048576,packed_len; unsigned char *src=malloc(n),*remote=calloc(1,n),*dst=calloc(1,n);
        ucp_mem_map_params_t mp={0}; ucp_mem_h mh; void *packed; ucp_rkey_h key;
        assert(src&&remote&&dst); for(i=0;i<n;i++) src[i]=pattern(i,201);
        mp.field_mask=UCP_MEM_MAP_PARAM_FIELD_ADDRESS|UCP_MEM_MAP_PARAM_FIELD_LENGTH; mp.address=remote; mp.length=n;
        assert(ucp_mem_map(ctx[1],&mp,&mh)==UCS_OK); assert(ucp_rkey_pack(ctx[1],mh,&packed,&packed_len)==UCS_OK);
        assert(ucp_ep_rkey_unpack(ep[0],packed,&key)==UCS_OK); ucp_rkey_buffer_release(packed);
        wait_req(ucp_put_nbx(ep[0],src,n,(uintptr_t)remote,key,&rp)); wait_req(ucp_ep_flush_nbx(ep[0],&rp));
        assert(!memcmp(src,remote,n)); wait_req(ucp_get_nbx(ep[0],dst,n,(uintptr_t)remote,key,&rp));
        assert(!memcmp(src,dst,n)); printf("PASS UCP PUT/GET size=%zu checked_bytes=%zu including_last_byte\n",n,n);
        ucp_rkey_destroy(key); assert(ucp_mem_unmap(ctx[1],mh)==UCS_OK); free(src); free(remote); free(dst);
    }
    if(!native) assert(read_posts==before_reads && write_posts==before_writes);
    for(i=0;i<2;i++) wait_req(ucp_ep_flush_nbx(ep[i],&rp));
    { void *r[2]; r[0]=ucp_ep_close_nbx(ep[0],&rp); r[1]=ucp_ep_close_nbx(ep[1],&rp); wait_req(r[0]); wait_req(r[1]); }
    for(i=0;i<2;i++) { ucp_worker_destroy(workers[i]); ucp_cleanup(ctx[i]); }
    ucp_config_release(cfg); assert(objects==0);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    alarm(45);
    uct_ub_component.query_md_resources=fake_resources;
    uct_ub_component.md_open=fake_md_open;
    run("inf",1); run("8k",1); run("8k",0);
    puts("PASS UCP integration: eager fragmentation through 8MiB, GET rendezvous, explicit PUT/GET, SEND-only fallback, dropped DATA/ACK, cleanup (fake provider, not hardware)");
    return 0;
}
