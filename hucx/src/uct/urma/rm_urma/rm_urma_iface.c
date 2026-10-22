#include "rm_urma_iface.h"
#include "rm_urma_ep.h"
#include "rm_urma_wireup.h"
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ucs/time/time.h>

static ucs_config_field_t rm_config_table[] = {
    {"", "", NULL, ucs_offsetof(uct_rm_urma_config_t, super), UCS_CONFIG_TYPE_TABLE(uct_iface_config_table)},
    {"RX_DEPTH", "256", "Receive buffers", ucs_offsetof(uct_rm_urma_config_t, rx_depth), UCS_CONFIG_TYPE_UINT},
    {"TX_DEPTH", "256", "Global work budget; eight entries reserved for ACKs", ucs_offsetof(uct_rm_urma_config_t, tx_depth), UCS_CONFIG_TYPE_UINT},
    {"PRIORITY", "1", "CTP-enabled device priority (verify device mapping)", ucs_offsetof(uct_rm_urma_config_t, priority), UCS_CONFIG_TYPE_UINT},
    {"RETRIES", "8", "AM retransmission limit", ucs_offsetof(uct_rm_urma_config_t, retries), UCS_CONFIG_TYPE_UINT},
    {"TIMEOUT", "10ms", "Initial AM ACK timeout; exponential backoff capped at 32x", ucs_offsetof(uct_rm_urma_config_t, timeout), UCS_CONFIG_TYPE_TIME},
    {"COMPLETION_TIMEOUT", "5s", "Local CQ deadline; stop shared queues before completing failed DMA operations", ucs_offsetof(uct_rm_urma_config_t, completion_timeout), UCS_CONFIG_TYPE_TIME},
    {"MAX_PEERS", "4096", "Bounded receive-stream table retained until iface close", ucs_offsetof(uct_rm_urma_config_t, max_peers), UCS_CONFIG_TYPE_UINT},
    {"MAX_BUFFERED", "16M", "Global out-of-order payload budget", ucs_offsetof(uct_rm_urma_config_t, max_buffered), UCS_CONFIG_TYPE_MEMUNITS},
    {"NATIVE_RMA", "y", "Enable native READ/WRITE (errors terminate, never blindly replay)", ucs_offsetof(uct_rm_urma_config_t, native_rma), UCS_CONFIG_TYPE_BOOL},
    {"TRACE", "n", "Log URMA operation and completion (functional tracing only)", ucs_offsetof(uct_rm_urma_config_t, trace), UCS_CONFIG_TYPE_BOOL},
    {"DIAG", "n", "Bounded timing diagnostics; may perturb timing, not a performance measurement",
     ucs_offsetof(uct_rm_urma_config_t, diag), UCS_CONFIG_TYPE_BOOL},
    {"TEST_DROP_DATA", "0",
     "TEST ONLY: discard the first N valid received DATA packets per iface",
     ucs_offsetof(uct_rm_urma_config_t, test_drop_data), UCS_CONFIG_TYPE_UINT},
    {"TEST_DROP_ACK", "0",
     "TEST ONLY: discard the first N valid received ACK packets per iface",
     ucs_offsetof(uct_rm_urma_config_t, test_drop_ack), UCS_CONFIG_TYPE_UINT},
    {"TX_POOL", "n", "Experimental pre-registered DATA/ACK buffer pool",
     ucs_offsetof(uct_rm_urma_config_t, tx_pool), UCS_CONFIG_TYPE_BOOL},
    {NULL}
};
/* UMMU table mode requires segment bases to be aligned to 4 KiB. */
#define RM_BUFFER_ALIGNMENT 4096u

/* At most 32 slow progress records, one first-CQE error, and two summaries.
 * Timings are wall time, including scheduling and provider waits. */
#define RM_DIAG_LOG_LIMIT 32u
#define RM_DIAG_PHASES 6u

static double rm_diag_us(uint64_t ticks)
{
    return ucs_time_to_sec(ticks) * 1e6;
}

static void rm_diag_max(uint64_t *maximum, uint64_t value)
{
    if (value > *maximum) {
        *maximum = value;
    }
}

static void rm_diag_phase(uct_rm_urma_iface_t *iface, unsigned phase,
                          uint64_t *stamp, uint64_t *durations)
{
    uint64_t now;

    if (!iface->diag.active) {
        return;
    }
    now = ucs_get_time();
    durations[phase] = now - *stamp;
    *stamp = now;
    rm_diag_max(&iface->diag.phase_max[phase], durations[phase]);
}

static unsigned rm_diag_ack_pending(uct_rm_urma_iface_t *iface)
{
    rm_peer_t *peer;
    unsigned count = 0;

    for (peer = iface->peers; peer; peer = peer->next) {
        count += !!peer->ack_pending;
    }
    return count;
}

