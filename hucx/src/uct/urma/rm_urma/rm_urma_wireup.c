#include "rm_urma_wireup.h"
#include "rm_urma_ep.h"
#include <arpa/inet.h>
#include <endian.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

int rm_address_valid(const rm_address_t *a)
{
    return ntohl(a->magic) == RM_MAGIC && ntohs(a->version) == RM_VERSION &&
           ntohs(a->mtu) == RM_PACKET_MAX && ntohl(a->jetty) && be64toh(a->cookie);
}
urma_target_jetty_t *rm_import(uct_rm_urma_iface_t *iface, const rm_address_t *a)
{
    urma_rjetty_t r = {0};
    urma_token_t token = {.token = ntohl(a->token)};
    memcpy(&r.jetty_id.eid, a->eid, 16);
    r.jetty_id.uasid = ntohl(a->uasid);
    r.jetty_id.id = ntohl(a->jetty);
    r.trans_mode = URMA_TM_RM;
    r.tp_type = URMA_CTP;
    r.type = URMA_JETTY;
    return urma_import_jetty(iface->md->dev.urma_ctx, &r, &token);
}
static void rm_deliver(void *arg, uint8_t id, void *data, size_t n)
{
    uct_rm_urma_iface_t *iface = arg;
    uct_iface_invoke_am(&iface->super, id, data, n, 0);
}
/* Drop after header/session/sender validation, before any protocol state
 * changes. RX progress still reposts the consumed hardware receive buffer. */
static int rm_test_drop_packet(uct_rm_urma_iface_t *iface,
                               const rm_header_t *header)
{
    unsigned *remaining;
    const char *packet_type;

    if (header->type == RM_DATA) {
        remaining = &iface->test_drop_data_remaining;
        packet_type = "DATA";
    } else if (header->type == RM_ACK) {
        remaining = &iface->test_drop_ack_remaining;
        packet_type = "ACK";
    } else {
        return 0;
    }

    if (*remaining == 0) {
        return 0;
    }

    --*remaining;
    ucs_info("RM_CTP_TEST_DROP type=%s stream=%" PRIu64
             " seq=%" PRIu64 " ack=%" PRIu64 " remaining=%u",
             packet_type, be64toh(header->stream), be64toh(header->seq),
             be64toh(header->ack), *remaining);
    return 1;
}

void rm_receive(uct_rm_urma_iface_t *iface, const void *buffer, size_t n,
                const urma_jetty_id_t *sender)
{
    rm_header_t h;
    rm_peer_t *p;
    rm_op_t *op;
    uint64_t seq;
    size_t before, length;
    if (n < sizeof(h)) {
        if (iface->config.trace) ucs_info("RM_CTP_DROP reason=short_header bytes=%zu", n);
        return;
    }
    memcpy(&h, buffer, sizeof(h));
    if (!rm_header_valid(&h, n, be64toh(iface->address.cookie)) ||
        !rm_address_valid(&h.source)) {
        if (iface->config.trace) ucs_info("RM_CTP_DROP reason=wire_format_or_session bytes=%zu", n);
        return;
    }
    /* The advertised return address must match the device's receive completion. */
    if (memcmp(h.source.eid, &sender->eid, 16) ||
        ntohl(h.source.uasid) != sender->uasid || ntohl(h.source.jetty) != sender->id) {
        if (iface->config.trace)
            ucs_info("RM_CTP_DROP reason=sender_identity header_jetty=%u cq_jetty=%u header_uasid=%u cq_uasid=%u eid_match=%d",
                     ntohl(h.source.jetty), sender->id, ntohl(h.source.uasid), sender->uasid,
                     !memcmp(h.source.eid, &sender->eid, 16));
        return;
    }
    if (rm_test_drop_packet(iface, &h)) {
        return;
    }
    if (h.type == RM_ACK) {
        uct_rm_urma_ep_t *ep;
        for (ep = iface->eps; ep; ep = ep->next) {
            if (ep->stream != be64toh(h.stream) ||
                memcmp(&ep->remote, &h.source, sizeof(h.source))) continue;
            if (!rm_ack_valid(be64toh(h.ack), ep->issued)) return;
            for (op = iface->ops; op; op = op->next)
                if (op->ep == ep && op->kind == RM_DATA && op->seq <= be64toh(h.ack)) op->acked = 1;
            return;
        }
        return;
    }
    if (h.am_id >= UCT_AM_ID_MAX) return;
    for (p = iface->peers; p; p = p->next)
        if (p->stream == be64toh(h.stream) && !memcmp(&p->address, &h.source, sizeof(h.source))) break;
    if (!p) {
        if (iface->peer_count >= iface->config.max_peers) {
            if (iface->config.trace) ucs_info("RM_CTP_DROP reason=max_receive_streams");
            return;
        }
        p = calloc(1, sizeof(*p));
        if (!p) return;
        p->address = h.source;
        p->stream = be64toh(h.stream);
        p->target = rm_import(iface, &p->address);
        if (!p->target) {
            if (iface->config.trace) ucs_info("RM_CTP_DROP reason=return_address_import jetty=%u", ntohl(h.source.jetty));
            free(p); return;
        }
        p->next = iface->peers; iface->peers = p; iface->peer_count++;
    }
    seq = be64toh(h.seq); length = n - sizeof(h);
    if (iface->config.trace && (seq <= p->rx.delivered)) {
        ucs_info("RM_CTP_DUPLICATE stream=%" PRIu64 " seq=%" PRIu64
                 " delivered=%" PRIu64, p->stream, seq, p->rx.delivered);
    }
    /* Reserve no unbounded heap for missing packets. A gap-filling packet may
     * temporarily exceed the budget by one payload; it immediately drains RX. */
    if (seq > p->rx.delivered && seq - p->rx.delivered > 1 &&
        !p->rx.slots[seq % RM_WINDOW].seq &&
        length > iface->config.max_buffered - iface->buffered) {
        p->ack_pending = 1;
        return;
    }
    before = p->rx.buffered;
    if (rm_rx_push(&p->rx, seq, h.am_id,
                   (const char *)buffer + sizeof(h), n - sizeof(h), rm_deliver, iface) >= 0)
        p->ack_pending = 1;
    iface->buffered = iface->buffered - before + p->rx.buffered;
}
void rm_progress_acks(uct_rm_urma_iface_t *iface)
{
    rm_peer_t *p;
    rm_op_t *op;
    rm_header_t *h;
    for (p = iface->peers; p; p = p->next) {
        if (!p->ack_pending || iface->inflight >= iface->config.tx_depth) continue;
        op = rm_op_alloc(iface, sizeof(*h));
        if (!op) return;
        op->kind = RM_ACK; op->target = p->target;
        h = op->buffer;
        h->magic = htonl(RM_MAGIC); h->version = RM_VERSION; h->type = RM_ACK;
        h->stream = htobe64(p->stream); h->ack = htobe64(p->rx.delivered);
        h->destination = p->address.cookie; h->source = iface->address;
        if (rm_post_op(iface, op) == UCS_OK) p->ack_pending = 0;
        else rm_op_release(iface, op);
    }
}
