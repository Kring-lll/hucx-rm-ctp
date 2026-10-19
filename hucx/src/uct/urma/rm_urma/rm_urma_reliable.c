#include "rm_urma_reliable.h"
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <arpa/inet.h>

int rm_rx_push(rm_rx_state_t *rx, uint64_t seq, uint8_t id, const void *data,
               size_t length, rm_deliver_cb_t cb, void *arg)
{
    rm_rx_slot_t *slot;
    if (!seq || length > RM_PAYLOAD_MAX || (!data && length)) return -1;
    if (seq <= rx->delivered) return 1; /* ACK loss: don't deliver twice. */
    if (seq - rx->delivered > RM_WINDOW) return -1;
    slot = &rx->slots[seq % RM_WINDOW];
    if (slot->seq == seq) return 1;
    if (slot->seq) return -1;
    slot->data = malloc(length ? length : 1);
    if (!slot->data) return -2;
    if (length) memcpy(slot->data, data, length);
    slot->length = length;
    rx->buffered += length;
    slot->am_id = id;
    slot->seq = seq;
    while (rx->delivered != UINT64_MAX) {
        slot = &rx->slots[(rx->delivered + 1) % RM_WINDOW];
        if (slot->seq != rx->delivered + 1) break;
        cb(arg, slot->am_id, slot->data, slot->length);
        rx->delivered++;
        rx->buffered -= slot->length;
        free(slot->data);
        memset(slot, 0, sizeof(*slot));
    }
    return 0;
}
void rm_rx_cleanup(rm_rx_state_t *rx)
{
    unsigned i;
    for (i = 0; i < RM_WINDOW; ++i) free(rx->slots[i].data);
    memset(rx, 0, sizeof(*rx));
}
int rm_ack_valid(uint64_t ack, uint64_t issued) { return ack <= issued; }
int rm_retry_due(uint64_t now, uint64_t sent, uint64_t base, unsigned attempts)
{
    unsigned shift = attempts > 5 ? 5 : attempts;
    uint64_t wait = base > (UINT64_MAX >> shift) ? UINT64_MAX : base << shift;
    return now >= sent && now - sent >= wait;
}
int rm_header_valid(const rm_header_t *h, size_t n, uint64_t destination)
{
    return n >= sizeof(*h) && n <= RM_PACKET_MAX &&
           ntohl(h->magic) == RM_MAGIC && h->version == RM_VERSION &&
           !h->reserved && (h->type == RM_DATA || h->type == RM_ACK) &&
           ntohl(h->source.magic) == RM_MAGIC &&
           ntohs(h->source.version) == RM_VERSION &&
           ntohs(h->source.mtu) == RM_PACKET_MAX &&
           be64toh(h->destination) == destination &&
           be64toh(h->source.cookie) && be64toh(h->stream) &&
           (h->type != RM_ACK || n == sizeof(*h));
}