static void rm_diag_summary(uct_rm_urma_iface_t *iface, const char *reason)
{
    rm_diag_t *d = &iface->diag;

    if (!d->active) {
        return;
    }
    ucs_info("RM_CTP_DIAG_SUMMARY reason=%s calls=%" PRIu64
             " rx=%" PRIu64 " tx=%" PRIu64 " rx_full=%" PRIu64
             " tx_full=%" PRIu64 " reposts=%" PRIu64
             " repost_errors=%" PRIu64 " repost_max_us=%.3f"
             " reg_calls=%" PRIu64 " reg_total_us=%.3f reg_max_us=%.3f"
             " unreg_calls=%" PRIu64 " unreg_total_us=%.3f unreg_max_us=%.3f"
             " gap_max_us=%.3f progress_max_us=%.3f"
             " rx_max_us=%.3f tx_max_us=%.3f ops_max_us=%.3f"
             " ack_max_us=%.3f pending_max_us=%.3f tail_max_us=%.3f"
             " slow_logs=%u",
             reason, d->calls, d->rx_records, d->tx_records, d->rx_full,
             d->tx_full, d->reposts, d->repost_errors,
             rm_diag_us(d->repost_max), d->registers,
             rm_diag_us(d->register_ticks), rm_diag_us(d->register_max),
             d->unregisters, rm_diag_us(d->unregister_ticks),
             rm_diag_us(d->unregister_max), rm_diag_us(d->gap_max),
             rm_diag_us(d->progress_max), rm_diag_us(d->phase_max[0]),
             rm_diag_us(d->phase_max[1]), rm_diag_us(d->phase_max[2]),
             rm_diag_us(d->phase_max[3]), rm_diag_us(d->phase_max[4]),
             rm_diag_us(d->phase_max[5]), d->slow_logs);
}

/* Before teardown: excludes RX buffer deregistration and queue destruction.
 * Includes initialization wireup and benchmark warmup; compare short/long runs.
 * CQE elapsed is overlapping wall time, never sum it with CPU/API categories. */
static void rm_cost_summary(uct_rm_urma_iface_t *iface)
{
    unsigned k;
    rm_diag_t *d = &iface->diag;
    if (!d->active) return;
    for (k = 1; k <= 3; ++k) {
        ucs_info("RM_COST_V1 kind=%u released=%" PRIu64
                 " alloc_us=%.3f reg_us=%.3f unreg_us=%.3f"
                 " posts=%" PRIu64 " post_us=%.3f cqes=%" PRIu64
                 " post_to_cqe_us=%.3f live_ops=%u scope=before_teardown",
                 k, d->cost_count[k], rm_diag_us(d->cost_alloc[k]),
                 rm_diag_us(d->cost_reg[k]), rm_diag_us(d->cost_unreg[k]),
                 d->cost_post_count[k], rm_diag_us(d->cost_post[k]),
                 d->cost_cqe_count[k], rm_diag_us(d->cost_cqe[k]), iface->op_count);
    }
}
static void rm_diag_cqe_error(uct_rm_urma_iface_t *iface, const char *queue,
                              const urma_cr_t *cr, const rm_op_t *op)
{
    if (!iface->diag.active || iface->diag.error_logged ||
        cr->status == URMA_CR_SUCCESS) {
        return;
    }
    iface->diag.error_logged = 1;
    ucs_info("RM_CTP_DIAG_CQE queue=%s status=%u id=%" PRIu64
             " known_op=%d kind=%d seq=%" PRIu64 " attempts=%u"
             " acked=%d op_count=%u inflight=%u ack_pending=%u",
             queue, (unsigned)cr->status, (uint64_t)cr->user_ctx,
             op != NULL, op ? op->kind : 0, op ? op->seq : 0,
             op ? op->attempts : 0, op ? op->acked : 0,
             iface->op_count, iface->inflight, rm_diag_ack_pending(iface));
}

static void rm_diag_unregister(uct_rm_urma_iface_t *iface,
                                urma_target_seg_t *segment)
{
    uint64_t start = 0, duration;
    int saved_errno;

    if (iface->diag.active) {
        start = ucs_get_time();
    }
    urma_unregister_seg(segment);
    saved_errno = errno;
    if (iface->diag.active) {
        duration = ucs_get_time() - start;
        iface->diag.unregisters++;
        iface->diag.unregister_ticks += duration;
        rm_diag_max(&iface->diag.unregister_max, duration);
    }
    errno = saved_errno;
}

static void *rm_buffer_alloc(size_t length)
{
    void *buffer = NULL;
    size_t allocation_size;
    int allocation_status;

    if ((length == 0) ||
        (length > SIZE_MAX - (RM_BUFFER_ALIGNMENT - 1))) {
        ucs_error("rm_urma: invalid registered buffer length %zu", length);
        return NULL;
    }

    /* Reserve whole alignment units, while keeping the wire length unchanged. */
    allocation_size = (length + RM_BUFFER_ALIGNMENT - 1) &
                      ~((size_t)RM_BUFFER_ALIGNMENT - 1);
    allocation_status = posix_memalign(&buffer, RM_BUFFER_ALIGNMENT,
                                       allocation_size);
    if (allocation_status != 0) {
        ucs_error("rm_urma: aligned allocation failed, bytes=%zu, status=%d",
                  allocation_size, allocation_status);
        return NULL;
    }

    memset(buffer, 0, allocation_size);
    return buffer;
}

