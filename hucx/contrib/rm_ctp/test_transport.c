/* In-process fake URMA provider. This tests transport code, not hardware. */
#include <uct/urma/rm_urma/rm_urma_ep.h>
#include <uct/urma/rm_urma/rm_urma_wireup.h>
#include <ucs/async/async.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uct_tl_t uct_rm_urma_tl;
typedef struct { urma_jfc_t public; urma_cr_t q[4096]; unsigned head, tail; int hold; } fake_cq_t;
typedef struct { urma_jfr_t public; urma_sge_t sge[1024]; uint64_t ctx[1024]; unsigned head, tail; } fake_rq_t;
static urma_jetty_t *jettys[256];
static unsigned next_jetty = 1, objects, allocations, fail_allocation;
static unsigned drop_data, drop_ack, duplicate_data, hardware_error;
static unsigned posts, data_posts, callbacks, errors, deliveries;
static unsigned read_posts, write_posts, fail_delete;
static ucs_status_t completed_status;
static int alloc_fails(void) { return ++allocations == fail_allocation; }
static void *allocate(size_t n) { void *p; if (alloc_fails()) return NULL; p = calloc(1,n); assert(p); objects++; return p; }
static void release(void *p) { assert(p && objects); objects--; free(p); }
static void push(urma_jfc_t *cq, const urma_cr_t *cr)
{ fake_cq_t *f = (void *)cq; assert(f->tail - f->head < 4096); f->q[f->tail++ % 4096] = *cr; }
urma_status_t urma_init(urma_init_attr_t *attr) { return URMA_SUCCESS; }
urma_status_t urma_uninit(void) { return URMA_SUCCESS; }
urma_jfc_t *urma_create_jfc(urma_context_t *ctx, urma_jfc_cfg_t *cfg)
{ fake_cq_t *q = allocate(sizeof(*q)); if (!q) return NULL; q->public.urma_ctx = ctx; q->public.jfc_cfg = *cfg; return &q->public; }
urma_status_t urma_delete_jfc(urma_jfc_t *q) { release(q); return URMA_SUCCESS; }
urma_jfr_t *urma_create_jfr(urma_context_t *ctx, urma_jfr_cfg_t *cfg)
{ fake_rq_t *r = allocate(sizeof(*r)); if (!r) return NULL; r->public.urma_ctx = ctx; r->public.jfr_cfg = *cfg; return &r->public; }
urma_status_t urma_delete_jfr(urma_jfr_t *r) { release(r); return URMA_SUCCESS; }
urma_jetty_t *urma_create_jetty(urma_context_t *ctx, urma_jetty_cfg_t *cfg)
{
    urma_jetty_t *j = allocate(sizeof(*j)); if (!j) return NULL;
    assert(cfg->jfs_cfg.trans_mode == URMA_TM_RM && cfg->flag.bs.share_jfr);
    assert(next_jetty < 256); j->urma_ctx = ctx; j->jetty_cfg = *cfg;
    j->jetty_id.eid = ctx->eid; j->jetty_id.uasid = ctx->uasid;
    j->jetty_id.id = next_jetty; jettys[next_jetty++] = j; return j;
}
urma_status_t urma_delete_jetty(urma_jetty_t *j)
{
    fake_cq_t *cq = (void *)j->jetty_cfg.jfs_cfg.jfc;
    if (fail_delete) { fail_delete--; return URMA_FAIL; }
    cq->head = cq->tail; /* Deletion synchronously stops all simulated DMA. */
    jettys[j->jetty_id.id] = NULL; release(j); return URMA_SUCCESS;
}
urma_target_jetty_t *urma_import_jetty(urma_context_t *ctx, urma_rjetty_t *r, urma_token_t *token)
{
    urma_target_jetty_t *t;
    assert(r->trans_mode == URMA_TM_RM && r->tp_type == URMA_CTP && r->type == URMA_JETTY);
    assert(r->jetty_id.id < 256 && jettys[r->jetty_id.id]);
    assert(!memcmp(&jettys[r->jetty_id.id]->jetty_id, &r->jetty_id, sizeof(r->jetty_id)));
    t = allocate(sizeof(*t)); if (!t) return NULL;
    t->urma_ctx = ctx; t->id = r->jetty_id; t->trans_mode = r->trans_mode; t->tp_type = r->tp_type;
    return t;
}
urma_status_t urma_unimport_jetty(urma_target_jetty_t *t) { release(t); return URMA_SUCCESS; }
urma_target_seg_t *urma_register_seg(urma_context_t *ctx, urma_seg_cfg_t *cfg)
{
    urma_target_seg_t *s = allocate(sizeof(*s)); if (!s) return NULL;
    s->urma_ctx = ctx; s->seg.ubva.eid = ctx->eid; s->seg.ubva.uasid = ctx->uasid;
    s->seg.ubva.va = cfg->va; s->seg.len = cfg->len; return s;
}
urma_status_t urma_unregister_seg(urma_target_seg_t *s) { release(s); return URMA_SUCCESS; }
urma_target_seg_t *urma_import_seg(urma_context_t *ctx, urma_seg_t *seg, urma_token_t *token, uint64_t addr, urma_import_seg_flag_t flags)
{ urma_target_seg_t *s = allocate(sizeof(*s)); if (!s) return NULL; s->urma_ctx = ctx; s->seg = *seg; return s; }
urma_status_t urma_unimport_seg(urma_target_seg_t *s) { release(s); return URMA_SUCCESS; }
urma_status_t urma_post_jetty_recv_wr(urma_jetty_t *j, urma_jfr_wr_t *wr, urma_jfr_wr_t **bad)
{
    fake_rq_t *r = (void *)j->jetty_cfg.shared.jfr;
    assert(wr->src.num_sge == 1 && r->tail - r->head < 1024);
    r->sge[r->tail % 1024] = *wr->src.sge; r->ctx[r->tail++ % 1024] = wr->user_ctx;
    return URMA_SUCCESS;
}
static void deliver(urma_jetty_t *from, urma_jetty_t *to, urma_sge_t *s)
{
    fake_rq_t *rq = (void *)to->jetty_cfg.shared.jfr;
    unsigned slot;
    urma_cr_t cr = {0};
    if (rq->head == rq->tail) return; /* RNR/loss model. */
    slot = rq->head++ % 1024; assert(s->len <= rq->sge[slot].len);
    memcpy((void *)(uintptr_t)rq->sge[slot].addr, (void *)(uintptr_t)s->addr, s->len);
    cr.status = URMA_CR_SUCCESS; cr.user_ctx = rq->ctx[slot];
    cr.flag.bs.s_r = cr.flag.bs.jetty = 1;
    cr.remote_id = from->jetty_id; cr.completion_len = s->len;
    push(rq->public.jfr_cfg.jfc, &cr);
}
urma_status_t urma_post_jetty_send_wr(urma_jetty_t *j, urma_jfs_wr_t *wr, urma_jfs_wr_t **bad)
{
    urma_cr_t cr = {0};
    posts++; cr.status = URMA_CR_SUCCESS; cr.user_ctx = wr->user_ctx;
    assert(wr->flag.bs.complete_enable);
    if (hardware_error) { hardware_error--; cr.status = URMA_CR_LOC_ACCESS_ERR; }
    else if (wr->opcode == URMA_OPC_SEND) {
        rm_header_t *h = (void *)(uintptr_t)wr->send.src.sge->addr;
        urma_jetty_t *to = jettys[wr->tjetty->id.id];
        assert(to && wr->send.src.num_sge == 1);
        if (h->type == RM_DATA) data_posts++;
        if (h->type == RM_DATA && drop_data) drop_data--;
        else if (h->type == RM_ACK && drop_ack) drop_ack--;
        else {
            deliver(j, to, wr->send.src.sge);
            if (h->type == RM_DATA && duplicate_data) { duplicate_data--; deliver(j, to, wr->send.src.sge); }
        }
    } else {
        assert(wr->opcode == URMA_OPC_WRITE || wr->opcode == URMA_OPC_READ);
        if (wr->opcode == URMA_OPC_READ) read_posts++; else write_posts++;
        assert(wr->rw.src.num_sge == 1 && wr->rw.dst.num_sge == 1);
        assert(wr->rw.src.sge->len == wr->rw.dst.sge->len);
        memcpy((void *)(uintptr_t)wr->rw.dst.sge->addr, (void *)(uintptr_t)wr->rw.src.sge->addr, wr->rw.src.sge->len);
    }
    push(j->jetty_cfg.jfs_cfg.jfc, &cr); return URMA_SUCCESS;
}
int urma_poll_jfc(urma_jfc_t *cq, int count, urma_cr_t *cr)
{
    fake_cq_t *f = (void *)cq; int n = 0;
    if (f->hold) return 0;
    while (n < count && f->head != f->tail) cr[n++] = f->q[f->head++ % 4096];
    return n;
}
#ifndef RM_CTP_PROVIDER_ONLY
static ucs_status_t on_error(void *arg, uct_ep_h ep, ucs_status_t status)
{ assert(status < 0); errors++; return UCS_OK; }
static ucs_status_t on_am(void *arg, void *data, size_t n, unsigned flags)
{ assert(n == 12 && flags == 0); assert(!memcmp((char *)data+8, "test", 4)); deliveries++; return UCS_OK; }
static void on_complete(uct_completion_t *c) { callbacks++; completed_status = c->status; }
static void on_unpack(void *arg, const void *data, size_t n) { memcpy(arg, data, n); }
static uct_rm_urma_iface_t *open_iface(uct_ub_md_t *md, uct_worker_h worker, unsigned rx)
{
    uct_iface_h tl;
    uct_rm_urma_config_t cfg = {0};
    uct_iface_params_t params = {0};
    cfg.super.failure = UCS_LOG_LEVEL_DEBUG; cfg.super.max_num_eps = 1024;
    cfg.rx_depth = rx; cfg.tx_depth = 128; cfg.priority = 1;
    cfg.timeout = .001; cfg.completion_timeout = 5; cfg.retries = 3; cfg.max_peers = 100; cfg.native_rma = 1;
    cfg.max_buffered = 16 * 1024 * 1024;
    params.field_mask = UCT_IFACE_PARAM_FIELD_ERR_HANDLER; params.err_handler = on_error;
    if (uct_rm_urma_tl.iface_open(&md->super, worker, &params, &cfg.super, &tl) != UCS_OK) return NULL;
    assert(uct_iface_set_am_handler(tl, 7, on_am, NULL, 0) == UCS_OK);
    return ucs_derived_of(tl, uct_rm_urma_iface_t);
}
static uct_rm_urma_ep_t *open_ep(uct_rm_urma_iface_t *a, uct_rm_urma_iface_t *b)
{
    uct_ep_params_t p = {0}; uct_ep_h tl;
    p.field_mask = UCT_EP_PARAM_FIELD_IFACE | UCT_EP_PARAM_FIELD_DEV_ADDR | UCT_EP_PARAM_FIELD_IFACE_ADDR;
    p.iface = &a->super.super; p.dev_addr = (void *)b->address.eid; p.iface_addr = (void *)&b->address;
    assert(uct_ep_create(&p, &tl) == UCS_OK); return ucs_derived_of(tl, uct_rm_urma_ep_t);
}
static void progress(uct_rm_urma_iface_t *a, uct_rm_urma_iface_t *b)
{ unsigned i; for (i=0;i<50;i++) { uct_iface_progress(&a->super.super); uct_iface_progress(&b->super.super); } }
static void retry_now(uct_rm_urma_iface_t *a)
{ rm_op_t *op; for(op=a->ops;op;op=op->next) if(op->kind==RM_DATA && !op->inflight) op->sent = ucs_get_time() - (a->retry_ticks << (op->attempts < 5 ? op->attempts : 5)); }
static void send_am(uct_rm_urma_ep_t *ep) { assert(rm_ep_am_short(&ep->super.super,7,0,"test",4)==UCS_OK); }
static void close_iface(uct_rm_urma_iface_t *iface) { uct_iface_close(&iface->super.super); }
static unsigned pending_calls;
static ucs_status_t send_pending(uct_pending_req_t *req)
{
    uct_rm_urma_ep_t *ep;
    memcpy(&ep,req->priv+sizeof(ucs_queue_elem_t),sizeof(ep));
    pending_calls++;
    return rm_ep_am_short(&ep->super.super,7,0,"test",4);
}
static void rkey_hash_test(void)
{
    uct_ub_rkey_hash_key_t a = {0}, b;
    a.va = 0x100000040ull; b = a; b.va += 0x100000000ull;
    assert(!uct_ub_rkey_hash_equal(a,b)); assert(uct_ub_rkey_hash_func(a)!=uct_ub_rkey_hash_func(b));
    b = a; ((unsigned char *)&b.deid)[0] = 1;
    assert(!uct_ub_rkey_hash_equal(a,b)); assert(uct_ub_rkey_hash_func(a)!=uct_ub_rkey_hash_func(b));
}
int main(void)
{
    ucs_async_context_t async;
    uct_worker_h worker;
    urma_context_t ctx[2] = {{0}};
    uct_ub_md_t md[2] = {{0}};
    uct_rm_urma_iface_t *a,*b;
    uct_rm_urma_ep_t *ep,*reverse;
    unsigned i, before, baseline;
    char local[128],remote[128],readback[128];
    uct_ub_rkey_info_t key = {0};
    uct_completion_t comp;
    urma_seg_cfg_t scfg = {0};
    uct_ub_mem_t memh;
    uct_iov_t iov = {0};
    rkey_hash_test();
    assert(ucs_async_context_init(&async,UCS_ASYNC_MODE_POLL)==UCS_OK);
    assert(uct_worker_create(&async,UCS_THREAD_MODE_SINGLE,&worker)==UCS_OK);
    for(i=0;i<2;i++) {
        ((unsigned char *)&ctx[i].eid)[15]=i+1;
        ctx[i].uasid=i+1; md[i].dev.urma_ctx=&ctx[i]; md[i].dev.dev_attr.dev_cap.rm_tp_cap.bs.ctp=1;
        md[i].dev.dev_attr.dev_cap.max_read_size=md[i].dev.dev_attr.dev_cap.max_write_size=1048576;
    }
    /* Fail each owned allocation in the constructor, including each RX segment. */
    for(i=1;i<=12;i++) {
        allocations=0; fail_allocation=i;
        assert(open_iface(&md[0],worker,8)==NULL); assert(objects==0);
    }
    fail_allocation=0;
    a=open_iface(&md[0],worker,128); b=open_iface(&md[1],worker,128); assert(a&&b);
    ep=open_ep(a,b); reverse=open_ep(b,a);
    drop_data=1; send_am(ep); progress(a,b); assert(deliveries==0 && ep->outstanding==1);
    retry_now(a); progress(a,b); assert(deliveries==1 && !ep->outstanding);
    drop_ack=1; send_am(ep); progress(a,b); assert(deliveries==2 && ep->outstanding==1);
    retry_now(a); progress(a,b); assert(deliveries==2 && !ep->outstanding);
    duplicate_data=1; send_am(ep); send_am(reverse); progress(a,b); assert(deliveries==4);
    /* ACK before local SEND completion must not free a DMA-owned buffer. */
    ((fake_cq_t *)a->tx_cq)->hold=1; send_am(ep); progress(a,b);
    assert(ep->outstanding==1 && a->ops->acked && a->ops->inflight);
    ((fake_cq_t *)a->tx_cq)->hold=0; progress(a,b); assert(!ep->outstanding);
    before=deliveries;
    for(i=0;i<RM_WINDOW;i++) send_am(ep);
    assert(rm_ep_am_short(&ep->super.super,7,0,"test",4)==UCS_ERR_NO_RESOURCE);
    assert(rm_ep_fence(&ep->super.super,0)==UCS_OK);
    {
        uct_pending_req_t pending={0}; pending.func=send_pending;
        memcpy(pending.priv+sizeof(ucs_queue_elem_t),&ep,sizeof(ep));
        assert(rm_ep_pending_add(&ep->super.super,&pending,0)==UCS_OK);
        progress(a,b); assert(deliveries==before+RM_WINDOW+1 && !ep->outstanding && pending_calls>=1);
    }
    /* Snapshot flush ignores a later, still-unacknowledged operation. */
    before=callbacks; send_am(ep); comp.func=on_complete; comp.count=1; comp.status=UCS_OK;
    assert(rm_ep_flush(&ep->super.super,0,&comp)==UCS_INPROGRESS);
    drop_data=1; send_am(ep); progress(a,b);
    assert(callbacks==before+1 && ep->outstanding==1 && completed_status==UCS_OK);
    retry_now(a); progress(a,b); assert(!ep->outstanding);
    memset(local,0x5a,sizeof(local)); memset(remote,0,sizeof(remote));
    scfg.va=(uintptr_t)local; scfg.len=sizeof(local); memh.tseg=urma_register_seg(&ctx[0],&scfg); assert(memh.tseg);
    key.remote_seg.ubva.va=(uintptr_t)remote; key.remote_seg.len=sizeof(remote);
    key.remote_seg.ubva.eid=ctx[1].eid; key.remote_seg.ubva.uasid=ctx[1].uasid;
    iov.buffer=local; iov.length=sizeof(local); iov.count=1; iov.memh=&memh;
    baseline=callbacks; comp.func=on_complete; comp.count=1; comp.status=UCS_OK;
    assert(rm_ep_put_zcopy(&ep->super.super,&iov,1,(uintptr_t)remote,(uct_rkey_t)&key,&comp)==UCS_INPROGRESS);
    progress(a,b); assert(callbacks==baseline+1 && completed_status==UCS_OK && !memcmp(local,remote,128));
    memset(local,0,128); comp.count=1; comp.status=UCS_OK;
    assert(rm_ep_get_zcopy(&ep->super.super,&iov,1,(uintptr_t)remote,(uct_rkey_t)&key,&comp)==UCS_INPROGRESS);
    progress(a,b); assert(callbacks==baseline+2 && !memcmp(local,remote,128));
    comp.count=1; comp.status=UCS_OK;
    assert(rm_ep_get_bcopy(&ep->super.super,on_unpack,readback,128,(uintptr_t)remote,(uct_rkey_t)&key,&comp)==UCS_INPROGRESS);
    progress(a,b); assert(callbacks==baseline+3 && !memcmp(readback,remote,128));
    assert(rm_ep_put_zcopy(&ep->super.super,&iov,1,(uintptr_t)remote,(uct_rkey_t)&key,NULL)==UCS_INPROGRESS);
    progress(a,b); assert(!ep->outstanding);
    assert(rm_ep_put_short(&ep->super.super,local,128,(uintptr_t)remote+1,(uct_rkey_t)&key)==UCS_ERR_INVALID_PARAM);
    /* Errors complete once and native WRITE is never replayed. */
    hardware_error=1; before=posts; comp.count=1; comp.status=UCS_OK;
    assert(rm_ep_put_zcopy(&ep->super.super,&iov,1,(uintptr_t)remote,(uct_rkey_t)&key,&comp)==UCS_INPROGRESS);
    progress(a,b); assert(callbacks==baseline+4 && completed_status==UCS_ERR_IO_ERROR && errors==1);
    assert(posts==before+1 && !a->ops && a->failed==UCS_ERR_IO_ERROR);
    urma_unregister_seg(memh.tseg); close_iface(a); close_iface(b); assert(objects==0);
    a=open_iface(&md[0],worker,128); b=open_iface(&md[1],worker,128); ep=open_ep(a,b);
    baseline=errors; before=deliveries; drop_data=100; send_am(ep);
    for(i=0;i<5;i++) { progress(a,b); retry_now(a); }
    progress(a,b); assert(errors==baseline+1 && ep->failed==UCS_ERR_ENDPOINT_TIMEOUT);
    assert(deliveries==before && !ep->outstanding); drop_data=0;
    close_iface(a); close_iface(b); assert(objects==0);
    a=open_iface(&md[0],worker,128); b=open_iface(&md[1],worker,128); ep=open_ep(a,b);
    /* A fake CQE's arbitrary user_ctx must never be used as a pointer. */
    { urma_cr_t fake={0}; fake.status=URMA_CR_WR_SUSPEND_DONE; fake.user_ctx=UINT64_MAX; push(a->tx_cq,&fake); }
    baseline=errors; progress(a,b); assert(errors==baseline+1 && !a->jetty && !a->ops);
    close_iface(a); close_iface(b); assert(objects==0);
    a=open_iface(&md[0],worker,128); b=open_iface(&md[1],worker,128); ep=open_ep(a,b);
    /* Missing local completion: quiesce queues before invoking zcopy completion. */
    scfg.va=(uintptr_t)local; memh.tseg=urma_register_seg(&ctx[0],&scfg); assert(memh.tseg);
    iov.memh=&memh; comp.count=1; comp.status=UCS_OK; before=callbacks;
    ((fake_cq_t *)a->tx_cq)->hold=1;
    assert(rm_ep_put_zcopy(&ep->super.super,&iov,1,(uintptr_t)remote,(uct_rkey_t)&key,&comp)==UCS_INPROGRESS);
    a->ops->sent=ucs_get_time()-a->completion_ticks; progress(a,b);
    assert(callbacks==before+1 && completed_status==UCS_ERR_ENDPOINT_TIMEOUT && !a->jetty && !a->ops);
    urma_unregister_seg(memh.tseg); close_iface(a); close_iface(b); assert(objects==0);
    /* Failed queue deletion must retain DMA memory and defer completion. */
    a=open_iface(&md[0],worker,128); b=open_iface(&md[1],worker,128); ep=open_ep(a,b);
    memh.tseg=urma_register_seg(&ctx[0],&scfg); iov.memh=&memh;
    comp.count=1; comp.status=UCS_OK; before=callbacks; fail_delete=1000;
    ((fake_cq_t *)a->tx_cq)->hold=1;
    assert(rm_ep_put_zcopy(&ep->super.super,&iov,1,(uintptr_t)remote,(uct_rkey_t)&key,&comp)==UCS_INPROGRESS);
    a->ops->sent=ucs_get_time()-a->completion_ticks; progress(a,b);
    assert(callbacks==before && a->ops && a->jetty);
    fail_delete=0; progress(a,b); assert(callbacks==before+1 && !a->ops && !a->jetty);
    urma_unregister_seg(memh.tseg); close_iface(a); close_iface(b); assert(objects==0);
    a=open_iface(&md[0],worker,128); b=open_iface(&md[1],worker,128); ep=open_ep(a,b);
    { urma_cr_t fake={0}; fake.user_ctx=(UINT64_C(1)<<63)|(UINT64_C(1)<<32); push(a->rx_cq,&fake); }
    progress(a,b); assert(a->failed==UCS_ERR_IO_ERROR && !a->jetty);
    close_iface(a); close_iface(b); assert(objects==0);
    /* Heap pressure still permits a gap-filling packet to drain the stream. */
    a=open_iface(&md[0],worker,128); b=open_iface(&md[1],worker,128); ep=open_ep(a,b);
    b->config.max_buffered=1; before=deliveries; drop_data=1; send_am(ep); send_am(ep);
    progress(a,b); assert(b->buffered==0 && deliveries==before);
    for(i=0;i<3;i++) { retry_now(a); progress(a,b); }
    assert(deliveries==before+2 && !ep->outstanding && b->buffered==0);
    close_iface(a); close_iface(b); assert(objects==0);
    a=open_iface(&md[0],worker,128); b=open_iface(&md[1],worker,128); ep=open_ep(a,b);
    send_am(ep); uct_ep_destroy(&ep->super.super); progress(a,b); assert(!a->eps);
    close_iface(a); close_iface(b); assert(objects==0);
    uct_worker_destroy(worker); ucs_async_context_cleanup(&async);
    printf("PASS transport: DATA/ACK loss, duplicate suppression, 64-slot backpressure, fence, pending, snapshot flush, ACK-before-CQE, READ/WRITE, callback once, retry exhaustion, fake/invalid CQE, missing CQE, quarantine, RX budget, deferred destroy, constructor cleanup, 64-bit rkey hash; data_posts=%u\n",data_posts);
    return 0;
}

#endif /* RM_CTP_PROVIDER_ONLY */
