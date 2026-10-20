#ifndef UCT_RM_URMA_IFACE_H
#define UCT_RM_URMA_IFACE_H
#include <uct/urma/base/urma_md.h>
#include <uct/base/uct_iface.h>
#include <ucs/sys/math.h>
#include "rm_urma_reliable.h"
typedef struct uct_rm_urma_ep uct_rm_urma_ep_t;
typedef struct rm_op rm_op_t;
typedef struct rm_peer rm_peer_t;
typedef struct {
    uct_iface_config_t super;
    unsigned rx_depth, tx_depth, priority, retries, max_peers;
    size_t max_buffered;
    double timeout, completion_timeout;
    int native_rma, trace;
    int diag;
    /* Test-only receive drops; zero leaves normal communication unchanged. */
    unsigned test_drop_data;
    unsigned test_drop_ack;
} uct_rm_urma_config_t;
typedef struct { void *buffer; urma_target_seg_t *seg; int posted; } rm_recv_t;
/* Optional counters observe software progress, not hardware queue occupancy. */
typedef struct {
    int active;
    unsigned slow_logs;
    int error_logged;
    int failure_summary;
    uint64_t last_exit;
    uint64_t calls, rx_records, tx_records, rx_full, tx_full;
    uint64_t reposts, repost_errors, repost_max;
    uint64_t registers, register_ticks, register_max;
    uint64_t unregisters, unregister_ticks, unregister_max;
    uint64_t gap_max, progress_max;
    uint64_t phase_max[6];
    uint64_t cost_count[4], cost_alloc[4], cost_reg[4], cost_unreg[4];
    uint64_t cost_post_count[4], cost_post[4], cost_cqe_count[4], cost_cqe[4];
} rm_diag_t;
typedef struct uct_rm_urma_iface {
    uct_base_iface_t super;
    uct_ub_md_t *md;
    urma_jfc_t *tx_cq, *rx_cq;
    urma_jfr_t *jfr;
    urma_jetty_t *jetty;
    rm_address_t address;
    uct_rm_urma_config_t config;
    rm_recv_t *recv;
    rm_op_t *ops;
    rm_peer_t *peers;
    uct_rm_urma_ep_t *eps;
    uint64_t next_id, retry_ticks, completion_ticks;
    unsigned inflight, peer_count, op_count;
    unsigned test_drop_data_remaining;
    unsigned test_drop_ack_remaining;
    size_t buffered;
    ucs_status_t failed;
    int in_progress;
    rm_diag_t diag;
} uct_rm_urma_iface_t;
struct rm_peer {
    rm_peer_t *next;
    rm_address_t address;
    uint64_t stream;
    urma_target_jetty_t *target;
    rm_rx_state_t rx;
    int ack_pending;
};
struct rm_op {
    rm_op_t *next;
    uint64_t id, seq, sent;
    uint64_t cost_alloc, cost_reg;
    unsigned attempts;
    int inflight, acked, kind;
    uct_rm_urma_ep_t *ep;
    urma_target_jetty_t *target;
    void *buffer;
    size_t length;
    urma_target_seg_t *seg, *remote_seg;
    urma_sge_t local, remote;
    urma_opcode_t opcode;
    uct_completion_t *comp;
    uct_unpack_callback_t unpack;
    void *unpack_arg;
};
UCS_CLASS_DECLARE(uct_rm_urma_iface_t, uct_md_h, uct_worker_h,
                  const uct_iface_params_t *, const uct_iface_config_t *);
unsigned uct_rm_urma_iface_progress(void *);
ucs_status_t rm_post_recv(uct_rm_urma_iface_t *, unsigned);
ucs_status_t rm_post_op(uct_rm_urma_iface_t *, rm_op_t *);
rm_op_t *rm_op_alloc(uct_rm_urma_iface_t *, size_t);
void rm_op_release(uct_rm_urma_iface_t *, rm_op_t *);
void rm_ep_fail(uct_rm_urma_ep_t *, ucs_status_t);
urma_target_seg_t *rm_register(uct_rm_urma_iface_t *, void *, size_t);
extern uct_iface_ops_t uct_rm_urma_ops;
#endif