urma_target_seg_t *rm_register(uct_rm_urma_iface_t *iface, void *buf, size_t n)
{
    urma_seg_cfg_t cfg = {0};
    urma_target_seg_t *segment;
    int register_errno;
    uint64_t diag_start = 0, diag_duration;

    cfg.va = (uintptr_t)buf;
    cfg.len = n;
    cfg.token_value = iface->md->dev.token;
    cfg.flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE;
    cfg.flag.bs.token_policy = URMA_TOKEN_NONE;

    if (iface->diag.active) {
        diag_start = ucs_get_time();
    }
    segment = urma_register_seg(iface->md->dev.urma_ctx, &cfg);
    register_errno = errno;
    if (iface->diag.active) {
        diag_duration = ucs_get_time() - diag_start;
        iface->diag.registers++;
        iface->diag.register_ticks += diag_duration;
        rm_diag_max(&iface->diag.register_max, diag_duration);
    }
    errno = register_errno;
    if (segment == NULL) {
        ucs_error("rm_urma: register buffer failed, address=%p, bytes=%zu, "
                  "errno=%d (%s)", buf, n, register_errno,
                  strerror(register_errno));
        errno = register_errno;
    }

    return segment;
}
rm_op_t *rm_op_alloc(uct_rm_urma_iface_t *iface, size_t n)
{
    rm_op_t *op;
    uint64_t cost_start = iface->diag.active ? ucs_get_time() : 0;
    uint64_t cost_reg_before = iface->diag.register_ticks;
    if (iface->op_count >= iface->config.tx_depth || iface->next_id >= (UINT64_C(1)<<63)) return NULL;
    op = calloc(1, sizeof(*op));
    if (!op) return NULL;
    if (n && iface->config.tx_pool) {
        if (n > RM_PACKET_MAX || !iface->tx_pool_available) {
            free(op); return NULL;
        }
        op->pool_slot = iface->tx_pool_free[--iface->tx_pool_available];
        op->pooled = 1;
        op->buffer = (char *)iface->tx_pool_buffer +
                     (size_t)op->pool_slot * RM_BUFFER_ALIGNMENT;
        op->seg = iface->tx_pool_seg;
        /* Same zeroed-header guarantee as the original allocator. */
        memset(op->buffer, 0, RM_BUFFER_ALIGNMENT);
    } else if (n) {
        op->buffer = rm_buffer_alloc(n);
        if (!op->buffer) { free(op); return NULL; }
        op->seg = rm_register(iface, op->buffer, n);
        if (!op->seg) { free(op->buffer); free(op); return NULL; }
    }
    if (iface->diag.active) {
        op->cost_reg = iface->diag.register_ticks - cost_reg_before;
        op->cost_alloc = ucs_get_time() - cost_start - op->cost_reg;
    }
    op->length = n; op->id = iface->next_id++;
    op->next = iface->ops; iface->ops = op; iface->op_count++;
    return op;
}
void rm_op_release(uct_rm_urma_iface_t *iface, rm_op_t *op)
{
    rm_op_t **p;
    unsigned cost_kind = (unsigned)op->kind;
    uint64_t cost_unreg_before = iface->diag.unregister_ticks;
    for (p = &iface->ops; *p && *p != op; p = &(*p)->next) {}
    if (*p) *p = op->next;
    if (op->remote_seg) urma_unimport_seg(op->remote_seg);
    if (op->seg && !op->pooled) rm_diag_unregister(iface, op->seg);
    if (iface->diag.active && cost_kind >= 1 && cost_kind <= 3) {
        iface->diag.cost_count[cost_kind]++;
        iface->diag.cost_alloc[cost_kind] += op->cost_alloc;
        iface->diag.cost_reg[cost_kind] += op->cost_reg;
        iface->diag.cost_unreg[cost_kind] += iface->diag.unregister_ticks - cost_unreg_before;
    }
    if (op->pooled) {
        ucs_assert(iface->tx_pool_available < iface->config.tx_depth);
        iface->tx_pool_free[iface->tx_pool_available++] = op->pool_slot;
    } else {
        free(op->buffer);
    }
    free(op); iface->op_count--;
}
ucs_status_t rm_post_op(uct_rm_urma_iface_t *iface, rm_op_t *op)
{
    uint64_t cost_post_start;
    urma_status_t cost_post_status;
    urma_jfs_wr_t wr = {0}, *bad = NULL;
    urma_sge_t sge = { .addr = (uintptr_t)op->buffer, .len = op->length, .tseg = op->seg };
    if (iface->failed != UCS_OK) return iface->failed;
    if (iface->inflight >= iface->config.tx_depth) return UCS_ERR_NO_RESOURCE;
    wr.user_ctx = op->id; wr.tjetty = op->target; wr.flag.bs.complete_enable = 1;
    wr.opcode = op->kind == 3 ? op->opcode : URMA_OPC_SEND;
    if (op->kind == 3) {
        wr.rw.src.sge = op->opcode == URMA_OPC_READ ? &op->remote : &op->local;
        wr.rw.dst.sge = op->opcode == URMA_OPC_READ ? &op->local : &op->remote;
        wr.rw.src.num_sge = wr.rw.dst.num_sge = 1;
    } else {
        wr.send.src.sge = &sge; wr.send.src.num_sge = 1; wr.send.tseg = op->seg;
    }
    cost_post_start = iface->diag.active ? ucs_get_time() : 0;
    cost_post_status = urma_post_jetty_send_wr(iface->jetty, &wr, &bad);
    if (iface->diag.active && op->kind >= 1 && op->kind <= 3) {
        iface->diag.cost_post_count[op->kind]++;
        iface->diag.cost_post[op->kind] += ucs_get_time() - cost_post_start;
    }
    if (cost_post_status != URMA_SUCCESS) {
        iface->failed = UCS_ERR_IO_ERROR; /* Notify during progress, not inside the posting call. */
        if (bad == &wr) return UCS_ERR_IO_ERROR; /* Definitely not posted. */
        /* A provider that omits bad_wr leaves acceptance ambiguous. Retain the
         * operation and its memory until progress quiesces the queue. */
    }
    op->inflight = 1; op->sent = ucs_get_time(); iface->inflight++;
    if (iface->config.trace)
        ucs_info("RM_CTP_POST id=%" PRIu64 " kind=%d opcode=%s bytes=%zu attempt=%u",
                 op->id, op->kind, op->kind == 3 ? (op->opcode == URMA_OPC_READ ? "READ" : "WRITE") : "SEND",
                 op->kind == 3 ? (size_t)op->local.len : op->length, op->attempts);
    return UCS_OK;
}
ucs_status_t rm_post_recv(uct_rm_urma_iface_t *iface, unsigned i)
{
    urma_sge_t sge = {.addr = (uintptr_t)iface->recv[i].buffer, .len = RM_PACKET_MAX, .tseg = iface->recv[i].seg};
    urma_jfr_wr_t wr = {.src = {.sge = &sge, .num_sge = 1}, .user_ctx = (UINT64_C(1)<<63) | i}, *bad = NULL;
    uint64_t diag_start = 0, diag_duration;
    int result, saved_errno;

    if (iface->diag.active) {
        diag_start = ucs_get_time();
    }
    result = urma_post_jetty_recv_wr(iface->jetty, &wr, &bad);
    saved_errno = errno;
    if (iface->diag.active) {
        diag_duration = ucs_get_time() - diag_start;
        iface->diag.reposts++;
        iface->diag.repost_errors += result != URMA_SUCCESS;
        rm_diag_max(&iface->diag.repost_max, diag_duration);
    }
    errno = saved_errno;
    if (result != URMA_SUCCESS) return UCS_ERR_IO_ERROR;
    iface->recv[i].posted = 1;
    return UCS_OK;
}
static void rm_finish(uct_rm_urma_iface_t *iface, rm_op_t *op, ucs_status_t st)
{
    uct_rm_urma_ep_t *ep = op->ep;
    uct_completion_t *comp = op->comp;
    if (ep) {
        ep->outstanding--;
        if (op->kind == RM_DATA) ep->am_outstanding--;
        else ep->rma_outstanding = 0;
    }
    if (st == UCS_OK && op->unpack) op->unpack(op->unpack_arg, op->buffer, op->local.len);
    rm_op_release(iface, op);
    if (comp) uct_invoke_completion(comp, st);
}
static void rm_iface_fail(uct_rm_urma_iface_t *iface, ucs_status_t st)
{
    uct_rm_urma_ep_t *ep;
    iface->failed = st;
    for (ep = iface->eps; ep; ep = ep->next) rm_ep_fail(ep, st);
}
/* Successful queue deletion is the quiescence boundary for registered memory.
 * If deletion fails, retain pinned memory and don't falsely complete zcopy DMA. */
