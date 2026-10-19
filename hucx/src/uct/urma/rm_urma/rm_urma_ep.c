#include "rm_urma_ep.h"
#include "rm_urma_wireup.h"
#include <arpa/inet.h>
#include <endian.h>
#include <stdlib.h>
#include <string.h>

static ucs_status_t rm_ready(uct_rm_urma_ep_t *ep)
{
    if (ep->failed != UCS_OK) return ep->failed;
    if (ep->closing) return UCS_ERR_CANCELED;
    if (ep->iface->failed != UCS_OK) return ep->iface->failed;
    if (ep->am_outstanding >= RM_WINDOW || ep->rma_outstanding ||
        (ep->fenced && ep->outstanding) ||
        ep->iface->op_count >= ep->iface->config.tx_depth - 8 ||
        ep->iface->inflight >= ep->iface->config.tx_depth - 8) return UCS_ERR_NO_RESOURCE;
    ep->fenced = 0;
    return UCS_OK;
}
/* AM has a bounded window; native RMA is serialized against the AM stream. */
static ucs_status_t rm_send(uct_rm_urma_ep_t *ep, uint8_t id,
                            const void *data, size_t n)
{
    rm_op_t *op;
    rm_header_t *h;
    ucs_status_t st = rm_ready(ep);
    if (st != UCS_OK) return st;
    if (id >= UCT_AM_ID_MAX || n > RM_PAYLOAD_MAX || ep->issued == UINT64_MAX)
        return UCS_ERR_INVALID_PARAM;
    op = rm_op_alloc(ep->iface, sizeof(*h) + n);
    if (!op) return UCS_ERR_NO_MEMORY;
    op->ep = ep; op->kind = RM_DATA; op->target = ep->target; op->seq = ep->issued + 1;
    h = op->buffer;
    h->magic = htonl(RM_MAGIC); h->version = RM_VERSION; h->type = RM_DATA; h->am_id = id;
    h->stream = htobe64(ep->stream); h->seq = htobe64(op->seq);
    h->source = ep->iface->address; h->destination = ep->remote.cookie;
    if (n) memcpy((char *)op->buffer + sizeof(*h), data, n);
    st = rm_post_op(ep->iface, op);
    if (st != UCS_OK) { rm_op_release(ep->iface, op); return st; }
    ep->issued++; ep->outstanding++; ep->am_outstanding++;
    return UCS_OK;
}
ucs_status_t rm_ep_am_short(uct_ep_h tl, uint8_t id, uint64_t header,
                            const void *payload, unsigned length)
{
    unsigned char data[RM_PAYLOAD_MAX];
    if (length > sizeof(data) - sizeof(header)) return UCS_ERR_INVALID_PARAM;
    memcpy(data, &header, sizeof(header));
    if (length) memcpy(data + sizeof(header), payload, length);
    return rm_send(ucs_derived_of(tl, uct_rm_urma_ep_t), id, data, length + sizeof(header));
}
ssize_t rm_ep_am_bcopy(uct_ep_h tl, uint8_t id, uct_pack_callback_t pack,
                       void *arg, unsigned flags)
{
    unsigned char data[RM_PAYLOAD_MAX];
    uct_rm_urma_ep_t *ep = ucs_derived_of(tl, uct_rm_urma_ep_t);
    ucs_status_t st = rm_ready(ep);
    size_t n;
    if (st != UCS_OK) return st;
    n = pack(data, arg); /* UCT caller obeys the advertised max_bcopy. */
    if (n > sizeof(data)) return UCS_ERR_INVALID_PARAM;
    st = rm_send(ep, id, data, n);
    return st == UCS_OK ? (ssize_t)n : st;
}
static ucs_status_t rm_rma(uct_rm_urma_ep_t *ep, const uct_iov_t *iov,
                            size_t count, const void *copy, size_t n,
                            uint64_t remote_addr, uct_rkey_t key, int read,
                            uct_completion_t *comp, uct_unpack_callback_t unpack,
                            void *unpack_arg)
{
    uct_ub_rkey_info_t *rkey = (void *)key;
    urma_import_seg_flag_t flags = {0};
    urma_token_t token = {.token = ntohl(ep->remote.token)};
    rm_op_t *op;
    void *buf;
    ucs_status_t st = rm_ready(ep);
    if (st != UCS_OK) return st;
    if (ep->outstanding) return UCS_ERR_NO_RESOURCE;
    if (!ep->iface->config.native_rma || !(ntohl(ep->remote.caps) & RM_CAP_RMA))
        return UCS_ERR_UNSUPPORTED;
    if (count) {
        if (count != 1 || iov->count != 1 || iov->memh == UCT_MEM_HANDLE_NULL)
            return UCS_ERR_INVALID_PARAM;
        n = iov->length;
        if (n) {
            urma_target_seg_t *local_seg = uct_ub_memh_get_tseg(iov->memh);
            uint64_t addr = (uintptr_t)iov->buffer;
            if (!local_seg || addr < local_seg->seg.ubva.va ||
                addr - local_seg->seg.ubva.va > local_seg->seg.len ||
                n > local_seg->seg.len - (addr - local_seg->seg.ubva.va))
                return UCS_ERR_INVALID_PARAM;
        }
    }
    if (!n) return UCS_OK;
    if (!count && n > RM_PAYLOAD_MAX) return UCS_ERR_INVALID_PARAM;
    if (n > 1048576 || n > (read ? ep->iface->md->dev.dev_attr.dev_cap.max_read_size :
                                  ep->iface->md->dev.dev_attr.dev_cap.max_write_size) ||
        !rkey || memcmp(&rkey->remote_seg.ubva.eid, ep->remote.eid, 16) ||
        rkey->remote_seg.ubva.uasid != ntohl(ep->remote.uasid) ||
        remote_addr < rkey->remote_seg.ubva.va ||
        remote_addr - rkey->remote_seg.ubva.va > rkey->remote_seg.len ||
        n > rkey->remote_seg.len - (remote_addr - rkey->remote_seg.ubva.va))
        return UCS_ERR_INVALID_PARAM;
    /* Own each imported segment through completion; never replay a failed WRITE. */
    op = rm_op_alloc(ep->iface, count ? 0 : n);
    if (!op) return UCS_ERR_NO_MEMORY;
    flags.bs.access = read ? URMA_ACCESS_READ : URMA_ACCESS_WRITE;
    op->remote_seg = urma_import_seg(ep->iface->md->dev.urma_ctx,
                                     &rkey->remote_seg, &token, 0, flags);
    if (!op->remote_seg) { rm_op_release(ep->iface, op); return UCS_ERR_IO_ERROR; }
    buf = count ? iov->buffer : op->buffer;
    if (!read && !count) {
        if (!copy) { rm_op_release(ep->iface, op); return UCS_ERR_INVALID_PARAM; }
        memcpy(buf, copy, n);
    }
    op->kind = 3; op->ep = ep; op->target = ep->target;
    op->opcode = read ? URMA_OPC_READ : URMA_OPC_WRITE;
    op->local.addr = (uintptr_t)buf; op->local.len = n;
    op->local.tseg = count ? uct_ub_memh_get_tseg(iov->memh) : op->seg;
    op->remote.addr = remote_addr; op->remote.len = n; op->remote.tseg = op->remote_seg;
    op->comp = comp; op->unpack = unpack; op->unpack_arg = unpack_arg;
    st = rm_post_op(ep->iface, op);
    if (st != UCS_OK) { rm_op_release(ep->iface, op); return st; }
    ep->outstanding++; ep->rma_outstanding = 1;
    return count || comp || unpack ? UCS_INPROGRESS : UCS_OK;
}
ucs_status_t rm_ep_put_short(uct_ep_h ep, const void *buf, unsigned n, uint64_t addr, uct_rkey_t key)
{ return rm_rma(ucs_derived_of(ep, uct_rm_urma_ep_t), NULL, 0, buf, n, addr, key, 0, NULL, NULL, NULL); }
ssize_t rm_ep_put_bcopy(uct_ep_h tl, uct_pack_callback_t cb, void *arg, uint64_t addr, uct_rkey_t key)
{
    char buf[RM_PAYLOAD_MAX];
    uct_rm_urma_ep_t *ep = ucs_derived_of(tl, uct_rm_urma_ep_t);
    ucs_status_t st = rm_ready(ep);
    size_t n;
    if (st != UCS_OK) return st;
    n = cb(buf, arg);
    if (n > sizeof(buf)) return UCS_ERR_INVALID_PARAM;
    st = rm_rma(ep, NULL, 0, buf, n, addr, key, 0, NULL, NULL, NULL);
    return st == UCS_OK ? (ssize_t)n : st;
}
ucs_status_t rm_ep_get_bcopy(uct_ep_h ep, uct_unpack_callback_t cb, void *arg, size_t n, uint64_t addr, uct_rkey_t key, uct_completion_t *comp)
{ return rm_rma(ucs_derived_of(ep, uct_rm_urma_ep_t), NULL, 0, NULL, n, addr, key, 1, comp, cb, arg); }
ucs_status_t rm_ep_put_zcopy(uct_ep_h ep, const uct_iov_t *iov, size_t count, uint64_t addr, uct_rkey_t key, uct_completion_t *comp)
{ return rm_rma(ucs_derived_of(ep, uct_rm_urma_ep_t), iov, count, NULL, 0, addr, key, 0, comp, NULL, NULL); }
ucs_status_t rm_ep_get_zcopy(uct_ep_h ep, const uct_iov_t *iov, size_t count, uint64_t addr, uct_rkey_t key, uct_completion_t *comp)
{ return rm_rma(ucs_derived_of(ep, uct_rm_urma_ep_t), iov, count, NULL, 0, addr, key, 1, comp, NULL, NULL); }
ucs_status_t rm_ep_flush(uct_ep_h tl, unsigned flags, uct_completion_t *comp)
{
    uct_rm_urma_ep_t *ep = ucs_derived_of(tl, uct_rm_urma_ep_t);
    rm_flush_req_t *req;
    if (flags) return UCS_ERR_UNSUPPORTED; /* REMOTE visibility needs hardware-specific evidence. */
    if (ep->failed != UCS_OK) return ep->failed;
    if (!ep->outstanding) return UCS_OK;
    if (comp) {
        req = calloc(1, sizeof(*req));
        if (!req) return UCS_ERR_NO_MEMORY;
        req->barrier = ep->iface->next_id - 1;
        req->comp = comp; req->next = ep->flushes; ep->flushes = req;
    }
    return UCS_INPROGRESS;
}
void rm_ep_progress_flush(uct_rm_urma_ep_t *ep)
{
    rm_flush_req_t **p = &ep->flushes, *req;
    rm_op_t *op;
    uct_completion_t *comp;
    while ((req = *p)) {
        for (op = ep->iface->ops; op; op = op->next)
            if (op->ep == ep && op->id <= req->barrier) break;
        if (op) { p = &req->next; continue; }
        *p = req->next; comp = req->comp; free(req);
        uct_invoke_completion(comp, ep->closing ? UCS_ERR_CANCELED : ep->failed);
        /* Callback may have enqueued another flush or requested endpoint close. */
        p = &ep->flushes;
    }
}
ucs_status_t rm_ep_fence(uct_ep_h tl, unsigned flags)
{
    uct_rm_urma_ep_t *ep = ucs_derived_of(tl, uct_rm_urma_ep_t);
    if (flags) return UCS_ERR_UNSUPPORTED;
    ep->fenced = 1; return UCS_OK;
}
ucs_status_t rm_ep_check(uct_ep_h tl, unsigned flags, uct_completion_t *comp)
{
    uct_rm_urma_ep_t *ep = ucs_derived_of(tl, uct_rm_urma_ep_t);
    return ep->failed != UCS_OK ? ep->failed : UCS_ERR_UNSUPPORTED;
}
ucs_status_t rm_ep_pending_add(uct_ep_h tl, uct_pending_req_t *req, unsigned flags)
{
    uct_rm_urma_ep_t *ep = ucs_derived_of(tl, uct_rm_urma_ep_t);
    if (ep->failed != UCS_OK) return ep->failed;
    /* Native RMA also waits for prior AM ACKs, even when the AM window has room.
     * BUSY here would make UCP retry immediately without progressing those ACKs. */
    if (!ep->outstanding && rm_ready(ep) == UCS_OK) return UCS_ERR_BUSY;
    ucs_queue_push(&ep->pending, (ucs_queue_elem_t *)req->priv);
    return UCS_OK;
}
void rm_ep_pending_purge(uct_ep_h tl, uct_pending_purge_callback_t cb, void *arg)
{
    uct_rm_urma_ep_t *ep = ucs_derived_of(tl, uct_rm_urma_ep_t);
    uct_pending_req_t *req;
    ucs_queue_elem_t *elem;
    while (!ucs_queue_is_empty(&ep->pending)) {
        elem = ucs_queue_pull_non_empty(&ep->pending);
        req = ucs_container_of(elem, uct_pending_req_t, priv);
        if (cb) cb(req, arg);
    }
}
void rm_ep_progress_pending(uct_rm_urma_ep_t *ep)
{
    uct_pending_req_t *req;
    ucs_queue_elem_t *elem;
    if (ep->failed != UCS_OK || ep->closing || ucs_queue_is_empty(&ep->pending)) return;
    /* Remove first, so a user callback can safely purge/destroy this endpoint. */
    elem = ucs_queue_pull_non_empty(&ep->pending);
    req = ucs_container_of(elem, uct_pending_req_t, priv);
    if (req->func(req) != UCS_OK && !ep->closing)
        ucs_queue_push(&ep->pending, elem);
}
void rm_ep_fail(uct_rm_urma_ep_t *ep, ucs_status_t st)
{
    if (ep->failed != UCS_OK || ep->closing) return;
    ep->failed = st;
    uct_iface_handle_ep_err(&ep->iface->super.super, &ep->super.super, st);
}
void rm_ep_reap(uct_rm_urma_ep_t *ep)
{
    uct_rm_urma_ep_t **p;
    if (!ep->closing || ep->outstanding || ep->flushes || ep->iface->in_progress) return;
    for (p = &ep->iface->eps; *p && *p != ep; p = &(*p)->next) {}
    if (*p) *p = ep->next;
    if (ep->target) urma_unimport_jetty(ep->target);
    UCS_CLASS_CLEANUP(uct_rm_urma_ep_t, ep);
    free(ep);
}
UCS_CLASS_INIT_FUNC(uct_rm_urma_ep_t, const uct_ep_params_t *params)
{
    uct_rm_urma_iface_t *iface = ucs_derived_of(params->iface, uct_rm_urma_iface_t);
    if (iface->failed != UCS_OK) return iface->failed;
    UCT_EP_PARAMS_CHECK_DEV_IFACE_ADDRS(params);
    UCS_CLASS_CALL_SUPER_INIT(uct_base_ep_t, &iface->super);
    memcpy(&self->remote, params->iface_addr, sizeof(self->remote));
    if (!rm_address_valid(&self->remote)) return UCS_ERR_UNREACHABLE;
    self->iface = iface; self->failed = UCS_OK; self->closing = 0;
    self->outstanding = self->am_outstanding = 0; self->rma_outstanding = self->fenced = 0; self->issued = 0;
    self->stream = ucs_generate_uuid((uintptr_t)self);
    if (!self->stream) self->stream = 1;
    ucs_queue_head_init(&self->pending);
    self->flushes = NULL;
    self->target = rm_import(iface, &self->remote);
    if (!self->target) return UCS_ERR_IO_ERROR;
    self->next = iface->eps; iface->eps = self;
    return UCS_OK;
}
static UCS_CLASS_CLEANUP_FUNC(uct_rm_urma_ep_t) {}
UCS_CLASS_DEFINE(uct_rm_urma_ep_t, uct_base_ep_t);
UCS_CLASS_DEFINE_NEW_FUNC(uct_rm_urma_ep_t, uct_ep_t, const uct_ep_params_t *);
/* Explicit destroy retains the endpoint while the device still owns operations. */
void UCS_CLASS_DELETE_FUNC_NAME(uct_rm_urma_ep_t)(uct_ep_t *tl)
{
    uct_rm_urma_ep_t *ep = ucs_derived_of(tl, uct_rm_urma_ep_t);
    ep->closing = 1;
    rm_ep_pending_purge(tl, NULL, NULL);
    rm_ep_reap(ep);
}
