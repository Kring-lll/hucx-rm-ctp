#ifndef UCT_RM_URMA_EP_H
#define UCT_RM_URMA_EP_H
#include "rm_urma_iface.h"
typedef struct rm_flush_req {
    struct rm_flush_req *next;
    uint64_t barrier;
    uct_completion_t *comp;
} rm_flush_req_t;
struct uct_rm_urma_ep {
    uct_base_ep_t super;
    uct_rm_urma_iface_t *iface;
    uct_rm_urma_ep_t *next;
    rm_address_t remote;
    urma_target_jetty_t *target;
    uint64_t stream, issued;
    unsigned outstanding, am_outstanding;
    ucs_status_t failed;
    int closing, rma_outstanding, fenced;
    ucs_queue_head_t pending;
    rm_flush_req_t *flushes;
};
UCS_CLASS_DECLARE(uct_rm_urma_ep_t, const uct_ep_params_t *);
UCS_CLASS_DECLARE_NEW_FUNC(uct_rm_urma_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_rm_urma_ep_t, uct_ep_t);
ucs_status_t rm_ep_am_short(uct_ep_h, uint8_t, uint64_t, const void *, unsigned);
ssize_t rm_ep_am_bcopy(uct_ep_h, uint8_t, uct_pack_callback_t, void *, unsigned);
ucs_status_t rm_ep_put_short(uct_ep_h, const void *, unsigned, uint64_t, uct_rkey_t);
ssize_t rm_ep_put_bcopy(uct_ep_h, uct_pack_callback_t, void *, uint64_t, uct_rkey_t);
ucs_status_t rm_ep_get_bcopy(uct_ep_h, uct_unpack_callback_t, void *, size_t, uint64_t, uct_rkey_t, uct_completion_t *);
ucs_status_t rm_ep_put_zcopy(uct_ep_h, const uct_iov_t *, size_t, uint64_t, uct_rkey_t, uct_completion_t *);
ucs_status_t rm_ep_get_zcopy(uct_ep_h, const uct_iov_t *, size_t, uint64_t, uct_rkey_t, uct_completion_t *);
ucs_status_t rm_ep_flush(uct_ep_h, unsigned, uct_completion_t *);
ucs_status_t rm_ep_fence(uct_ep_h, unsigned);
ucs_status_t rm_ep_check(uct_ep_h, unsigned, uct_completion_t *);
ucs_status_t rm_ep_pending_add(uct_ep_h, uct_pending_req_t *, unsigned);
void rm_ep_pending_purge(uct_ep_h, uct_pending_purge_callback_t, void *);
void rm_ep_reap(uct_rm_urma_ep_t *);
void rm_ep_progress_pending(uct_rm_urma_ep_t *);
void rm_ep_progress_flush(uct_rm_urma_ep_t *);
#endif