static int rm_stop_io(uct_rm_urma_iface_t *iface)
{
    if (iface->jetty) {
        if (urma_delete_jetty(iface->jetty) != URMA_SUCCESS) return 0;
        iface->jetty = NULL;
    }
    if (iface->jfr) {
        if (urma_delete_jfr(iface->jfr) != URMA_SUCCESS) return 0;
        iface->jfr = NULL;
    }
    return 1;
}
unsigned uct_rm_urma_iface_progress(void *arg)
{
    uct_rm_urma_iface_t *iface = arg;
    urma_cr_t cr[16];
    int n, j;
    unsigned total = 0, i;
    rm_op_t *op, *next;
    uct_rm_urma_ep_t *ep, *ep_next;
    uint64_t now, rx_id;
    uint64_t diag_start = 0, diag_stamp = 0, diag_gap = 0, diag_duration;
    uint64_t diag_phases[RM_DIAG_PHASES] = {0};
    if (iface->in_progress) return 0;
    iface->in_progress = 1;
    if (iface->diag.active) {
        diag_start = ucs_get_time();
        diag_stamp = diag_start;
        if (iface->diag.last_exit) {
            diag_gap = diag_start - iface->diag.last_exit;
            rm_diag_max(&iface->diag.gap_max, diag_gap);
        }
        iface->diag.calls++;
    }
    if (iface->failed != UCS_OK) goto failed;
    /* Providers may populate only part of each completion record. */
    memset(cr, 0, sizeof(cr));
    n = urma_poll_jfc(iface->rx_cq, 16, cr);
    if (iface->diag.active && n > 0) {
        iface->diag.rx_records += n;
        iface->diag.rx_full += n == 16;
    }
    if (n < 0) rm_iface_fail(iface, UCS_ERR_IO_ERROR);
    for (j = 0; j < n; ++j) {
        rm_diag_cqe_error(iface, "rx", &cr[j], NULL);
        /* Fake CQEs have no valid user_ctx. Fail conservatively without dereference. */
        if (cr[j].status == URMA_CR_WR_SUSPEND_DONE || cr[j].status == URMA_CR_WR_FLUSH_ERR_DONE) {
            rm_iface_fail(iface, UCS_ERR_IO_ERROR); continue;
        }
        rx_id = cr[j].user_ctx & ~(UINT64_C(1)<<63);
        if (!(cr[j].user_ctx >> 63) || rx_id >= iface->config.rx_depth) {
            rm_iface_fail(iface, UCS_ERR_IO_ERROR); continue;
        }
        i = (unsigned)rx_id;
        if (!iface->recv[i].posted) { rm_iface_fail(iface, UCS_ERR_IO_ERROR); continue; }
        iface->recv[i].posted = 0;
        if (cr[j].status != URMA_CR_SUCCESS) rm_iface_fail(iface, UCS_ERR_IO_ERROR);
        else if (iface->failed == UCS_OK)
            rm_receive(iface, iface->recv[i].buffer, cr[j].completion_len, &cr[j].remote_id);
        if (iface->failed == UCS_OK && rm_post_recv(iface, i) != UCS_OK) rm_iface_fail(iface, UCS_ERR_IO_ERROR);
        total++;
    }
    rm_diag_phase(iface, 0, &diag_stamp, diag_phases);
    /* Providers may populate only part of each completion record. */
    memset(cr, 0, sizeof(cr));
    n = urma_poll_jfc(iface->tx_cq, 16, cr);
    if (iface->diag.active && n > 0) {
        iface->diag.tx_records += n;
        iface->diag.tx_full += n == 16;
    }
    if (n < 0) rm_iface_fail(iface, UCS_ERR_IO_ERROR);
    for (j = 0; j < n; ++j) {
        if (cr[j].status == URMA_CR_WR_SUSPEND_DONE || cr[j].status == URMA_CR_WR_FLUSH_ERR_DONE) {
            rm_diag_cqe_error(iface, "tx", &cr[j], NULL);
            rm_iface_fail(iface, UCS_ERR_IO_ERROR); continue;
        }
        for (op = iface->ops; op && op->id != cr[j].user_ctx; op = op->next) {}
        rm_diag_cqe_error(iface, "tx", &cr[j], op);
        if (!op || !op->inflight) { rm_iface_fail(iface, UCS_ERR_IO_ERROR); continue; }
        if (iface->diag.active && op->kind >= 1 && op->kind <= 3) {
            iface->diag.cost_cqe_count[op->kind]++;
            iface->diag.cost_cqe[op->kind] += ucs_get_time() - op->sent;
        }
        op->inflight = 0; iface->inflight--; total++;
        if (iface->config.trace)
            ucs_info("RM_CTP_CQE id=%" PRIu64 " status=%u", op->id, (unsigned)cr[j].status);
        if (cr[j].status != URMA_CR_SUCCESS) {
            /* A hardware queue error can flush unrelated operations. Don't reuse it. */
            rm_iface_fail(iface, UCS_ERR_IO_ERROR);
        } else if (op->kind != RM_DATA && iface->failed == UCS_OK) {
            rm_finish(iface, op, iface->failed);
        }
    }
    rm_diag_phase(iface, 1, &diag_stamp, diag_phases);
    if (iface->failed != UCS_OK) goto failed;
    now = ucs_get_time();
    for (op = iface->ops; op; op = op->next) {
        if (op->inflight && rm_retry_due(now, op->sent, iface->completion_ticks, 0)) {
            rm_iface_fail(iface, UCS_ERR_ENDPOINT_TIMEOUT);
            break;
        }
    }
    if (iface->failed != UCS_OK) goto failed;
    for (op = iface->ops; op; op = next) {
        next = op->next;
        if (op->inflight) continue; /* Device still owns DMA: keep memory pinned. */
        if (iface->failed != UCS_OK || (op->ep && (op->ep->failed != UCS_OK || op->ep->closing))) {
            rm_finish(iface, op, op->ep && op->ep->closing ? UCS_ERR_CANCELED : UCS_ERR_IO_ERROR);
        } else if (op->acked) {
            rm_finish(iface, op, UCS_OK);
        } else if (op->kind == RM_DATA && rm_retry_due(now, op->sent, iface->retry_ticks, op->attempts)) {
            if (op->attempts >= iface->config.retries) {
                rm_ep_fail(op->ep, UCS_ERR_ENDPOINT_TIMEOUT);
                rm_finish(iface, op, UCS_ERR_ENDPOINT_TIMEOUT);
            } else if (rm_post_op(iface, op) == UCS_OK) op->attempts++;
            else { rm_iface_fail(iface, UCS_ERR_IO_ERROR); }
        }
    }
    rm_diag_phase(iface, 2, &diag_stamp, diag_phases);
    if (iface->failed == UCS_OK) {
        rm_progress_acks(iface);
        rm_diag_phase(iface, 3, &diag_stamp, diag_phases);
        for (ep = iface->eps; ep; ep = ep->next) rm_ep_progress_pending(ep);
        rm_diag_phase(iface, 4, &diag_stamp, diag_phases);
    }
failed:
    if (iface->failed != UCS_OK)
        for (ep = iface->eps; ep; ep = ep->next) rm_ep_fail(ep, iface->failed);
    if (iface->failed != UCS_OK && rm_stop_io(iface)) {
        iface->inflight = 0;
        while (iface->ops) rm_finish(iface, iface->ops, iface->failed);
    }
    for (ep = iface->eps; ep; ep = ep->next) rm_ep_progress_flush(ep);
    iface->in_progress = 0;
    for (ep = iface->eps; ep; ep = ep_next) { ep_next = ep->next; rm_ep_reap(ep); }
    rm_diag_phase(iface, 5, &diag_stamp, diag_phases);
    if (iface->diag.active) {
        diag_duration = diag_stamp - diag_start;
        rm_diag_max(&iface->diag.progress_max, diag_duration);
        if (iface->diag.slow_logs < RM_DIAG_LOG_LIMIT &&
            (diag_gap >= ucs_time_from_sec(0.005) ||
             diag_duration >= ucs_time_from_sec(0.005))) {
            iface->diag.slow_logs++;
            ucs_info("RM_CTP_DIAG_PROGRESS call=%" PRIu64
                     " gap_us=%.3f total_us=%.3f rx_us=%.3f tx_us=%.3f"
                     " ops_us=%.3f ack_us=%.3f pending_us=%.3f tail_us=%.3f"
                     " rx_total=%" PRIu64 " tx_total=%" PRIu64
                     " reg_max_us=%.3f unreg_max_us=%.3f"
                     " op_count=%u inflight=%u ack_pending=%u",
                     iface->diag.calls, rm_diag_us(diag_gap),
                     rm_diag_us(diag_duration), rm_diag_us(diag_phases[0]),
                     rm_diag_us(diag_phases[1]), rm_diag_us(diag_phases[2]),
                     rm_diag_us(diag_phases[3]), rm_diag_us(diag_phases[4]),
                     rm_diag_us(diag_phases[5]), iface->diag.rx_records,
                     iface->diag.tx_records, rm_diag_us(iface->diag.register_max),
                     rm_diag_us(iface->diag.unregister_max), iface->op_count,
                     iface->inflight, rm_diag_ack_pending(iface));
        }
        if (iface->failed != UCS_OK && !iface->diag.failure_summary) {
            iface->diag.failure_summary = 1;
            rm_diag_summary(iface, "failed");
        }
        /* Exclude our own end-of-call logging from the next caller-gap metric. */
        iface->diag.last_exit = ucs_get_time();
    }
    return total;
}
static void rm_progress_enable(uct_iface_h tl, unsigned flags)
{
    uct_rm_urma_iface_t *iface = ucs_derived_of(tl, uct_rm_urma_iface_t);
    uct_base_iface_progress_enable_cb(&iface->super, uct_rm_urma_iface_progress, flags);
}
static ucs_status_t rm_query(uct_iface_h tl, uct_iface_attr_t *a)
{
    uct_rm_urma_iface_t *iface = ucs_derived_of(tl, uct_rm_urma_iface_t);
    uct_base_iface_query(&iface->super, a);
    a->device_addr_len = 16; a->iface_addr_len = sizeof(rm_address_t); a->ep_addr_len = 0;
    a->cap.flags = UCT_IFACE_FLAG_CONNECT_TO_IFACE | UCT_IFACE_FLAG_AM_SHORT |
                   UCT_IFACE_FLAG_AM_BCOPY | UCT_IFACE_FLAG_PENDING | UCT_IFACE_FLAG_CB_SYNC;
    a->cap.am.max_short = a->cap.am.max_bcopy = RM_PAYLOAD_MAX;
    a->cap.am.opt_zcopy_align = a->cap.am.align_mtu = 1;
    if (iface->config.native_rma) {
        a->cap.flags |= UCT_IFACE_FLAG_PUT_SHORT | UCT_IFACE_FLAG_PUT_BCOPY |
                         UCT_IFACE_FLAG_PUT_ZCOPY | UCT_IFACE_FLAG_GET_BCOPY | UCT_IFACE_FLAG_GET_ZCOPY;
        a->cap.put.max_zcopy = ucs_min(1048576, iface->md->dev.dev_attr.dev_cap.max_write_size);
        a->cap.get.max_zcopy = ucs_min(1048576, iface->md->dev.dev_attr.dev_cap.max_read_size);
        a->cap.put.max_short = a->cap.put.max_bcopy = ucs_min(RM_PAYLOAD_MAX, a->cap.put.max_zcopy);
        a->cap.get.max_bcopy = ucs_min(RM_PAYLOAD_MAX, a->cap.get.max_zcopy);
        a->cap.put.max_iov = a->cap.get.max_iov = 1;
        a->cap.put.opt_zcopy_align = a->cap.put.align_mtu = 1;
        a->cap.get.opt_zcopy_align = a->cap.get.align_mtu = 1;
    }
    a->latency = ucs_linear_func_make(2e-6, 0); /* Uncalibrated selection estimate. */
    a->bandwidth.dedicated = 0; a->bandwidth.shared = 10.0 * 1024 * 1024 * 1024;
    a->overhead = 200e-9; a->priority = 30;
    return UCS_OK;
}
static ucs_status_t rm_get_address(uct_iface_h tl, uct_iface_addr_t *a)
{ memcpy(a, &ucs_derived_of(tl, uct_rm_urma_iface_t)->address, sizeof(rm_address_t)); return UCS_OK; }
static ucs_status_t rm_get_dev_address(uct_iface_h tl, uct_device_addr_t *a)
{ memcpy(a, ucs_derived_of(tl, uct_rm_urma_iface_t)->address.eid, 16); return UCS_OK; }
static int rm_reachable(const uct_iface_h tl, const uct_device_addr_t *dev, const uct_iface_addr_t *a)
{ return a && rm_address_valid((const rm_address_t *)a); }
static ucs_status_t rm_flush(uct_iface_h tl, unsigned flags, uct_completion_t *comp)
{
    uct_rm_urma_iface_t *iface = ucs_derived_of(tl, uct_rm_urma_iface_t);
    if (flags || comp) return UCS_ERR_UNSUPPORTED;
    if (iface->failed != UCS_OK) return iface->failed;
    return iface->ops ? UCS_INPROGRESS : UCS_OK;
}
static void rm_cleanup(uct_rm_urma_iface_t *self);
UCS_CLASS_INIT_FUNC(uct_rm_urma_iface_t, uct_md_h md, uct_worker_h worker,
                           const uct_iface_params_t *params, const uct_iface_config_t *cfg)
{
    uct_rm_urma_config_t *config = (void *)cfg;
    urma_jfc_cfg_t cq = {0};
    urma_jfr_cfg_t rq = {0};
    urma_jetty_cfg_t jq = {0};
    unsigned i;
    ucs_status_t status;
    UCS_CLASS_CALL_SUPER_INIT(uct_base_iface_t, &uct_rm_urma_ops, &uct_base_iface_internal_ops,
                              md, worker, params, cfg UCS_STATS_ARG(NULL) UCS_STATS_ARG("rm_urma"));
    self->md = ucs_derived_of(md, uct_ub_md_t); self->config = *config;
    memset(&self->diag, 0, sizeof(self->diag));
    self->tx_cq = self->rx_cq = NULL; self->jfr = NULL; self->jetty = NULL;
    self->recv = NULL; self->ops = NULL; self->peers = NULL; self->eps = NULL;
    self->tx_pool_buffer = NULL; self->tx_pool_seg = NULL;
    self->tx_pool_free = NULL; self->tx_pool_available = 0;
    self->inflight = self->peer_count = self->op_count = 0;
    self->test_drop_data_remaining = config->test_drop_data;
    self->test_drop_ack_remaining = config->test_drop_ack;
    if (config->test_drop_data || config->test_drop_ack) {
        ucs_info("RM_CTP_TEST_INJECTION data=%u ack=%u",
                 config->test_drop_data, config->test_drop_ack);
    }
    self->buffered = 0;
    self->next_id = 1; self->failed = UCS_OK; self->in_progress = 0;
    self->retry_ticks = ucs_time_from_sec(config->timeout);
    self->completion_ticks = ucs_time_from_sec(config->completion_timeout);
    memset(&self->address, 0, sizeof(self->address));
    if (ucs_derived_of(worker, uct_priv_worker_t)->thread_mode == UCS_THREAD_MODE_MULTI ||
        config->tx_depth < 16 || !config->rx_depth || !config->max_peers ||
        !self->retry_ticks || !self->completion_ticks || config->priority > 15 ||
        !config->max_buffered)
        return UCS_ERR_INVALID_PARAM;
    if (!self->md->dev.dev_attr.dev_cap.rm_tp_cap.bs.ctp) return UCS_ERR_UNSUPPORTED;
    if (!self->md->dev.dev_attr.dev_cap.max_read_size ||
        !self->md->dev.dev_attr.dev_cap.max_write_size) self->config.native_rma = 0;
    cq.depth = config->tx_depth; self->tx_cq = urma_create_jfc(self->md->dev.urma_ctx, &cq);
    cq.depth = config->rx_depth; self->rx_cq = urma_create_jfc(self->md->dev.urma_ctx, &cq);
    if (!self->tx_cq || !self->rx_cq) { status = UCS_ERR_IO_ERROR; goto fail; }
    rq.depth = config->rx_depth; rq.trans_mode = URMA_TM_RM; rq.max_sge = 1;
    rq.jfc = self->rx_cq; rq.token_value = self->md->dev.token;
    rq.min_rnr_timer = URMA_TYPICAL_MIN_RNR_TIMER;
    self->jfr = urma_create_jfr(self->md->dev.urma_ctx, &rq);
    if (!self->jfr) { status = UCS_ERR_IO_ERROR; goto fail; }
    jq.flag.bs.share_jfr = 1; jq.shared.jfr = self->jfr;
    jq.jfs_cfg.depth = config->tx_depth; jq.jfs_cfg.trans_mode = URMA_TM_RM;
    jq.jfs_cfg.priority = config->priority; jq.jfs_cfg.max_sge = 1;
    jq.jfs_cfg.jfc = self->tx_cq; jq.jfs_cfg.err_timeout = URMA_TYPICAL_ERR_TIMEOUT;
    jq.jfs_cfg.rnr_retry = URMA_TYPICAL_RNR_RETRY;
    self->jetty = urma_create_jetty(self->md->dev.urma_ctx, &jq);
    if (!self->jetty) { status = UCS_ERR_IO_ERROR; goto fail; }
    self->address.magic = htonl(RM_MAGIC); self->address.version = htons(RM_VERSION);
    self->address.mtu = htons(RM_PACKET_MAX);
    memcpy(self->address.eid, &self->jetty->jetty_id.eid, 16);
    self->address.uasid = htonl(self->jetty->jetty_id.uasid);
    self->address.jetty = htonl(self->jetty->jetty_id.id);
    self->address.token = htonl(self->md->dev.token.token);
    self->address.cookie = htobe64(ucs_generate_uuid((uintptr_t)self) | 1);
    self->address.caps = htonl(self->config.native_rma ? RM_CAP_RMA : 0);
    if (config->tx_pool) {
        if ((size_t)config->tx_depth > SIZE_MAX / RM_BUFFER_ALIGNMENT ||
            (size_t)config->tx_depth > SIZE_MAX / sizeof(*self->tx_pool_free)) {
            status = UCS_ERR_INVALID_PARAM; goto fail;
        }
        self->tx_pool_buffer = rm_buffer_alloc((size_t)config->tx_depth * RM_BUFFER_ALIGNMENT);
        self->tx_pool_free = calloc(config->tx_depth, sizeof(*self->tx_pool_free));
        if (!self->tx_pool_buffer || !self->tx_pool_free) {
            status = UCS_ERR_NO_MEMORY; goto fail;
        }
        self->tx_pool_seg = rm_register(self, self->tx_pool_buffer,
                            (size_t)config->tx_depth * RM_BUFFER_ALIGNMENT);
        if (!self->tx_pool_seg) { status = UCS_ERR_IO_ERROR; goto fail; }
        for (i = 0; i < config->tx_depth; ++i) self->tx_pool_free[i] = i;
        self->tx_pool_available = config->tx_depth;
        ucs_info("RM_TX_POOL_INIT slots=%u bytes=%zu", config->tx_depth,
                 (size_t)config->tx_depth * RM_BUFFER_ALIGNMENT);
    }
    self->recv = calloc(config->rx_depth, sizeof(*self->recv));
    if (!self->recv) { status = UCS_ERR_NO_MEMORY; goto fail; }
    for (i = 0; i < config->rx_depth; ++i) {
        self->recv[i].buffer = rm_buffer_alloc(RM_PACKET_MAX);
        if (!self->recv[i].buffer) { status = UCS_ERR_NO_MEMORY; goto fail; }
        self->recv[i].seg = rm_register(self, self->recv[i].buffer, RM_PACKET_MAX);
        if (!self->recv[i].seg || rm_post_recv(self, i) != UCS_OK) { status = UCS_ERR_IO_ERROR; goto fail; }
    }
    self->diag.active = !!config->diag;
    if (self->diag.active) {
        ucs_info("RM_CTP_DIAG_INIT version=1 rx_depth=%u tx_depth=%u"
                 " timeout_ms=%.3f threshold_ms=5 slow_limit=%u"
                 " note=software_counters_not_hardware_occupancy",
                 config->rx_depth, config->tx_depth, config->timeout * 1e3,
                 RM_DIAG_LOG_LIMIT);
    }
    return UCS_OK;
fail:
    rm_cleanup(self);
    return status;
}
static void rm_cleanup(uct_rm_urma_iface_t *self)
{
    unsigned i;
    rm_peer_t *p;
    uct_rm_urma_ep_t *ep;
    rm_cost_summary(self);
    if (self->failed == UCS_OK) self->failed = UCS_ERR_CANCELED;
    self->in_progress = 1;
    uct_base_iface_progress_disable(&self->super.super, UCT_PROGRESS_SEND | UCT_PROGRESS_RECV);
    /* Destroy hardware queues before unpinning their buffers. Quarantine on failure. */
    if (!rm_stop_io(self)) {
        ucs_error("rm_urma: queue destruction failed; DMA buffers quarantined"); return;
    }
    while (self->ops) rm_finish(self, self->ops, UCS_ERR_CANCELED);
    while ((ep = self->eps)) {
        self->in_progress = 1;
        ep->closing = 1; rm_ep_progress_flush(ep);
        rm_ep_pending_purge(&ep->super.super, NULL, NULL);
        self->in_progress = 0;
        rm_ep_reap(ep);
    }
    while ((p = self->peers)) {
        self->peers = p->next; urma_unimport_jetty(p->target); rm_rx_cleanup(&p->rx); free(p);
    }
    if (self->recv) {
        for (i = 0; i < self->config.rx_depth; ++i) {
            if (self->recv[i].seg) rm_diag_unregister(self, self->recv[i].seg);
            free(self->recv[i].buffer);
        }
        free(self->recv);
    }
    if (self->tx_pool_seg) {
        /* All queues stopped and operations released above: no outstanding DMA. */
        if (urma_unregister_seg(self->tx_pool_seg) != URMA_SUCCESS) {
            ucs_error("rm_urma: TX pool unregister failed; buffer quarantined");
        } else {
            free(self->tx_pool_buffer);
        }
    } else {
        free(self->tx_pool_buffer);
    }
    free(self->tx_pool_free);
    rm_diag_summary(self, "cleanup");
    if (self->tx_cq) urma_delete_jfc(self->tx_cq);
    if (self->rx_cq) urma_delete_jfc(self->rx_cq);
}
static UCS_CLASS_CLEANUP_FUNC(uct_rm_urma_iface_t) { rm_cleanup(self); }
UCS_CLASS_DEFINE(uct_rm_urma_iface_t, uct_base_iface_t);
static UCS_CLASS_DEFINE_NEW_FUNC(uct_rm_urma_iface_t, uct_iface_t, uct_md_h, uct_worker_h,
                                 const uct_iface_params_t *, const uct_iface_config_t *);
static UCS_CLASS_DEFINE_DELETE_FUNC(uct_rm_urma_iface_t, uct_iface_t);
static unsigned rm_iface_progress(uct_iface_h iface)
{
    return uct_rm_urma_iface_progress(ucs_derived_of(iface, uct_rm_urma_iface_t));
}
uct_iface_ops_t uct_rm_urma_ops = {
    .ep_am_short = rm_ep_am_short, .ep_am_bcopy = rm_ep_am_bcopy,
    .ep_put_short = rm_ep_put_short, .ep_put_bcopy = rm_ep_put_bcopy,
    .ep_get_bcopy = rm_ep_get_bcopy, .ep_put_zcopy = rm_ep_put_zcopy, .ep_get_zcopy = rm_ep_get_zcopy,
    .ep_create = UCS_CLASS_NEW_FUNC_NAME(uct_rm_urma_ep_t), .ep_destroy = UCS_CLASS_DELETE_FUNC_NAME(uct_rm_urma_ep_t),
    .ep_pending_add = rm_ep_pending_add, .ep_pending_purge = rm_ep_pending_purge,
    .ep_flush = rm_ep_flush, .ep_fence = rm_ep_fence, .ep_check = rm_ep_check,
    .iface_query = rm_query, .iface_get_address = rm_get_address,
    .iface_get_device_address = rm_get_dev_address, .iface_is_reachable = rm_reachable,
    .iface_flush = rm_flush, .iface_fence = uct_base_iface_fence,
    .iface_progress = rm_iface_progress, .iface_progress_enable = rm_progress_enable,
    .iface_progress_disable = uct_base_iface_progress_disable,
    .iface_close = UCS_CLASS_DELETE_FUNC_NAME(uct_rm_urma_iface_t)
};
static ucs_status_t rm_query_devices(uct_md_h md, uct_tl_device_resource_t **devs, unsigned *n)
{ return uct_ub_device_query_ports(&ucs_derived_of(md, uct_ub_md_t)->dev, 0, devs, n); }
UCT_TL_DEFINE(&uct_ub_component, rm_urma, rm_query_devices, uct_rm_urma_iface_t,
               "RM_URMA_", rm_config_table, uct_rm_urma_config_t);
